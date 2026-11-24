using System.Diagnostics;
namespace NexRT.Configure;
/// <summary>Exercises the actual CMake option guards without configuring compilers or building firmware.</summary>
internal static class ConfigurationChecks
{
    /// <summary>Checks accepted boundaries and rejected syntax, ranges and feature combinations.</summary>
    public static void Run(string root)
    {
        int cases=0;
        void Check(bool accepted,string diagnostic,params string[] settings)
        {
            var start=new ProcessStartInfo("cmake"){
                WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true,
                RedirectStandardOutput=true,RedirectStandardError=true
            };
            foreach(var setting in settings)start.ArgumentList.Add("-D"+setting);
            start.ArgumentList.Add("-P");start.ArgumentList.Add(Path.Combine(root,"cmake/configuration.cmake"));
            using var process=Process.Start(start)??throw new IOException("Cannot start CMake.");
            var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();
            if(!process.WaitForExit(30000)){process.Kill(entireProcessTree:true);process.WaitForExit();throw new TimeoutException("CMake configuration check exceeded 30 seconds.");}
            string report=output.GetAwaiter().GetResult()+error.GetAwaiter().GetResult();
            if((process.ExitCode==0)!=accepted||!accepted&&!report.Contains(diagnostic,StringComparison.Ordinal))
                throw new InvalidOperationException("Unexpected CMake result for "+string.Join(", ",settings)+": "+report);
            ++cases;
        }
        foreach(var (key,low,high) in new[]{
            ("RTOS_MAX_THREADS","1","255"),("RTOS_TICK_HZ","1","4294967295"),
            ("RTOS_IDLE_STACK_BYTES","512","4294967280"),("RTOS_IPC_MAX_MESSAGE_BYTES","1","512"),
            ("RTOS_MAX_HELD_MUTEXES","1","255")
        })
        {
            Check(true,"",key+"="+low);Check(true,"",key+"="+high);
            foreach(var value in new[]{"0","-1","08","1+1","1;2","999999999999999999999"})Check(false,key,key+"="+value);
        }
        Check(false,"RTOS_MAX_THREADS","RTOS_MAX_THREADS=256");
        Check(false,"RTOS_TICK_HZ","RTOS_TICK_HZ=4294967296");
        Check(false,"RTOS_IDLE_STACK_BYTES","RTOS_IDLE_STACK_BYTES=496");
        Check(false,"RTOS_IDLE_STACK_BYTES","RTOS_IDLE_STACK_BYTES=513");
        Check(false,"RTOS_IPC_MAX_MESSAGE_BYTES","RTOS_IPC_MAX_MESSAGE_BYTES=513");
        Check(false,"RTOS_MAX_HELD_MUTEXES","RTOS_MAX_HELD_MUTEXES=256");
        Check(false,"requires RTOS_SYNC","RTOS_LOGGING=ON","RTOS_SYNC=OFF");
        Check(true,"","RTOS_LOGGING=ON","RTOS_SYNC=ON");
        Check(true,"","RTOS_STORAGE=ON");
        // Modular Tier 1-4 and developer-configurable flight build checks
        Check(true,"","RTOS_FLIGHT_BUILD=ON","RTOS_BOOT=ON","RTOS_RM_PERIOD=ON","RTOS_POOL=ON","RTOS_WORKQUEUE=ON","RTOS_CLOCK_EPOCH=ON","RTOS_TICKLESS=ON","RTOS_TOPIC=ON","RTOS_TRACE=ON");
        Check(true,"","RTOS_FLIGHT_BUILD=ON","RTOS_MONITOR=ON");
        Check(true,"","RTOS_FLIGHT_BUILD=ON","RTOS_ELF_LOADER=ON");
        Check(true,"","RTOS_FLIGHT_BUILD=ON","RTOS_SCRIPT=ON");
        Check(true,"","RTOS_FLIGHT_BUILD=ON","RTOS_MONITOR=ON","RTOS_SCRIPT=ON","RTOS_ELF_LOADER=ON","RTOS_AMP=ON");
        Check(true,"","RTOS_FLIGHT_BUILD=OFF","RTOS_MONITOR=ON","RTOS_SCRIPT=ON","RTOS_ELF_LOADER=ON","RTOS_AMP=ON");
        Console.WriteLine($"PASS: {cases} CMake syntax, boundary, alignment, modular option and feature-dependency checks.");
    }
}
