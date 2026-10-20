using System.Text.RegularExpressions;

namespace NexRT.Handoff;

/// <summary>Dependency profiles validate tool execution/version, native headers/linking and selected simulator files.</summary>
internal static partial class Program
{
    /// <summary>Returns an explicit option/environment override or a portable default.</summary>
    private static string Setting(Dictionary<string,string> options,string name,string environment,string fallback)=>
        options.GetValueOrDefault(name)??Environment.GetEnvironmentVariable(environment)??fallback;

    /// <summary>Reports every selected missing dependency; never installs software or changes the environment.</summary>
    private static async Task<int> Doctor(string root,string profiles,Dictionary<string,string> options)
    {
        var selected=(profiles=="all"?"host,docs,analysis,arm,riscv,rtl":profiles).Split(',').ToHashSet(StringComparer.Ordinal);
        string[] known=["host","docs","analysis","arm","riscv","rtl"];
        if(selected.Count==0||selected.Any(item=>!known.Contains(item)))throw new ArgumentException("Unknown dependency profile");
        var report=new List<string>();int failures=0;
        void Check(bool success,string name,string detail){string line=$"{(success?"PASS":"FAIL")} {name}: {detail}";report.Add(line);Console.WriteLine(line);if(!success)++failures;}
        async Task Tool(string name,string executable,string[] arguments,Version minimum)
        {
            try
            {
                var result=await Execute(executable,arguments);
                var match=Regex.Match(result.Output,@"(?<!\d)(\d+\.\d+(?:\.\d+)?)",RegexOptions.CultureInvariant,TimeSpan.FromSeconds(1));
                bool version=match.Success&&Version.TryParse(match.Value,out var found)&&found>=minimum;
                Check(result.ExitCode==0&&version,name,$"{executable}; minimum {minimum}; {result.Output.Split('\n')[0].Trim()}");
            }
            catch(Exception error){Check(false,name,executable+"; "+error.Message);}
        }
        void FileCheck(string name,string path)=>Check(File.Exists(path),name,path);
        string riscv=Setting(options,"riscv-prefix","RISCV_TOOLCHAIN_PREFIX","riscv64-unknown-elf-");
        string arm=Setting(options,"arm-prefix","ARM_TOOLCHAIN_PREFIX","arm-none-eabi-");
        if(selected.Contains("host"))
        {
            await Tool("Git","git",["--version"],new(2,0));
            await Tool("CMake","cmake",["--version"],new(3,22));
            await Tool("Ninja","ninja",["--version"],new(1,10));
            try{var sdk=await Execute("dotnet",["--list-sdks"]);Check(sdk.ExitCode==0&&sdk.Output.Split('\n').Any(line=>line.StartsWith("10.",StringComparison.Ordinal)),".NET 10 SDK",sdk.Output.Trim());}
            catch(Exception error){Check(false,".NET 10 SDK",error.Message);}
            if(OperatingSystem.IsWindows())
                await Tool("C++ compiler (use Developer Command Prompt, or prefer WSL)",Setting(options,"cxx","CXX","clang++"),["--version"],new(18,0));
            else
            {
                await Tool("C++ compiler",Setting(options,"cxx","CXX","c++"),["--version"],new(11,0));
                await Tool("Native AOT clang","clang",["--version"],new(14,0));
                string temp=Path.Combine(Path.GetTempPath(),"nexrt-dependency-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(temp);
                try
                {
                    string source=Path.Combine(temp,"probe.cpp"),binary=Path.Combine(temp,"probe");
                    File.WriteAllText(source,"#include <zlib.h>\nint main(){return zlibVersion()[0]?0:1;}\n");
                    var compile=await Execute("clang++",[source,"-lz","-o",binary]);
                    Check(compile.ExitCode==0,"Native AOT C++ linker and zlib development files",compile.ExitCode==0?"compile/link probe passed":compile.Output.Trim());
                    if(compile.ExitCode==0){var run=await Execute(binary,[]);Check(run.ExitCode==0,"Native probe execution","exit="+run.ExitCode);}
                }
                catch(Exception error){Check(false,"Native AOT link probe",error.Message);}
                finally{DeleteOwnedTemporary(temp);}
            }
        }
        if(selected.Contains("docs"))
        {
            string local=Path.Combine(root,"build","docs-venv",OperatingSystem.IsWindows()?"Scripts/sphinx-build.exe":"bin/sphinx-build");
            await Tool("Sphinx (project pin 8.2.3)",options.GetValueOrDefault("sphinx")??(File.Exists(local)?local:"sphinx-build"),["--version"],new(8,2,3));
        }
        if(selected.Contains("analysis"))
        {
            await Tool("Cppcheck",Setting(options,"cppcheck","CPPCHECK","cppcheck"),["--version"],new(2,13));
            await Tool("clang-tidy",Setting(options,"clang-tidy","CLANG_TIDY","clang-tidy"),["--version"],new(18,0));
        }
        if(selected.Contains("arm"))
        {
            foreach(string tool in new[]{"g++","gcc","nm","size"})await Tool("ARM "+tool,arm+tool,["--version"],new(2,0));
            await Tool("QEMU ARM",Setting(options,"qemu","QEMU_ARM","qemu-system-arm"),["--version"],new(8,2,2));
        }
        if(selected.Contains("riscv")||selected.Contains("rtl"))
            foreach(string tool in new[]{"g++","gcc","nm","size","objcopy"})await Tool("RISC-V "+tool,riscv+tool,["--version"],new(2,0));
        if(selected.Contains("rtl"))
        {
            Check(OperatingSystem.IsLinux(),"RTL host platform","native runner currently requires Linux/WSL");
            await Tool("Verilator",Setting(options,"verilator","VERILATOR","verilator"),["--version"],new(5,48));
            await Tool("Make","make",["--version"],new(4,0));
            string riscue=Setting(options,"riscue","RISCUE_SOURCE_DIR",Path.GetFullPath(Path.Combine(root,"../RISCue")));
            string sharp=Setting(options,"sharp","SHARPTESTBENCH_SOURCE_DIR",Path.GetFullPath(Path.Combine(root,"../SharpTestBench")));
            string cocotb=Setting(options,"cocotb","NEXRT_COCOTB_ROOT",Path.GetFullPath(Path.Combine(root,"../runtime/cocotb")));
            foreach(string name in new[]{"ft_pkg","riscue_pipeline_pkg","riscue_rvc","riscue_jump_target","riscue_execute","riscue_muldiv","riscue_pipeline_core","riscue_axi_master_bridge","riscue_axi_core"})FileCheck("RISCue RTL",Path.Combine(riscue,"rtl",name+".sv"));
            FileCheck("RISCue UART",Path.Combine(riscue,"boards/kc705/uart_tx.sv"));
            FileCheck("SharpTestBench project",Path.Combine(sharp,"src/SharpTestBench/SharpTestBench.csproj"));
            string host=Path.Combine(sharp,"build/libsharptestbench_host.so");FileCheck("SharpTestBench native host",host);
            FileCheck("cocotb Verilator adapter",Path.Combine(cocotb,"share/lib/verilator/verilator.cpp"));
            foreach(string name in new[]{"libgpi.so","libgpi_impl_verilator_vpi.so","libcocotb_bootstrap.so"})FileCheck("cocotb library",Path.Combine(cocotb,"libs",name));
            string dotnet=FindDotnetRoot();
            Check(Directory.Exists(Path.Combine(dotnet,"host/fxr"))&&Directory.Exists(Path.Combine(dotnet,"shared/Microsoft.NETCore.App")),".NET hosting layout",dotnet);
            if(OperatingSystem.IsLinux()&&File.Exists(host))
            {
                try{var linked=await Execute("ldd",[host]);Check(linked.ExitCode==0&&!linked.Output.Contains("not found",StringComparison.Ordinal),"native bridge shared dependencies",linked.Output.Trim());}
                catch(Exception error){Check(false,"native bridge shared dependencies",error.Message);}
            }
        }
        report.Add($"Failures={failures}; profile={profiles}; versions/probes are prerequisites, not qualification.");
        if(options.TryGetValue("report",out string? output)){Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);File.WriteAllLines(output,report);}
        Console.WriteLine(report[^1]);return failures==0?0:1;
    }

    /// <summary>Finds the installed dotnet root through an explicit override or the real executable on PATH.</summary>
    private static string FindDotnetRoot()
    {
        string? configured=Environment.GetEnvironmentVariable("DOTNET_ROOT");if(!string.IsNullOrWhiteSpace(configured))return configured;
        foreach(string folder in (Environment.GetEnvironmentVariable("PATH")??"").Split(Path.PathSeparator))
        {
            string executable=Path.Combine(folder,OperatingSystem.IsWindows()?"dotnet.exe":"dotnet");
            if(File.Exists(executable))return Path.GetDirectoryName(new FileInfo(executable).ResolveLinkTarget(true)?.FullName??Path.GetFullPath(executable))!;
        }
        return "dotnet-installation-not-found";
    }
}
