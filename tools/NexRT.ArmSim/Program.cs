using System.Diagnostics;
using System.Security.Cryptography;

/// <summary>Runs bounded real-kernel ARM simulations with varied virtual timer timing.</summary>
internal static class Program
{
    /// <summary>Requires QEMU path, firmware ELF and output directory; preserves every run.</summary>
    public static async Task<int> Main(string[] args)
    {
        try
        {
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

    /// <summary>Streams both output pipes, enforces a 120-second bound and verifies guest success.</summary>
    private static async Task Run(string executable,string[] arguments,string log,bool requireGuestPass)
    {
        var start=new ProcessStartInfo(executable){RedirectStandardOutput=true,RedirectStandardError=true,UseShellExecute=false};
        foreach(string argument in arguments)start.ArgumentList.Add(argument);
        using var process=Process.Start(start)??throw new InvalidOperationException("Cannot launch QEMU");
        var lines=new List<string>();var gate=new object();
        async Task Drain(StreamReader reader)
        {
            while(await reader.ReadLineAsync() is string line)
                lock(gate){lines.Add(line);Console.WriteLine(line);}
        }
        Task stdout=Drain(process.StandardOutput),stderr=Drain(process.StandardError);
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(120));
        bool timedOut=false;
        try{await process.WaitForExitAsync(timeout.Token);}
        catch(OperationCanceledException){timedOut=true;process.Kill(entireProcessTree:true);await process.WaitForExitAsync();}
        await Task.WhenAll(stdout,stderr);
        await File.WriteAllLinesAsync(log,lines);
        if(timedOut)throw new TimeoutException("QEMU exceeded 120 seconds; see "+log);
        if(process.ExitCode!=0||requireGuestPass&&(!lines.Contains("ARM_STRESS_PASS")||lines.Any(line=>line.StartsWith("FAIL:",StringComparison.Ordinal))))
            throw new InvalidOperationException($"QEMU/guest failed (exit {process.ExitCode}); see {log}");
    }
}
