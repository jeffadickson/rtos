using System.Buffers.Binary;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using NexRT.Host;

namespace NexRT.Sim;

/// <summary>Native C# orchestration for a real RISCue/SharpTestBench UART simulation; no Python build glue.</summary>
internal static class Program
{
    /// <summary>Builds a checked model and test DLL, then preserves a fresh bounded simulation run.</summary>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if(args.Length!=4||!OperatingSystem.IsLinux())throw new ArgumentException("Usage on Linux/WSL: NexRT.Sim ROOT RISCUE_ROOT SHARPTESTBENCH_ROOT FIRMWARE_ELF");
            string root=Path.GetFullPath(args[0]),riscue=Path.GetFullPath(args[1]),sharp=Path.GetFullPath(args[2]);
            string elf=Path.GetFullPath(args[3],root),build=Path.Combine(root,"build/sharp-demo");
            string run=Path.Combine(build,"runs",DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff"));
            Directory.CreateDirectory(run);
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
            string[] sources=[..names.Select(name=>Path.Combine(riscue,"rtl",name+".sv")),Path.Combine(riscue,"boards/kc705/uart_tx.sv"),Path.Combine(root,"sim/rtl/riscue_demo.sv")];
            foreach(string file in sources.Concat([elf,host,cpp,Path.Combine(libs,"libgpi.so"),Path.Combine(libs,"libgpi_impl_verilator_vpi.so"),Path.Combine(libs,"libcocotb_bootstrap.so")]))
                if(!File.Exists(file))throw new FileNotFoundException("Required local dependency is missing",file);
            string binary=Path.Combine(run,"firmware.bin");
            string bundledPrefix=Path.Combine(riscue,".tools/riscv/root/usr/bin/riscv64-unknown-elf-");
            string prefix=Environment.GetEnvironmentVariable("RISCV_TOOLCHAIN_PREFIX")??
                (File.Exists(bundledPrefix+"objcopy")?bundledPrefix:"riscv64-unknown-elf-");
            await Run(prefix+"objcopy",["-O","binary",elf,binary],root,Path.Combine(run,"objcopy.log"));
            byte[] bytes=File.ReadAllBytes(binary);
            if(bytes.Length==0||bytes.Length>0x7f00)throw new IOException("Firmware exceeds the wrapper RAM image region");
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
            string model=Path.Combine(cache,"model");Directory.CreateDirectory(model);
            string[] options=["--cc","--exe","--build","-j","2","--Mdir",model,"--top-module","riscue_demo","--prefix","Vtop","-o","riscue_demo","--vpi","--public-flat-rw","--timing","--timescale-override","1ns/1ns","-Wno-fatal","-CFLAGS","-std=c++20 -O2","-LDFLAGS","-Wl,-rpath,"+libs+" -L"+libs+" -lcocotb_bootstrap -rdynamic",cpp,..sources];
            string version=await Run("verilator",["--version"],root,Path.Combine(run,"verilator-version.log"));
            string manifest=version+"\n"+string.Join('\n',options)+"\n"+string.Join('\n',sources.Append(cpp).Select(file=>file+" "+Hash(file)));
            string key=Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(manifest)));
            string executable=Path.Combine(model,"riscue_demo"),stamp=Path.Combine(model,"fingerprint.txt");
            if(!File.Exists(executable)||!File.Exists(stamp)||File.ReadAllText(stamp)!=key)
            {
                await Run("verilator",options,root,Path.Combine(run,"model-build.log"));
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
            File.WriteAllText(Path.Combine(run,"manifest.txt"),"PLAIN pipeline, RF_TMR=0, FI_ENABLE=0; upstream AXI bridge; plain RAM; accelerated TX UART\n"+
                "CS_TEST_FILTER=ThreeWorkers\nCS_SEED=1\n"+
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
                ["CS_TEST_FILTER"]="ThreeWorkers",["CS_SEED"]="1"
            };
            await Run(executable,["+firmware="+hex],run,Path.Combine(run,"simulation.log"),environment);
            foreach(var identity in identities)
                if(Hash(identity.Key)!=identity.Value)throw new IOException("Simulation dependency changed during execution: "+identity.Key);
            using var results=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"results.json")));
            if(results.RootElement.GetArrayLength()!=1||results.RootElement[0].GetProperty("Status").GetString()!="passed")throw new IOException("SharpTestBench did not report exactly one passing case");
            File.WriteAllText(Path.Combine(build,"latest-run.txt"),run+"\n");
            Console.WriteLine("UART transcript: "+Path.Combine(run,"uart.log"));
            Console.WriteLine("Evidence: "+run);return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
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
    private static async Task<string> Run(string executable,string[] arguments,string directory,string log,Dictionary<string,string>? environment=null)
    {
        Console.WriteLine("Running "+executable+" "+string.Join(' ',arguments));
        var start=new ProcessStartInfo(executable){WorkingDirectory=directory,UseShellExecute=false,RedirectStandardOutput=true,RedirectStandardError=true,CreateNoWindow=true};
        foreach(string argument in arguments)start.ArgumentList.Add(argument);
        if(environment!=null)foreach(var pair in environment)start.Environment[pair.Key]=pair.Value;
        var result=await HostProcess.Run(start,TimeSpan.FromMinutes(15));
        File.WriteAllText(log,result.Output);
        if(result.TimedOut)throw new TimeoutException("Execution/output wall-clock timeout: "+executable);
        if(result.OutputLimit||result.IoError is not null)throw new IOException("Output failure: "+executable+"; see "+log);
        if(result.ExitCode!=0)throw new IOException($"{executable} exited {result.ExitCode}; see {log}");
        return result.Output;
    }
}
