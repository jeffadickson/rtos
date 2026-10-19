using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
namespace NexRT.Configure;
/// <summary>A researched CPU/board profile; physical CPU capacity does not imply RTOS SMP support.</summary>
internal sealed record Target(string Id,string Architecture,string Cpu,string Status,int PhysicalCpuLimit,bool FirmwareReady,string[] Sources);
/// <summary>A bounded static feature set, shared by every target.</summary>
internal sealed record Profile(string Id,int MaxThreads,int TickHz,int IdleStackBytes,bool Metrics,bool StackGuard,bool RoundRobin,bool Ethernet,bool Web=false,bool Storage=false,bool Sync=false,bool Logging=false);
/// <summary>Explicit target and profile catalogue.</summary>
internal sealed record Catalogue(int SchemaVersion,Target[] Targets,Profile[] Profiles);
/// <summary>Generates deterministic CMake initial caches and checks configuration contracts.</summary>
internal static class Program
{
    /// <summary>Lists targets, generates a configuration, or executes configuration contract tests.</summary>
    public static int Main(string[] args)
    {
        try
        {
            if(args.Length<2) throw new ArgumentException("Usage: list ROOT | self-test ROOT | cmake-test ROOT | board-check ROOT JSON | generate ROOT TARGET PROFILE OUTPUT [CPU_COUNT] [up|amp|smp]");
            var path=Path.Combine(Path.GetFullPath(args[1]),"config/system.json");
            if(new FileInfo(path).Length>1024*1024) throw new InvalidDataException("Catalogue exceeds 1 MiB.");
            var catalogue=JsonSerializer.Deserialize(File.ReadAllText(path), ConfigurationJsonContext.Default.Catalogue) ?? throw new InvalidDataException("Null catalogue.");
            Validate(catalogue);
            switch(args[0])
            {
                case "list" when args.Length==2:
                    foreach(var target in catalogue.Targets) Console.WriteLine($"{target.Id,-25} {target.Cpu,-22} {target.Status}; firmware={(target.FirmwareReady?"cross-built, target-unverified":"planned")}");
                    Console.WriteLine("Profiles: "+string.Join(", ",catalogue.Profiles.Select(p=>p.Id))); break;
                case "self-test" when args.Length==2: SelfTest(catalogue); BoardContracts.SelfTest(); break;
                case "board-check" when args.Length==3: BoardContracts.Check(Path.GetFullPath(args[2])); break;
                case "cmake-test" when args.Length==2: ConfigurationChecks.Run(Path.GetFullPath(args[1])); break;
                case "generate" when args.Length is >=5 and <=7:
                    var selected=catalogue.Targets.SingleOrDefault(t=>t.Id==args[2]) ?? throw new ArgumentException("Unknown target.");
                    var profile=catalogue.Profiles.SingleOrDefault(p=>p.Id==args[3]) ?? throw new ArgumentException("Unknown profile.");
                    int cpus=args.Length>=6?int.Parse(args[5],CultureInfo.InvariantCulture):1;
                    string mode=args.Length>=7?args[6]:"up";
                    var content=Render(selected,profile,cpus,mode);
                    var output=Path.GetFullPath(args[4]); Directory.CreateDirectory(Path.GetDirectoryName(output)!);
                    File.WriteAllText(output+".tmp",content); File.Move(output+".tmp",output,true);
                    Console.WriteLine($"Generated {output}. Target status: {selected.Status}. Planned ports deliberately fail firmware configure."); break;
                default: throw new ArgumentException("Unknown command or argument count.");
            }
            return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
    }
    /// <summary>Checks identifiers, sizes, unique identities and allowed readiness states before generating CMake.</summary>
    private static void Validate(Catalogue c)
    {
        bool Id(string value)=>!string.IsNullOrEmpty(value)&&value.Length<=64&&value.All(ch=>char.IsAsciiLetterOrDigit(ch)||ch=='-');
        if(c.SchemaVersion!=1||c.Targets.Length is <1 or >64||c.Profiles.Length is <1 or >32) throw new InvalidDataException("Unsupported schema/count.");
        if(c.Targets.Select(t=>t.Id).Distinct().Count()!=c.Targets.Length||c.Profiles.Select(p=>p.Id).Distinct().Count()!=c.Profiles.Length) throw new InvalidDataException("Duplicate identity.");
        foreach(var t in c.Targets)
            if(!Id(t.Id)||t.PhysicalCpuLimit is <1 or >64||t.Status is not("planned" or "provisional" or "cross-built")||t.FirmwareReady!=(t.Id=="riscue"&&t.Status=="cross-built")) throw new InvalidDataException("Invalid target/readiness.");
        foreach(var p in c.Profiles)
            if(!Id(p.Id)||p.MaxThreads is <1 or >255||p.TickHz is <1 or >1000000||p.IdleStackBytes is <512 or >1048576||p.IdleStackBytes%16!=0||p.Logging&&!p.Sync) throw new InvalidDataException("Invalid profile capacity or logging dependency.");
    }
    /// <summary>Renders static capacities and features; rejects multiprocessing until a qualified implementation exists.</summary>
    private static string Render(Target t,Profile p,int cpus,string mode)
    {
        if(cpus<1||cpus>t.PhysicalCpuLimit) throw new ArgumentException("CPU count exceeds selected hardware profile.");
        if(mode!="up"||cpus!=1) throw new ArgumentException("AMP/SMP is planned; current kernel accepts only up with one CPU.");
        if(p.Logging&&!p.Sync) throw new ArgumentException("Logging requires synchronization.");
        string On(bool value)=>value?"ON":"OFF";
        return $"# Generated by NexRT.Configure. Edit config/system.json or select another profile.\n# Target status: {t.Status}; hardware execution is unverified.\n"+
            $"set(RTOS_TARGET_ID {t.Id} CACHE STRING \"Target\" FORCE)\nset(RTOS_BUILD_FIRMWARE ON CACHE BOOL \"Firmware\" FORCE)\n"+
            $"set(RTOS_CPU_COUNT {cpus} CACHE STRING \"CPUs\" FORCE)\nset(RTOS_MULTIPROCESSING {mode} CACHE STRING \"Mode\" FORCE)\n"+
            $"set(RTOS_MAX_THREADS {p.MaxThreads} CACHE STRING \"Thread capacity\" FORCE)\nset(RTOS_TICK_HZ {p.TickHz} CACHE STRING \"Tick rate\" FORCE)\nset(RTOS_IDLE_STACK_BYTES {p.IdleStackBytes} CACHE STRING \"Idle stack\" FORCE)\n"+
            $"set(RTOS_METRICS {On(p.Metrics)} CACHE BOOL \"Metrics\" FORCE)\nset(RTOS_STACK_GUARD {On(p.StackGuard)} CACHE BOOL \"Guards\" FORCE)\nset(RTOS_ROUND_ROBIN {On(p.RoundRobin)} CACHE BOOL \"Slicing\" FORCE)\nset(RTOS_ETHERNET {On(p.Ethernet)} CACHE BOOL \"Ethernet adapter\" FORCE)\n"+
            $"set(RTOS_WEB {On(p.Web)} CACHE BOOL \"HTTP component\" FORCE)\nset(RTOS_STORAGE {On(p.Storage)} CACHE BOOL \"Flash storage\" FORCE)\nset(RTOS_SYNC {On(p.Sync)} CACHE BOOL \"Synchronization\" FORCE)\nset(RTOS_LOGGING {On(p.Logging)} CACHE BOOL \"Queued logging\" FORCE)\n";
    }
    /// <summary>Exercises invalid topology, capacities and readiness, plus deterministic rendering and feature-off configuration.</summary>
    private static void SelfTest(Catalogue c)
    {
        void Reject(Action operation){try{operation();}catch(ArgumentException){return;}catch(InvalidDataException){return;}throw new InvalidOperationException("Invalid configuration accepted.");}
        var t=c.Targets.Single(t=>t.Id=="riscue"); var p=c.Profiles.Single(p=>p.Id=="minimal");
        Reject(()=>Render(t,p,2,"smp")); Reject(()=>Render(t,p,1,"amp")); Reject(()=>Render(t,p,0,"up")); Reject(()=>Render(t,p,1,"bogus"));
        Reject(()=>Validate(c with{Profiles=[p with{MaxThreads=0}]}));
        Reject(()=>Validate(c with{Profiles=[p with{IdleStackBytes=513}]}));
        Reject(()=>Validate(c with{Targets=[t with{Id="bad\ncommand"}]}));
        Reject(()=>Validate(c with{Targets=[t with{Id="fake",FirmwareReady=true}]}));
        Reject(()=>Render(t,p with{Logging=true,Sync=false},1,"up"));
        Reject(()=>Validate(c with{Profiles=[p with{Logging=true,Sync=false}]}));
        var logging=Render(t,p with{Logging=true,Sync=true},1,"up");
        if(!logging.Contains("set(RTOS_LOGGING ON")||!logging.Contains("set(RTOS_SYNC ON")) throw new InvalidOperationException("Logging flags missing.");
        var first=Render(t,p,1,"up");
        if(first!=Render(t,p,1,"up")||!first.Contains("set(RTOS_ETHERNET OFF")||!first.Contains("set(RTOS_METRICS OFF")) throw new InvalidOperationException("Non-deterministic or incorrect minimal output.");
        foreach(var target in c.Targets) _=Render(target,p,1,"up");
        Console.WriteLine("PASS: topology, capacity, injection, readiness, deterministic generation and all planned target configurations.");
    }
}
