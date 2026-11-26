using System.Diagnostics;
using System.Security.Cryptography;
using NexRT.Host;

/// <summary>Runs bounded real-kernel ARM simulations with varied virtual timer timing.</summary>
internal static class Program
{
    /// <summary>Requires QEMU path, firmware ELF and output directory; preserves every run.</summary>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if(args.Length==2&&args[0]=="--fixture")return await Fixture(args[1]);
            if(args.Length==1&&args[0]=="--self-test"){await SelfTest();return 0;}
            if(args.Length==4&&args[0]=="--cameo-embed")return await RunCameoEmbed(args[1],args[2],args[3]);
            if(args.Length!=3)throw new ArgumentException("Usage: NexRT.ArmSim QEMU FIRMWARE_ELF OUTPUT_DIRECTORY | --cameo-embed QEMU ELF HEX");
            string qemu=Path.GetFullPath(args[0]),elf=Path.GetFullPath(args[1]);
            if(!File.Exists(qemu)||!File.Exists(elf))throw new FileNotFoundException("Missing QEMU or firmware ELF");
            string root=Path.Combine(Path.GetFullPath(args[2]),DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff"));
            Directory.CreateDirectory(root);
            string elfHash=await Digest(elf),qemuHash=await Digest(qemu);
            await File.WriteAllTextAsync(Path.Combine(root,"manifest.txt"),
                $"QEMU={qemu}\nQEMU_SHA256={qemuHash}\nELF={elf}\nELF_SHA256={elfHash}\nICOUNT_SHIFTS=3,4,5\n");
            await Run(qemu,["--version"],Path.Combine(root,"qemu-version.log"),false);
            foreach(int shift in new[]{3,4,5})
                await Run(qemu,["-M","mps2-an385","-display","none","-monitor","none",
                    "-serial","none","-semihosting-config","enable=on,target=native",
                    "-icount",$"shift={shift},align=off,sleep=off","-kernel",elf],
                    Path.Combine(root,$"shift-{shift}.log"),true);
            if(elfHash!=await Digest(elf)||qemuHash!=await Digest(qemu))
                throw new IOException("Firmware or QEMU changed during the campaign");
            await File.WriteAllTextAsync(Path.Combine(root,"PASS.txt"),"Three bounded Cortex-M3 stress runs passed.\n");
            Console.WriteLine("ARM stress evidence: "+root);
            return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
    }

    /// <summary>Identifies a file without retaining its complete contents in host memory.</summary>
    private static async Task<string> Digest(string path)
    {
        await using var stream=File.OpenRead(path);
        return Convert.ToHexStringLower(await SHA256.HashDataAsync(stream));
    }

    /// <summary>Finite subprocess fixtures, run only by the native runner self-test.</summary>
    private static async Task<int> Fixture(string mode)
    {
        switch(mode)
        {
            case "exit":Console.WriteLine("FIXTURE_OK");return 0;
            case "error":Console.Error.WriteLine("FIXTURE_ERROR");return 7;
            case "sleep":await Task.Delay(5000);return 0;
            case "flood":for(;;)Console.Write(new string('x',4096));
            case "capture-flood":
                var nested=new ProcessStartInfo(Environment.ProcessPath!);
                nested.ArgumentList.Add("--fixture");nested.ArgumentList.Add("flood");
                var captured=await HostProcess.Run(nested,TimeSpan.FromSeconds(3),1_000_000);
                return captured.OutputLimit&&captured.Output.Length==1_000_000?0:8;
            case "pipe-parent":
                var start=new ProcessStartInfo(Environment.ProcessPath!){UseShellExecute=false,CreateNoWindow=true};
                start.ArgumentList.Add("--fixture");start.ArgumentList.Add("sleep");
                using(var child=Process.Start(start)??throw new IOException("Cannot start pipe fixture"))
                    Console.WriteLine("PARENT_EXIT");
                return 0;
            default:throw new ArgumentException("Unknown process fixture");
        }
    }

