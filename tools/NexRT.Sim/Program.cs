using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using NexRT.Host;

namespace NexRT.Sim;

/// <summary>Native C# orchestration for a real RISCue/SharpTestBench UART simulation; no Python build glue.</summary>
internal static class Program
{
    /// <summary>Builds a checked model and test DLL, then preserves a fresh bounded simulation run.</summary>
    public static async Task<int> Main(string[] args)
    {
        string? evidence=null;
        try
        {
            if(args.Length<4||args.Length>6||!OperatingSystem.IsLinux())throw new ArgumentException("Usage on Linux/WSL: NexRT.Sim ROOT RISCUE_ROOT SHARPTESTBENCH_ROOT FIRMWARE_ELF [--prepare-soak|--smoke|--soak|--atomics-baseline|--atomics-zaamo] [HOST_SECONDS]");
            string mode=args.Length>=5?args[4]:"uart";
            if(mode is not ("uart" or "--prepare-soak" or "--smoke" or "--soak" or "--atomics-baseline" or "--atomics-zaamo"))throw new ArgumentException("Unknown simulation mode");
            bool atomics=mode.StartsWith("--atomics-",StringComparison.Ordinal),enabled=mode=="--atomics-zaamo";
            bool soak=mode!="uart";
            int hostSeconds=args.Length==6?int.Parse(args[5],CultureInfo.InvariantCulture):(atomics?300:(mode=="--smoke"?300:43200));
            if(hostSeconds is <1 or >43200)throw new ArgumentException("Host seconds must be in 1..43200");
            string root=Path.GetFullPath(args[0]),riscue=Path.GetFullPath(args[1]),sharp=Path.GetFullPath(args[2]);
            string elf=Path.GetFullPath(args[3],root),build=Path.Combine(root,atomics?(enabled?"build/sharp-atomics-zaamo":"build/sharp-atomics-baseline"):(soak?"build/sharp-soak":"build/sharp-demo"));
            Directory.CreateDirectory(build);
            using var ownership=soak?new FileStream(Path.Combine(build,"active.lock"),FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None):null;
            string run=Path.Combine(build,"runs",DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff"));
            Directory.CreateDirectory(run);
            evidence=run;
            if(soak)File.WriteAllText(Path.Combine(build,"current-run.txt"),run+"\n");
            Console.WriteLine("Evidence: "+run);
            File.WriteAllText(Path.Combine(run,"launcher.pid"),Environment.ProcessId.ToString(CultureInfo.InvariantCulture));
            string portableCocotb=Path.GetFullPath(Path.Combine(root,"../runtime/cocotb"));
            string cocotb=Environment.GetEnvironmentVariable("NEXRT_COCOTB_ROOT")??
                (Directory.Exists(portableCocotb)?portableCocotb:
                Directory.GetDirectories(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),".venvs/sharptestbench/lib"),"python*")
                    .Select(path=>Path.Combine(path,"site-packages/cocotb")).Single(Directory.Exists));
            string libs=Path.Combine(cocotb,"libs"),host=Path.Combine(sharp,"build/libsharptestbench_host.so");
            string cpp=Path.Combine(cocotb,"share/lib/verilator/verilator.cpp");
            string dotnet=FindDotnetRoot();
            string fxr=Directory.GetFiles(Path.Combine(dotnet,"host/fxr"),"libhostfxr.so",SearchOption.AllDirectories)
                .OrderByDescending(path=>Version.Parse(new DirectoryInfo(Path.GetDirectoryName(path)!).Name.Split('-')[0])).First();
            string[] names=["ft_pkg","riscue_pipeline_pkg","riscue_rvc","riscue_jump_target","riscue_execute","riscue_muldiv","riscue_pipeline_core","riscue_axi_master_bridge","riscue_axi_core"];
            string[] sources=atomics
                ?[..names.Take(7).Select(name=>Path.Combine(riscue,"rtl",name+".sv")),Path.Combine(root,"sim/rtl/riscue_atomic.sv")]
                :[..names.Select(name=>Path.Combine(riscue,"rtl",name+".sv")),Path.Combine(riscue,"boards/kc705/uart_tx.sv"),Path.Combine(root,"sim/rtl/riscue_demo.sv")];
            foreach(string file in sources.Concat([elf,host,cpp,Path.Combine(libs,"libgpi.so"),Path.Combine(libs,"libgpi_impl_verilator_vpi.so"),Path.Combine(libs,"libcocotb_bootstrap.so")]))
                if(!File.Exists(file))throw new FileNotFoundException("Required local dependency is missing",file);
            string binary=Path.Combine(run,"firmware.bin");
            string bundledPrefix=Path.Combine(riscue,".tools/riscv/root/usr/bin/riscv64-unknown-elf-");
            string prefix=Environment.GetEnvironmentVariable("RISCV_TOOLCHAIN_PREFIX")??
                (File.Exists(bundledPrefix+"objcopy")?bundledPrefix:"riscv64-unknown-elf-");
            string statusAddress="";
            uint timerAddress=0;
            if(soak)
            {
                string symbols=await Run(prefix+"nm",["-S","--defined-only",elf],root,Path.Combine(run,"symbols.log"));
                string[] symbol=symbols.Split('\n').Select(line=>line.Split((char[]?)null,StringSplitOptions.RemoveEmptyEntries))
                    .Single(fields=>fields.Length==4&&fields[3]==(atomics?"nexrt_atomic_status":"nexrt_soak_status"));
                uint address=uint.Parse(symbol[0],NumberStyles.HexNumber,CultureInfo.InvariantCulture);
                uint size=uint.Parse(symbol[1],NumberStyles.HexNumber,CultureInfo.InvariantCulture);
                if(size!=(atomics?12:57)*4||address%4!=0||address>32768-size)throw new IOException("Unknown firmware status layout/address");
                statusAddress=symbol[0];
                string[] timerSymbol=symbols.Split('\n').Select(line=>line.Split((char[]?)null,StringSplitOptions.RemoveEmptyEntries))
                    .Single(fields=>fields.Length==4&&fields[3]==(atomics?"nexrt_atomic_clock_hz":"nexrt_soak_clock_hz"));
                if(uint.Parse(timerSymbol[1],NumberStyles.HexNumber,CultureInfo.InvariantCulture)!=4)throw new IOException("Invalid clock identity");
                timerAddress=uint.Parse(timerSymbol[0],NumberStyles.HexNumber,CultureInfo.InvariantCulture);
                File.WriteAllText(Path.Combine(run,"firmware.sha256"),Hash(elf)+"\n");
                if(atomics)
                {
                    string listing=await Run(prefix+"objdump",["-d",elf],root,Path.Combine(run,"disassembly.log"));
                    int count=0;
                    foreach(Match instruction in Regex.Matches(listing,@"(?m)^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s"))
                    {
                        uint opcode=uint.Parse(instruction.Groups[1].Value,NumberStyles.HexNumber,CultureInfo.InvariantCulture);
                        if((opcode&127)!=0x2f)continue;
                        uint operation=opcode>>27;
                        if(((opcode>>12)&7)!=2||((opcode>>25)&3)!=3||operation is not (0 or 1 or 4 or 8 or 12))
                            throw new IOException("Unsupported AMO/LR/SC in final ELF");
                        ++count;
                    }
                    if(enabled?count==0:count!=0)throw new IOException("Final ELF atomic instructions disagree with requested mode");
                    File.WriteAllText(Path.Combine(run,"amo-instructions.txt"),count.ToString(CultureInfo.InvariantCulture)+"\n");
                }
            }
            await Run(prefix+"objcopy",["-O","binary",elf,binary],root,Path.Combine(run,"objcopy.log"));
            byte[] bytes=File.ReadAllBytes(binary);
            if(bytes.Length==0||bytes.Length>0x7f00)throw new IOException("Firmware exceeds the wrapper RAM image region");
            if(soak&&(timerAddress>bytes.Length-4||BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan((int)timerAddress,4))!=100_000_000))
                throw new IOException("Soak ELF must be configured with RTOS_SOAK_CLOCK_HZ=100000000 for this RTL wrapper");
            byte[] memory=new byte[32768];bytes.CopyTo(memory,0);
            string hex=Path.Combine(run,"firmware.hex");
            File.WriteAllLines(hex,Enumerable.Range(0,8192).Select(i=>BinaryPrimitives.ReadUInt32LittleEndian(memory.AsSpan(i*4,4)).ToString("x8")));
            // Per-run managed output prevents a concurrent run from replacing
            // verification assemblies while this simulator is loading them.
            string managed=Path.Combine(run,"managed");
            await Run(Path.Combine(dotnet,"dotnet"),["build",Path.Combine(root,"sim/SharpDemo/SharpDemo.csproj"),"-c","Release","--output",managed,"-p:SharpTestBenchRoot="+sharp],root,Path.Combine(run,"managed-build.log"));
            // Large compiler intermediates belong on the Linux filesystem, not /mnt/c.
            string workspaceKey=Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(root)))[..16];
            string cache=Environment.GetEnvironmentVariable("NEXRT_SIM_CACHE")??
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),".cache/nexrt",workspaceKey);
            string model=Path.Combine(cache,atomics?(enabled?"atomic-model-zaamo":"atomic-model-baseline"):(soak?"soak-model":"model"));Directory.CreateDirectory(model);
            // Keep every generated/runtime C++ unit at O3. The cocotb driver rule
            // appends OPT_FAST after user flags, so CFLAGS alone can lose to -Os.
            string[] options=["--cc","--exe","--build","-j","2","--Mdir",model,"--top-module","riscue_demo","--prefix","Vtop","-o","riscue_demo","--vpi","--public-flat-rw","--timing","--timescale-override","1ns/1ns","-Wno-fatal","-CFLAGS","-std=c++20 -O3","-MAKEFLAGS","OPT_FAST=-O3 OPT_SLOW=-O3 OPT_GLOBAL=-O3","-LDFLAGS","-Wl,-rpath,"+libs+" -L"+libs+" -lcocotb_bootstrap -rdynamic",cpp,..sources];
            if(atomics)options=[..options,"-GZAAMO_ENABLED="+(enabled?"1":"0")];
            string verilator=Environment.GetEnvironmentVariable("VERILATOR")??"verilator";
            string version=await Run(verilator,["--version"],root,Path.Combine(run,"verilator-version.log"));
            string manifest=version+"\n"+string.Join('\n',options)+"\n"+string.Join('\n',sources.Append(cpp).Select(file=>file+" "+Hash(file)));
            string key=Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(manifest)));
            string executable=Path.Combine(model,"riscue_demo"),stamp=Path.Combine(model,"fingerprint.txt");
            if(!File.Exists(executable)||!File.Exists(stamp)||File.ReadAllText(stamp)!=key)
            {
                await Run(verilator,options,root,Path.Combine(run,"model-build.log"));
                File.WriteAllText(stamp,key);
            }
            string runtimeRoot=Path.Combine(dotnet,"shared","Microsoft.NETCore.App");
            string[] runtimeFiles=Directory.GetFiles(runtimeRoot,"*",SearchOption.AllDirectories)
                .Where(file=>file.EndsWith(".dll",StringComparison.Ordinal)||file.EndsWith(".so",StringComparison.Ordinal)||
                    file.EndsWith(".json",StringComparison.Ordinal)||Path.GetFileName(file)==".version").ToArray();
            string[] managedFiles=Directory.GetFiles(managed).Where(file=>file.EndsWith(".dll",StringComparison.Ordinal)||file.EndsWith(".json",StringComparison.Ordinal)).ToArray();
            var identities=sources.Concat([elf,host,executable,cpp,fxr]).Concat(managedFiles)
                .Concat(Directory.GetFiles(libs,"*.so")).Concat(runtimeFiles)
                .Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToDictionary(file=>file,Hash,StringComparer.Ordinal);
            await Run(Path.Combine(dotnet,"dotnet"),["--info"],root,Path.Combine(run,"dotnet-info.log"));
            File.WriteAllText(Path.Combine(run,"manifest.txt"),(atomics
                ?$"PLAIN pipeline, RF_TMR=0, FI_ENABLE=0; native atomic-capable RAM; Zaamo={(enabled?1:0)}; no AXI/UART\n"
                :"PLAIN pipeline, RF_TMR=0, FI_ENABLE=0; upstream AXI bridge; plain RAM; accelerated TX UART\n")+
                $"CS_TEST_FILTER={(atomics?"Atomics":(soak?"Overnight":"ThreeWorkers"))}\nCS_SEED=1\nmode={mode}\nhostSeconds={hostSeconds}\nstatusAddress={statusAddress}\n"+
                string.Join('\n',identities.Select(pair=>pair.Key+" "+pair.Value))+"\n"+manifest);
            var environment=new Dictionary<string,string>
            {
                ["DOTNET_ROOT"]=dotnet,["CS_HOSTFXR"]=fxr,["CS_CONFIG"]=Path.Combine(managed,"SharpDemo.runtimeconfig.json"),
                ["CS_ASSEMBLY"]=Path.Combine(managed,"SharpDemo.dll"),["CS_ENTRY_TYPE"]="Entry, SharpDemo",["CS_TOPLEVEL"]="riscue_demo",
                ["CS_RESULTS"]=Path.Combine(run,"results.json"),["CS_TRACE"]=Path.Combine(run,"trace.txt"),["CS_BINDINGS"]=Path.Combine(run,"Bindings.cs"),
                ["CS_GPI_LIBRARY"]=Path.Combine(libs,"libgpi.so"),["NEXRT_UART_LOG"]=Path.Combine(run,"uart.log"),
                ["GPI_IMPL"]=Path.Combine(libs,"libgpi_impl_verilator_vpi.so")+":gpi_impl_verilator_vpi_entry",
                ["COCOTB_BOOTSTRAP"]=Path.Combine(libs,"libgpi.so")+",gpi_initialize:"+host+",sharptestbench_initialize",
                ["LD_LIBRARY_PATH"]=libs+":"+Path.GetDirectoryName(host)+":"+(Environment.GetEnvironmentVariable("LD_LIBRARY_PATH")??""),
                ["CS_TEST_FILTER"]=atomics?"Atomics":(soak?"Overnight":"ThreeWorkers"),["CS_SEED"]="1"
            };
            if(soak)
            {
                environment["NEXRT_SOAK_ADDRESS"]=statusAddress;
                environment["NEXRT_SOAK_HOST_SECONDS"]=hostSeconds.ToString(CultureInfo.InvariantCulture);
                environment["NEXRT_SOAK_MODE"]=mode=="--smoke"?"smoke":"soak";
                environment["NEXRT_SOAK_RUN"]=run;
                if(atomics)environment["NEXRT_ATOMIC_ENABLED"]=enabled?"1":"0";
            }
            if(mode=="--prepare-soak"){Console.WriteLine("Prepared; no simulation launched. Evidence: "+run);return 0;}
            await Run(executable,["+firmware="+hex],run,Path.Combine(run,"simulation.log"),environment,
                soak?TimeSpan.FromSeconds(hostSeconds+30):null,soak?Path.Combine(run,"simulator.pid"):null);
            foreach(var identity in identities)
                if(Hash(identity.Key)!=identity.Value)throw new IOException("Simulation dependency changed during execution: "+identity.Key);
            using var results=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"results.json")));
            if(results.RootElement.GetArrayLength()!=1||results.RootElement[0].GetProperty("Status").GetString()!="passed")throw new IOException("SharpTestBench did not report exactly one passing case");
            if(soak)
            {
                using var status=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"status.json")));
                string outcome=status.RootElement.GetProperty("outcome").GetString()!;
                if(outcome is not ("completed" or "smoke_passed" or "wall_limit_reached"))throw new IOException("Invalid soak outcome: "+outcome);
                File.WriteAllText(Path.Combine(build,"latest-"+outcome+".txt"),run+"\n");
                Console.WriteLine((atomics?"Atomic test outcome: ":"Soak outcome: ")+outcome+"; evidence: "+run);return 0;
            }
            File.WriteAllText(Path.Combine(build,"latest-run.txt"),run+"\n");
            Console.WriteLine("UART transcript: "+Path.Combine(run,"uart.log"));
            Console.WriteLine("Evidence: "+run);return 0;
        }
        catch(Exception error){if(evidence!=null)File.WriteAllText(Path.Combine(evidence,"launcher-failure.txt"),error.ToString());Console.Error.WriteLine(error.Message);return 1;}
    }

    /// <summary>Resolves .NET hosting files from an override or the real executable on PATH.</summary>
    private static string FindDotnetRoot()
    {
        string? configured=Environment.GetEnvironmentVariable("DOTNET_ROOT");
        if(!string.IsNullOrWhiteSpace(configured))return configured;
        foreach(string folder in (Environment.GetEnvironmentVariable("PATH")??"").Split(Path.PathSeparator))
        {
            string candidate=Path.Combine(folder,"dotnet");
            if(File.Exists(candidate))return Path.GetDirectoryName(new FileInfo(candidate).ResolveLinkTarget(true)?.FullName??Path.GetFullPath(candidate))!;
        }
        throw new FileNotFoundException("Install .NET 10 or set DOTNET_ROOT");
    }

    /// <summary>Identifies exact source and executable bytes in the preserved run manifest.</summary>
    private static string Hash(string path)=>Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)));

    /// <summary>Runs arguments without a shell, streams both channels, and enforces a wall-clock limit.</summary>
    private static async Task<string> Run(string executable,string[] arguments,string directory,string log,Dictionary<string,string>? environment=null,TimeSpan? duration=null,string? pidFile=null)
    {
        Console.WriteLine("Running "+executable+" "+string.Join(' ',arguments));
        var start=new ProcessStartInfo(executable){WorkingDirectory=directory,UseShellExecute=false,RedirectStandardOutput=true,RedirectStandardError=true,CreateNoWindow=true};
        foreach(string argument in arguments)start.ArgumentList.Add(argument);
        if(environment!=null)foreach(var pair in environment)start.Environment[pair.Key]=pair.Value;
        var result=await HostProcess.Run(start,duration??TimeSpan.FromMinutes(15),onStarted:pid=>
        {if(pidFile!=null)File.WriteAllText(pidFile,pid.ToString(CultureInfo.InvariantCulture));});
        File.WriteAllText(log,result.Output);
        if(result.TimedOut)throw new TimeoutException("Execution/output wall-clock timeout: "+executable);
        if(result.OutputLimit||result.IoError is not null)throw new IOException("Output failure: "+executable+"; see "+log);
        if(result.ExitCode!=0)throw new IOException($"{executable} exited {result.ExitCode}; see {log}");
        return result.Output;
    }
}
