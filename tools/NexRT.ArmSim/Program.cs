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
            if(args.Length!=3)throw new ArgumentException("Usage: NexRT.ArmSim QEMU FIRMWARE_ELF OUTPUT_DIRECTORY");
            string qemu=Path.GetFullPath(args[0]),elf=Path.GetFullPath(args[1]);
            if(!File.Exists(qemu)||!File.Exists(elf))throw new FileNotFoundException("Missing QEMU or firmware ELF");
            string root=Path.Combine(Path.GetFullPath(args[2]),DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff"));
            Directory.CreateDirectory(root);
            await File.WriteAllTextAsync(Path.Combine(root,"manifest.txt"),
                $"QEMU={qemu}\nELF={elf}\nELF_SHA256={Convert.ToHexStringLower(SHA256.HashData(await File.ReadAllBytesAsync(elf)))}\n");
            await Run(qemu,["--version"],Path.Combine(root,"qemu-version.log"),false);
            foreach(int shift in new[]{3,4,5})
                await Run(qemu,["-M","mps2-an385","-display","none","-monitor","none",
                    "-serial","none","-semihosting-config","enable=on,target=native",
                    "-icount",$"shift={shift},align=off,sleep=off","-kernel",elf],
                    Path.Combine(root,$"shift-{shift}.log"),true);
            await File.WriteAllTextAsync(Path.Combine(root,"PASS.txt"),"Three bounded Cortex-M3 stress runs passed.\n");
            Console.WriteLine("ARM stress evidence: "+root);
            return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
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
        var watch=Stopwatch.StartNew();
        var pipes=await Probe("pipe-parent",TimeSpan.FromSeconds(2));
        if(!pipes.TimedOut||watch.Elapsed>TimeSpan.FromSeconds(4)||!pipes.Output.Contains("PARENT_EXIT",StringComparison.Ordinal))throw new IOException("Inherited-pipe timeout fixture failed");
        // The finite pipe-holding fixture has its own five-second lifetime; do
        // not leave that detached fixture alive after the self-test completes.
        await Task.Delay(5100);
        Console.WriteLine("PASS: native process success/failure, timeout, output cap and inherited-pipe cancellation");
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
    }
}