    /// <summary>Verifies success, failure, execution timeout, output limits and descendant-held pipes.</summary>
    private static async Task SelfTest()
    {
        string executable=Environment.ProcessPath??throw new IOException("Missing native process path");
        if(Path.GetFileNameWithoutExtension(executable).Equals("dotnet",StringComparison.OrdinalIgnoreCase))
            throw new NotSupportedException("The process-fixture self-test requires the native runner executable");
        async Task<HostResult> Probe(string mode,TimeSpan limit,int cap=8192)
        {
            var start=new ProcessStartInfo(executable);start.ArgumentList.Add("--fixture");start.ArgumentList.Add(mode);
            return await HostProcess.Run(start,limit,cap);
        }
        var normal=await Probe("exit",TimeSpan.FromSeconds(10));
        if(normal.ExitCode!=0||normal.TimedOut||!normal.Output.Contains("FIXTURE_OK",StringComparison.Ordinal))throw new IOException("Normal process fixture failed");
        var failure=await Probe("error",TimeSpan.FromSeconds(10));
        if(failure.ExitCode!=7||!failure.Output.Contains("FIXTURE_ERROR",StringComparison.Ordinal))throw new IOException("Exit propagation fixture failed");
        var timeout=await Probe("sleep",TimeSpan.FromMilliseconds(500));
        if(!timeout.TimedOut)throw new IOException("Execution timeout fixture failed");
        var flood=await Probe("flood",TimeSpan.FromSeconds(10),512);
        if(!flood.OutputLimit||flood.Output.Length!=512)throw new IOException("Output-cap fixture failed");
        // Deliberately do not read this runner's stdout/stderr until it exits.
        // Old synchronous live forwarding filled this outer pipe and hung.
        var stalledStart=new ProcessStartInfo(executable){UseShellExecute=false,CreateNoWindow=true,
            RedirectStandardOutput=true,RedirectStandardError=true};
        stalledStart.ArgumentList.Add("--fixture");stalledStart.ArgumentList.Add("capture-flood");
        using(var stalled=Process.Start(stalledStart)??throw new IOException("Cannot start stalled-output fixture"))
        {
            try{await stalled.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));}
            catch(TimeoutException){stalled.Kill(entireProcessTree:true);throw;}
            if(stalled.ExitCode!=0)throw new IOException("Stalled outer-output fixture failed");
        }
        var watch=Stopwatch.StartNew();
        var pipes=await Probe("pipe-parent",TimeSpan.FromSeconds(2));
        if(!pipes.TimedOut||watch.Elapsed>TimeSpan.FromSeconds(4)||!pipes.Output.Contains("PARENT_EXIT",StringComparison.Ordinal))throw new IOException("Inherited-pipe timeout fixture failed");
        // The finite pipe-holding fixture has its own five-second lifetime; do
        // not leave that detached fixture alive after the self-test completes.
        await Task.Delay(5100);
        Console.WriteLine("PASS: process exit, timeout, output cap, stalled outer consumer and inherited pipes");
    }

    /// <summary>Streams both output pipes, enforces a 120-second bound and verifies guest success.</summary>
    private static async Task Run(string executable,string[] arguments,string log,bool requireGuestPass)
    {
        var start=new ProcessStartInfo(executable){RedirectStandardOutput=true,RedirectStandardError=true,UseShellExecute=false};
        foreach(string argument in arguments)start.ArgumentList.Add(argument);
        var result=await HostProcess.Run(start,TimeSpan.FromSeconds(120));
        await File.WriteAllTextAsync(log,result.Output);
        if(result.TimedOut)throw new TimeoutException("QEMU execution/output exceeded 120 seconds; see "+log);
        if(result.OutputLimit||result.IoError is not null)throw new IOException("QEMU output failure; see "+log);
        string[] lines=result.Output.Split('\n').Select(line=>line.TrimEnd('\r')).ToArray();
        if(result.ExitCode!=0||requireGuestPass&&(!lines.Contains("ARM_STRESS_PASS")||lines.Any(line=>line.StartsWith("FAIL:",StringComparison.Ordinal))))
            throw new InvalidOperationException($"QEMU/guest failed (exit {result.ExitCode}); see {log}");
        if(requireGuestPass&&(!lines.Contains("PASS: register canaries, negative probes, PSP alignment padding")||
            !lines.Contains("PASS: masked timer catch-up and counter continuity")))
            throw new IOException("Missing ARM register/timer coverage markers; see "+log);
    }

    /// <summary>Boots a Cortex-M3 Cameo.Embed firmware ELF in QEMU, streams a hex CEMB script over UART0, and verifies live execution, single-stepping, and bus-trace capture.</summary>
    private static async Task<int> RunCameoEmbed(string qemuPath, string elfPath, string hexPath)
    {
        string qemu=Path.GetFullPath(qemuPath),elf=Path.GetFullPath(elfPath),hexFile=Path.GetFullPath(hexPath);
        if(!File.Exists(qemu)||!File.Exists(elf)||!File.Exists(hexFile))
            throw new FileNotFoundException("Missing QEMU, firmware ELF, or hex script file");
        string hexData=string.Concat((await File.ReadAllTextAsync(hexFile)).Where(c=>!char.IsWhiteSpace(c)));
        if(hexData.Length==0||(hexData.Length%2)!=0)
            throw new InvalidDataException("Invalid Cameo.Embed hex payload");

        var start=new ProcessStartInfo(qemu)
        {
            RedirectStandardInput=true,
            RedirectStandardOutput=true,
            RedirectStandardError=true,
            UseShellExecute=false,
            CreateNoWindow=true,
        };
        foreach(string a in new[]{"-M","mps2-an385","-display","none","-monitor","none",
            "-serial","stdio","-semihosting-config","enable=on,target=native","-kernel",elf})
            start.ArgumentList.Add(a);

        using var proc=Process.Start(start)??throw new IOException("Cannot launch QEMU");
        try
        {
            async Task<(string Match, List<string> All)> Send(string cmd, string expectPrefix)
            {
                await proc.StandardInput.WriteLineAsync(cmd);
                await proc.StandardInput.FlushAsync();
                var collected=new List<string>();
                while(true)
                {
                    string? raw=await proc.StandardOutput.ReadLineAsync().WaitAsync(TimeSpan.FromSeconds(15));
                    if(raw is null)throw new IOException($"QEMU EOF waiting for '{expectPrefix}' after '{cmd}'");
                    string s=raw.Trim();
                    if(s.Length==0)continue;
                    collected.Add(s);
                    if(s.StartsWith(expectPrefix,StringComparison.Ordinal)||s.StartsWith("err ",StringComparison.Ordinal))
                        return (s,collected);
                }
            }

            string? banner=await proc.StandardOutput.ReadLineAsync().WaitAsync(TimeSpan.FromSeconds(15));
            if(banner is null||!banner.Trim().StartsWith("CAMEO_EMBED_ARM_READY",StringComparison.Ordinal))
                throw new IOException($"Unexpected Cortex-M3 banner: '{banner ?? "<eof>"}'");
            int idx=banner.IndexOf("scratch=0x",StringComparison.OrdinalIgnoreCase);
            if(idx<0)throw new IOException("Missing scratch=0x... in ready banner");
            string scratchAddr=banner[(idx+"scratch=".Length)..].Trim().Split(' ')[0];

            var (rst,_)=await Send("script reset","ok ");
            if(rst!="ok reset")throw new IOException("script reset failed: "+rst);
            for(int off=0;off<hexData.Length;off+=64)
            {
                string chunk=hexData.Substring(off,Math.Min(64,hexData.Length-off));
                var (hr,_)=await Send("script hex "+chunk,"ok ");
                if(!hr.StartsWith("ok bytes=",StringComparison.Ordinal))
                    throw new IOException($"script hex failed at offset {off}: {hr}");
            }
            var (ld,_)=await Send("script load","ok ");
            if(!ld.StartsWith("ok loaded crc=0x",StringComparison.Ordinal))
                throw new IOException("script load failed: "+ld);

            var (run,_)=await Send($"script run probe_hw {scratchAddr}","ok");
            if(!run.StartsWith("ok ret=",StringComparison.Ordinal))
                throw new IOException("script run probe_hw failed: "+run);

            var (bt,btLines)=await Send("script bustrace","ok bustrace");
            if(!bt.StartsWith("ok bustrace count=",StringComparison.Ordinal)||btLines.Count<2)
                throw new IOException("script bustrace failed: "+bt);

            var (p0,_)=await Send($"script start probe_hw {scratchAddr}","ok paused");
            if(!p0.StartsWith("ok paused",StringComparison.Ordinal))throw new IOException("script start failed: "+p0);
            var (st1,_)=await Send("script step","ok stepped");
            if(!st1.StartsWith("ok stepped",StringComparison.Ordinal))throw new IOException("script step failed: "+st1);
            var (rg,_)=await Send("script regs","paused=1");
            if(!rg.StartsWith("paused=1",StringComparison.Ordinal))throw new IOException("script regs failed: "+rg);
            var (ct,_)=await Send("script cont","ok ret=");
            if(ct!=run)throw new IOException($"Stepped continue mismatch: expected '{run}', got '{ct}'");

            await proc.StandardInput.WriteLineAsync("quit");
            await proc.StandardInput.FlushAsync();
            await proc.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));
            if(proc.ExitCode!=0)throw new IOException($"QEMU exited with code {proc.ExitCode}");
            Console.WriteLine($"PASS: Cortex-M3 QEMU Cameo.Embed live UART hot-load, stepping & bustrace ({ld}, {run}, {bt})");
            return 0;
        }
        finally
        {
            if(!proc.HasExited)try{proc.Kill(entireProcessTree:true);}catch{}
        }
    }
}
