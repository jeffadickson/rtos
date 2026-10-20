using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using NexRT.Host;

/// <summary>Collects bounded, hashed engineering evidence and verifies negative memory-analysis gates.</summary>
internal static class Program
{
    private const int MaximumFiles=512;
    private const long MaximumFileBytes=4*1024*1024,MaximumTotalBytes=32*1024*1024;
    private static readonly StringComparison Paths=OperatingSystem.IsWindows()?StringComparison.OrdinalIgnoreCase:StringComparison.Ordinal;

    /// <summary>Dispatches collect ROOT DEST INPUT..., verify DIRECTORY, self-test, or probe MODE ELF LOG [TOOL LIB].</summary>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if(args.Length==1&&args[0]=="self-test"){SelfTest();return 0;}
            if(args.Length==2&&args[0]=="verify"){Verify(Path.GetFullPath(args[1]));return 0;}
            if(args.Length>=4&&args[0]=="collect"){Collect(Path.GetFullPath(args[1]),args[2],args[3..]);return 0;}
            if(args.Length>=4&&args[0]=="probe"){await Probe(args[1..]);return 0;}
            throw new ArgumentException("collect ROOT DEST INPUT... | verify DIRECTORY | self-test | probe MODE ELF LOG [TOOL LIB]");
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
    }

    /// <summary>Resolves a relative path under a trusted root, rejecting traversal and manifest delimiters.</summary>
    private static string Within(string root,string relative)
    {
        if(Path.IsPathRooted(relative)||relative.IndexOfAny(['\r','\n','\t'])>=0)throw new IOException("Expected a relative evidence path");
        string result=Path.GetFullPath(Path.Combine(root,relative));
        if(!result.StartsWith(Path.TrimEndingDirectorySeparator(root)+Path.DirectorySeparatorChar,Paths))
            throw new IOException("Evidence path escapes its root");
        return result;
    }

    /// <summary>Rejects symbolic links and Windows reparse points before walking or copying.</summary>
    private static void NoLinks(string root,string path)
    {
        string current=root;
        if((File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0)throw new IOException("Linked evidence root");
        foreach(string part in Path.GetRelativePath(root,path).Split(Path.DirectorySeparatorChar))
        {
            current=Path.Combine(current,part);
            if((File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0)throw new IOException("Linked evidence input: "+path);
        }
    }

    /// <summary>Walks a shallow, bounded input tree without following links.</summary>
    private static List<string> Files(string root,IEnumerable<string> inputs)
    {
        var files=new HashSet<string>(OperatingSystem.IsWindows()?StringComparer.OrdinalIgnoreCase:StringComparer.Ordinal);
        int entries=0;
        void Visit(string path,int depth)
        {
            if(++entries>2048||depth>8)throw new IOException("Evidence tree exceeds traversal bound");
            NoLinks(root,path);
            if(Directory.Exists(path))foreach(string child in Directory.EnumerateFileSystemEntries(path))Visit(child,depth+1);
            else if(File.Exists(path))
            {
                files.Add(path);if(files.Count>MaximumFiles+1)throw new IOException("Too many evidence files");
            }
            else throw new FileNotFoundException("Missing evidence input",path);
        }
        foreach(string input in inputs)Visit(Within(root,input),0);
        if(files.Count==0)throw new IOException("No evidence files selected");
        return files.Order(StringComparer.Ordinal).ToList();
    }

    /// <summary>Hashes an existing file by streaming rather than retaining its entire contents.</summary>
    private static string Hash(string path)
    {
        using var input=File.OpenRead(path);using var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var buffer=new byte[65536];long bytes=0;int count;
        while((count=input.Read(buffer))!=0){if((bytes+=count)>MaximumFileBytes)throw new IOException("Evidence grew beyond file limit");hash.AppendData(buffer,0,count);}
        return Convert.ToHexStringLower(hash.GetHashAndReset());
    }

    /// <summary>Bounds actual copied bytes even if a selected file grows after the initial quota check.</summary>
    private static void Copy(string source,string destination,ref long total)
    {
        using var input=File.OpenRead(source);using var output=new FileStream(destination,FileMode.CreateNew,FileAccess.Write);
        var buffer=new byte[65536];long bytes=0;int count;
        while((count=input.Read(buffer))!=0)
        {
            if((bytes+=count)>MaximumFileBytes||(total+=count)>MaximumTotalBytes)throw new IOException("Evidence grew beyond copy limits");
            output.Write(buffer,0,count);
        }
    }

    /// <summary>Copies explicit inputs to a fresh staging directory and publishes only after verification.</summary>
    private static void Collect(string root,string destination,string[] inputs)
    {
        string target=Within(root,destination);
        if(File.Exists(target)||Directory.Exists(target))throw new IOException("Evidence destination already exists");
        var files=Files(root,inputs);long total=0;
        if(files.Count>MaximumFiles)throw new IOException("Too many selected evidence files");
        foreach(string file in files)
        {
            long bytes=new FileInfo(file).Length;
            if(bytes>MaximumFileBytes||(total+=bytes)>MaximumTotalBytes)throw new IOException("Evidence byte limit exceeded");
            if(file.StartsWith(target+Path.DirectorySeparatorChar,Paths))throw new IOException("Input overlaps destination");
        }
        string parent=Path.GetDirectoryName(target)!,existing=parent;
        while(!Directory.Exists(existing))existing=Path.GetDirectoryName(existing)??throw new IOException("Missing evidence parent root");
        NoLinks(root,existing); // Check existing ancestors before creating any destination directory.
        Directory.CreateDirectory(parent);NoLinks(root,parent);
        string stage=Within(root,Path.GetRelativePath(root,target)+".partial-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stage);
        try
        {
            var manifest=new StringBuilder();long copiedBytes=0;
            foreach(string source in files)
            {
                string relative="inputs/"+Path.GetRelativePath(root,source).Replace('\\','/');
                string copy=Within(stage,relative);Directory.CreateDirectory(Path.GetDirectoryName(copy)!);
                string before=Hash(source);Copy(source,copy,ref copiedBytes);
                if(before!=Hash(copy)||before!=Hash(source))throw new IOException("Input changed during collection: "+source);
                manifest.Append(before).Append('\t').Append(new FileInfo(copy).Length.ToString(CultureInfo.InvariantCulture)).Append('\t').Append(relative).Append('\n');
            }
            File.WriteAllText(Path.Combine(stage,"manifest.tsv"),manifest.ToString(),new UTF8Encoding(false));
            Verify(stage);Directory.Move(stage,target);
            Console.WriteLine($"EVIDENCE_COLLECTED files={files.Count} bytes={total} directory={target}");
        }
        catch
        {
            // stage was freshly created by this invocation and resolved under root.
            string checkedStage=Within(root,Path.GetRelativePath(root,stage));
            if(Directory.Exists(checkedStage))Directory.Delete(checkedStage,true);
            throw;
        }
    }

    /// <summary>Verifies every retained size/hash and rejects missing, duplicate or unlisted files.</summary>
    private static void Verify(string directory)
    {
        string manifest=Path.Combine(directory,"manifest.tsv");NoLinks(directory,manifest);
        if(new FileInfo(manifest).Length>1024*1024)throw new IOException("Oversized evidence manifest");
        var expected=new HashSet<string>(OperatingSystem.IsWindows()?StringComparer.OrdinalIgnoreCase:StringComparer.Ordinal);
        long total=0;
        foreach(string line in File.ReadLines(manifest))
        {
            string[] fields=line.Split('\t');
            if(fields.Length!=3||fields[0].Length!=64||!long.TryParse(fields[1],NumberStyles.None,CultureInfo.InvariantCulture,out long bytes)||bytes<0||bytes>MaximumFileBytes)
                throw new IOException("Malformed evidence manifest");
            string file=Within(directory,fields[2]);NoLinks(directory,file);
            if(!expected.Add(file)||expected.Count>MaximumFiles||(total+=bytes)>MaximumTotalBytes)throw new IOException("Evidence manifest bounds/duplicate failure");
            if(new FileInfo(file).Length!=bytes||!Hash(file).Equals(fields[0],StringComparison.Ordinal))throw new IOException("Evidence hash/size mismatch: "+fields[2]);
        }
        if(expected.Count==0)throw new IOException("Empty evidence manifest");
        var actual=Files(directory,Directory.EnumerateFileSystemEntries(directory).Select(path=>Path.GetRelativePath(directory,path)));
        if(actual.Count!=expected.Count+1||actual.Any(path=>path!=manifest&&!expected.Contains(path)))throw new IOException("Unlisted evidence file");
        Console.WriteLine("EVIDENCE_VERIFIED files="+expected.Count);
    }

    /// <summary>Exercises round-trip, tamper detection, overwrite rejection and traversal rejection in an owned temp root.</summary>
    private static void SelfTest()
    {
        string temporaryRoot=Path.GetFullPath(Path.GetTempPath());
        string root=Within(temporaryRoot,"nexrt-evidence-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        try
        {
            Directory.CreateDirectory(Path.Combine(root,"input"));File.WriteAllText(Path.Combine(root,"input","one.log"),"evidence\n");
            Collect(root,"bundle",["input"]);Verify(Path.Combine(root,"bundle"));
            void Reject(Action action){try{action();}catch(IOException){return;}throw new IOException("Negative evidence fixture was accepted");}
            Reject(()=>Collect(root,"bundle",["input"]));Reject(()=>Collect(root,"../escape",["input"]));
            File.AppendAllText(Path.Combine(root,"bundle","inputs","input","one.log"),"tampered");
            Reject(()=>Verify(Path.Combine(root,"bundle")));
            string copied=Path.Combine(root,"bundle","inputs","input","one.log");
            File.WriteAllText(copied,"evidence\n");
            string listing=Path.Combine(root,"bundle","manifest.tsv"),original=File.ReadAllText(listing);
            File.AppendAllText(listing,original);Reject(()=>Verify(Path.Combine(root,"bundle")));
            File.WriteAllText(listing,original);
            File.WriteAllText(Path.Combine(root,"bundle","unlisted.log"),"extra");Reject(()=>Verify(Path.Combine(root,"bundle")));
            string oversized=Path.Combine(root,"oversized.log");
            using(var file=File.Create(oversized))file.SetLength(MaximumFileBytes+1);
            Reject(()=>Collect(root,"oversized-bundle",["oversized.log"]));
            Console.WriteLine("PASS: evidence round-trip, tamper, overwrite and traversal rejection");
        }
        finally
        {
            // Exact owned GUID directory, checked against the resolved temporary root.
            Directory.Delete(Within(temporaryRoot,Path.GetRelativePath(temporaryRoot,root)),true);
        }
    }

    /// <summary>Requires a specific analyzer failure, retaining raw diagnostics rather than accepting arbitrary crashes.</summary>
    private static async Task Probe(string[] args)
    {
        string mode=args[0],elf=Path.GetFullPath(args[1]),log=Path.GetFullPath(args[2]);
        bool memcheck=mode=="valgrind";
        if(mode!="asan"&&mode!="ubsan"&&!memcheck)throw new ArgumentException("Unknown memory probe mode");
        var start=new ProcessStartInfo(memcheck?args[3]:elf);
        if(memcheck){start.ArgumentList.Add("--error-exitcode=99");start.ArgumentList.Add("--tool=memcheck");start.ArgumentList.Add(elf);if(args.Length>4&&args[4].Length!=0)start.Environment["VALGRIND_LIB"]=args[4];}
        start.ArgumentList.Add(mode=="ubsan"?"overflow":"uaf");
        var result=await HostProcess.Run(start,TimeSpan.FromSeconds(30),1024*1024);
        Directory.CreateDirectory(Path.GetDirectoryName(log)!);await File.WriteAllTextAsync(log,result.Output);
        bool diagnosed=mode switch{
            "asan"=>result.Output.Contains("AddressSanitizer",StringComparison.Ordinal)&&result.Output.Contains("heap-use-after-free",StringComparison.Ordinal),
            "ubsan"=>result.Output.Contains("runtime error:",StringComparison.Ordinal)&&result.Output.Contains("signed integer overflow",StringComparison.Ordinal),
            _=>result.ExitCode==99&&result.Output.Contains("Invalid read",StringComparison.Ordinal)&&result.Output.Contains("ERROR SUMMARY",StringComparison.Ordinal)};
        if(result.TimedOut||result.OutputLimit||result.IoError is not null||result.ExitCode==0||!diagnosed)throw new IOException("Memory analyzer failed its negative detection gate; see "+log);
        Console.WriteLine("PASS: "+mode+" rejected its controlled invalid-memory probe");
    }
}
