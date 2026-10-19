using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
namespace NexRT.Workbench;
/// <summary>An immutable source snapshot queued for a local background commit.</summary>
internal sealed record CommitJob(string Label,string[] Paths);
/// <summary>A substantive integration change with a unique source anchor.</summary>
internal sealed record Change(int Iteration,string Title,string Path,string Anchor,string Summary,string Sha256);
/// <summary>Recorded execution at a requested integration boundary.</summary>
internal sealed record Gate(string Command,string[] Arguments,int ExitCode,string Log,string Sha256);
/// <summary>Measured allocated ELF sections, including code resident in RISCue RAM.</summary>
internal sealed record Footprint(long CodeAndReadOnly,long InitializedData,long ZeroFill,long AllocatedSpan,long Padding,long BootReserve,long CommittedRam,long ImageBytes,string ElfSha256);
/// <summary>A checkpoint report distinguishes integration changes from runtime qualification.</summary>
internal sealed record Checkpoint(int Iteration,string Result,string Scope,Gate[] Gates);
/// <summary>Native-safe schemas for workbench state and reports.</summary>
[JsonSourceGenerationOptions(PropertyNamingPolicy=JsonKnownNamingPolicy.CamelCase,WriteIndented=true)]
[JsonSerializable(typeof(CommitJob))]
[JsonSerializable(typeof(Change[]),TypeInfoPropertyName="Changes")]
[JsonSerializable(typeof(Footprint))]
[JsonSerializable(typeof(Checkpoint))]
internal partial class WorkJson : JsonSerializerContext { }
/// <summary>Builds content, records integration, measures firmware and queues local commits without shell interpolation.</summary>
internal static class Program
{
    private static string Root="";
    /// <summary>Runs one workbench command. Commit work is detached and serialized by its worker.</summary>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if(args.Length<2)throw new ArgumentException("Command ROOT [arguments]: init, start-worker, queue LABEL, finish, record PSV, checkpoint N, footprint ELF, site, assets, serve PORT");
            Root=Path.GetFullPath(args[1]);Directory.SetCurrentDirectory(Root);
            switch(args[0])
            {
                case "init":
                    if(!Directory.Exists(".git")){await Run("git",["init","-b","main"]);await Run("git",["config","user.name","Codex"]);await Run("git",["config","user.email","codex@local"]);} break;
                case "start-worker": StartWorker();break;
                case "worker": await Worker();break;
                case "queue": await Queue(args[2]);break;
                case "finish": Directory.CreateDirectory("build/commit-queue");File.WriteAllText("build/commit-queue/finish","");break;
                case "record": Record(args[2]);break;
                case "checkpoint": await CheckpointRun(int.Parse(args[2]));break;
                case "footprint": Measure(args[2]);break;
                case "site": Site();break;
                case "assets": Assets();break;
                case "serve": await Serve(int.Parse(args[2]));break;
                default: throw new ArgumentException("Unknown command.");
            }
            return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
    }
    /// <summary>Computes a content digest, independent of timestamps.</summary>
    private static string Hash(string path)=>Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)));
    /// <summary>Runs a bounded child process and captures both streams without invoking a shell.</summary>
    private static async Task<string> Run(string command,string[] args,string? input=null,bool allowFailure=false,string? index=null)
    {
        var start=new ProcessStartInfo(command){WorkingDirectory=Root,UseShellExecute=false,RedirectStandardOutput=true,RedirectStandardError=true,RedirectStandardInput=input!=null,CreateNoWindow=true};
        foreach(var arg in args)start.ArgumentList.Add(arg);if(index!=null)start.Environment["GIT_INDEX_FILE"]=index;
        using var process=Process.Start(start)??throw new IOException("Cannot start "+command);
        if(input!=null){await process.StandardInput.WriteAsync(input);process.StandardInput.Close();}
        var stdout=process.StandardOutput.ReadToEndAsync();var stderr=process.StandardError.ReadToEndAsync();
        using var timeout=new CancellationTokenSource(TimeSpan.FromMinutes(20));
        try{await process.WaitForExitAsync(timeout.Token);}catch{process.Kill(true);throw;}
        var text=await stdout+await stderr;if(text.Length>8*1024*1024)throw new IOException("Child output limit exceeded.");
        LastExit=process.ExitCode;if(!allowFailure&&LastExit!=0)throw new IOException(command+" failed: "+text);
        return text;
    }
    private static int LastExit;
    /// <summary>Detaches the native commit worker; the caller never waits for commits.</summary>
    private static void StartWorker()
    {
        Directory.CreateDirectory("build/commit-queue");
        var start=new ProcessStartInfo(OperatingSystem.IsWindows()?Environment.ProcessPath!:"/usr/bin/setsid"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,WorkingDirectory=Root};
        if(!OperatingSystem.IsWindows())start.ArgumentList.Add(Environment.ProcessPath!);
        start.ArgumentList.Add("worker");start.ArgumentList.Add(Root);
        using var worker=Process.Start(start)??throw new IOException("Cannot start worker");
        File.WriteAllText("build/commit-queue/worker.pid",worker.Id.ToString());Console.WriteLine("Background commit worker started; PID "+worker.Id);
    }
    /// <summary>Saves bytes before queuing, so future source edits cannot enter an earlier commit.</summary>
    private static async Task Queue(string label)
    {
        var paths=(await Run("git",["ls-files","--cached","--others","--exclude-standard","-z"])).Split('\0',StringSplitOptions.RemoveEmptyEntries).Distinct().Order(StringComparer.Ordinal).ToArray();
        var directory=Path.Combine(Root,"build/commit-queue",DateTime.UtcNow.Ticks.ToString());Directory.CreateDirectory(directory);
        long total=0;
        foreach(var relative in paths)
        {
            if(Path.IsPathRooted(relative)||relative.Split('/').Contains("..")||relative.StartsWith("build/",StringComparison.Ordinal))throw new IOException("Unsafe snapshot path.");
            if(!File.Exists(relative))continue;
            if((File.GetAttributes(relative)&FileAttributes.ReparsePoint)!=0)throw new IOException("Snapshot refuses links.");
            var bytes=File.ReadAllBytes(relative);total+=bytes.Length;if(total>128*1024*1024)throw new IOException("Snapshot too large.");
            var destination=Path.Combine(directory,"files",relative);Directory.CreateDirectory(Path.GetDirectoryName(destination)!);File.WriteAllBytes(destination,bytes);
        }
        paths=paths.Where(p=>File.Exists(Path.Combine(directory,"files",p))).ToArray();
        File.WriteAllText(Path.Combine(directory,"job.json"),JsonSerializer.Serialize(new CommitJob(label,paths),WorkJson.Default.CommitJob));
        Console.WriteLine("Queued local snapshot: "+label+"; commit runs in background.");
    }
    /// <summary>Creates Git trees from snapshots using a private index; only this worker updates local HEAD.</summary>
    private static async Task Worker()
    {
        using var workerLock=new FileStream("build/commit-queue/worker.lock",FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None);
        var log=Path.Combine(Root,"build/commit-queue/worker.log");
        for(;;)
        {
            bool work=false;
            foreach(var directory in Directory.GetDirectories("build/commit-queue").Order(StringComparer.Ordinal))
            {
                var jobPath=Path.Combine(directory,"job.json");if(!File.Exists(jobPath)||File.Exists(Path.Combine(directory,"done"))||File.Exists(Path.Combine(directory,"failed")))continue;
                work=true;
                try
                {
                    var job=JsonSerializer.Deserialize(File.ReadAllText(jobPath),WorkJson.Default.CommitJob)??throw new IOException("Null job");
                    var index=Path.Combine(directory,"index");await Run("git",["read-tree","--empty"],index:index);
                    foreach(var path in job.Paths)
                    {
                        string hash=(await Run("git",["hash-object","-w","--no-filters",Path.Combine(directory,"files",path)])).Trim();
                        await Run("git",["update-index","--add","--cacheinfo","100644",hash,path],index:index);
                    }
                    string tree=(await Run("git",["write-tree"],index:index)).Trim();
                    string parent=(await Run("git",["rev-parse","--verify","HEAD"],allowFailure:true)).Trim();bool hasParent=LastExit==0;
                    string commit=(await Run("git",hasParent?["commit-tree",tree,"-p",parent]:["commit-tree",tree],job.Label+"\n")).Trim();
                    await Run("git",["update-ref","HEAD",commit,hasParent?parent:new string('0',40)]);
                    await Run("git",["read-tree",commit]);
                    File.WriteAllText(Path.Combine(directory,"done"),commit);File.AppendAllText(log,job.Label+" "+commit+"\n");
                }
                catch(Exception error){File.WriteAllText(Path.Combine(directory,"failed"),error.ToString());File.AppendAllText(log,"FAILED "+error.Message+"\n");}
            }
            if(!work&&File.Exists("build/commit-queue/finish"))return;
            await Task.Delay(300);
        }
    }
    /// <summary>Records exactly25 unique source-backed integration changes at each boundary.</summary>
    private static void Record(string path)
    {
        Directory.CreateDirectory("integration");var previous=File.Exists("integration/changes.json")?JsonSerializer.Deserialize(File.ReadAllText("integration/changes.json"),WorkJson.Default.Changes)!:[];
        var added=new List<Change>();
        foreach(var line in File.ReadAllLines(path).Where(s=>!string.IsNullOrWhiteSpace(s)))
        {
            var f=line.Split('|');if(f.Length!=5)throw new IOException("Expected ID|title|path|anchor|summary");int id=int.Parse(f[0]);
            if(id!=previous.Length+added.Count+1||!File.Exists(f[2])||!File.ReadAllText(f[2]).Contains(f[3],StringComparison.Ordinal)||f[1].Length<8||f[4].Length<20)throw new IOException("Incomplete integration item "+f[0]);
            added.Add(new(id,f[1],f[2],f[3],f[4],Hash(f[2])));
        }
        if(added.Count!=25)throw new IOException("A checkpoint requires25 substantive items.");var all=previous.Concat(added).ToArray();
        if(all.Select(c=>c.Title).Distinct().Count()!=all.Length||all.Select(c=>c.Path+"#"+c.Anchor).Distinct().Count()!=all.Length)throw new IOException("Duplicate integration item.");
        File.WriteAllText("integration/changes.json",JsonSerializer.Serialize(all,WorkJson.Default.Changes)+"\n");
        var rst=new StringBuilder("Integration history\n===================\n\nThese are individual integration changes, grouped into five25-item batches.\nThey are not125 complete kernel implementations or human reviews.\n\n");
        foreach(var item in all)rst.AppendLine($"{item.Iteration}. **{item.Title}** — {item.Summary} Source: ``{item.Path}``, anchor ``{item.Anchor}``.\n");
        File.WriteAllText("docs/integration-history.rst",rst.ToString());Console.WriteLine("Recorded "+all.Length+" source-backed integration items.");
    }
    /// <summary>Runs tests only at25-step boundaries and preserves separate attempt directories.</summary>
    private static async Task CheckpointRun(int number)
    {
        var changes=JsonSerializer.Deserialize(File.ReadAllText("integration/changes.json"),WorkJson.Default.Changes)!;
        if(number!=changes.Length||number%25!=0||number>125)throw new IOException("Invalid checkpoint boundary.");
        var directory=$"integration/checkpoints/{number:000}/{DateTime.UtcNow:yyyyMMdd-HHmmss}";Directory.CreateDirectory(directory);
        string tracker="build/host/native-tools/NexRT.Requirements/NexRT.Requirements";await Run(tracker,["generate",Root]);
        var commands=new (string Tool,string[] Args)[]{("cmake",["--build","--preset","host"]),("ctest",["--preset","host"]),("cmake",["--build","--preset","nano"]),("cmake",["--build","--preset","riscue"]),("cmake",["--build","--preset","riscue","--target","static_contract_check"]),("cmake",["--build","--preset","host","--target","static_analysis","docs"])};
        var gates=new List<Gate>();
        foreach(var (tool,args) in commands)
        {
            string output=await Run(tool,args,allowFailure:true);int code=LastExit;string log=$"{directory}/{gates.Count+1:00}.log";File.WriteAllText(log,output);gates.Add(new(tool,args,code,log,Hash(log)));Console.WriteLine($"Checkpoint{number}: {tool} {string.Join(' ',args)} => {code}");
            File.WriteAllText(directory+"/result.json",JsonSerializer.Serialize(new Checkpoint(number,code==0?"in-progress":"failed","Host/cross-build evidence only; no board execution",gates.ToArray()),WorkJson.Default.Checkpoint));
            if(code!=0)throw new IOException("Checkpoint failed; inspect "+log);
            if(args.Contains("nano")){Measure("build/nano/demo.elf");Site();Assets();await Run(tracker,["generate",Root]);}
        }
        File.WriteAllText(directory+"/result.json",JsonSerializer.Serialize(new Checkpoint(number,"passed","Host/cross-build evidence only; no board execution",gates.ToArray()),WorkJson.Default.Checkpoint));
    }
    /// <summary>Measures allocated32-bit little-endian ELF sections without including ELF/debug container bytes.</summary>
    private static void Measure(string path)
    {
        var b=File.ReadAllBytes(path);if(b.Length<52||b[0]!=127||b[1]!='E'||b[2]!='L'||b[3]!='F'||b[4]!=1||b[5]!=1)throw new IOException("Expected ELF32 little endian");
        uint U32(int p)=>BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(p,4));ushort U16(int p)=>BinaryPrimitives.ReadUInt16LittleEndian(b.AsSpan(p,2));
        int table=checked((int)U32(32)),entry=U16(46),count=U16(48);if(entry<40||count==0||table<0||(long)table+(long)entry*count>b.Length)throw new IOException("Bad ELF section table");
        long code=0,data=0,zero=0,low=long.MaxValue,high=0;
        for(int i=0;i<count;i++){int p=checked(table+i*entry);uint type=U32(p+4),flags=U32(p+8),address=U32(p+12),size=U32(p+20);if((flags&2)==0||size==0)continue;low=Math.Min(low,address);high=Math.Max(high,(long)address+size);if(type==8)zero+=size;else if((flags&1)!=0)data+=size;else code+=size;}
        if(low==long.MaxValue||low!=0||high>0x7f00-4096)throw new IOException("Unexpected RISCue allocation/map");
        var footprint=new Footprint(code,data,zero,high-low,high-low-code-data-zero,4096,high-low+4096,code+data,Hash(path));
        Directory.CreateDirectory("site");File.WriteAllText("site/footprint.json",JsonSerializer.Serialize(footprint,WorkJson.Default.Footprint)+"\n");
        File.WriteAllText("docs/footprint.rst",$"Measured minimal configuration\n==============================\n\nMeasured from ``{path}``; ELF SHA256 ``{footprint.ElfSha256}``.\n\n* Code and read-only data: **{code} bytes**.\n* Initialized writable data: **{data} bytes**.\n* BSS: **{zero} bytes**, including both512-byte thread/idle stacks.\n* Allocated RAM span: **{high-low} bytes**, including alignment.\n* Reserved boot/trap stack: **4096 bytes**.\n* Total committed RISCue RAM: **{footprint.CommittedRam} bytes**.\n* Loadable code and initialized data: **{footprint.ImageBytes} bytes**.\n\nRISCue executes code from RAM, so RAM includes code, not just data+BSS.\nThis is the one-thread nano example; web, Ethernet, storage, metrics, guards\nand synchronization are OFF. Host tools, HTML assets and debug sections are\nexcluded. These figures do not prove worst-case stack safety or board execution.\n");
        Console.WriteLine($"Measured image={footprint.ImageBytes}, allocated={footprint.AllocatedSpan}, committed RAM={footprint.CommittedRam} bytes.");
    }
    /// <summary>Builds a standalone HTML introduction from the measured report and authored template.</summary>
    private static void Site()
    {
        var f=JsonSerializer.Deserialize(File.ReadAllText("site/footprint.json"),WorkJson.Default.Footprint)!;var template=File.ReadAllText("site/introduction.template.html");
        File.WriteAllText("site/index.html",template.Replace("{{IMAGE}}",f.ImageBytes.ToString()).Replace("{{RAM}}",f.CommittedRam.ToString()).Replace("{{BSS}}",f.ZeroFill.ToString()).Replace("{{SHA}}",f.ElfSha256));
        var guide=File.ReadAllText("site/how-it-works.template.html")
            .Replace("{{IMAGE}}",f.ImageBytes.ToString()).Replace("{{RAM}}",f.CommittedRam.ToString()).Replace("{{SHA}}",f.ElfSha256)
            .Replace("{{MINIMAL_SOURCE}}",WebUtility.HtmlEncode(File.ReadAllText("examples/nano.cpp")))
            .Replace("{{STARTUP_SOURCE}}",WebUtility.HtmlEncode(File.ReadAllText("ports/riscue/start.S")))
            .Replace("{{ASM_SOURCE}}",WebUtility.HtmlEncode(File.ReadAllText("ports/riscue/context.S")))
            .Replace("{{PORT_SOURCE}}",WebUtility.HtmlEncode(File.ReadAllText("ports/riscue/rtos_port.hpp")));
        File.WriteAllText("site/how-it-works.html",guide);
    }
    /// <summary>Compiles HTML bytes into an immutable C++ asset with a recorded content digest.</summary>
    private static void Assets()
    {
        var bytes=File.ReadAllBytes("site/index.html");if(bytes.Length>65536)throw new IOException("HTML exceeds asset budget");Directory.CreateDirectory("generated");
        File.WriteAllText("generated/web_assets.hpp","#pragma once\n#include <stddef.h>\nnamespace rtos::assets {\ninline constexpr unsigned char introduction[]={"+string.Join(',',bytes.Select(b=>b.ToString()))+"};\ninline constexpr size_t introduction_size=sizeof(introduction);\n}\n");
        File.WriteAllText("site/assets.sha256",Hash("site/index.html")+"  index.html\n");
    }
    /// <summary>Serves the introduction locally; it is a host preview, not an Ethernet driver for the board.</summary>
    private static async Task Serve(int port)
    {
        if(port<1024||port>65535)throw new ArgumentException("Use a nonprivileged port");var listener=new TcpListener(IPAddress.Loopback,port);listener.Start();Console.WriteLine($"http://127.0.0.1:{port}/");
        for(;;){using var client=await listener.AcceptTcpClientAsync();client.ReceiveTimeout=5000;using var stream=client.GetStream();var request=new byte[1024];using var timeout=new CancellationTokenSource(5000);int length=await stream.ReadAsync(request,timeout.Token);string line=Encoding.ASCII.GetString(request,0,length);string path=line.StartsWith("GET /footprint.json ",StringComparison.Ordinal)?"site/footprint.json":line.StartsWith("GET /how-it-works.html ",StringComparison.Ordinal)?"site/how-it-works.html":"site/index.html";var body=File.ReadAllBytes(path);var header=Encoding.ASCII.GetBytes($"HTTP/1.0 200 OK\r\nContent-Type: {(path.EndsWith("json",StringComparison.Ordinal)?"application/json":"text/html; charset=utf-8")}\r\nContent-Length: {body.Length}\r\nConnection: close\r\n\r\n");await stream.WriteAsync(header);await stream.WriteAsync(body);}
    }
}
