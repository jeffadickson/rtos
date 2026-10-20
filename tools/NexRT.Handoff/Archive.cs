using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

namespace NexRT.Handoff;

/// <summary>Snapshots reachable Git history, staging state, worktrees and selected portable runtime support.</summary>
internal static partial class Program
{
    private const long FileLimit=128L*1024*1024,TotalLimit=2L*1024*1024*1024;
    private const int EntryLimit=50000;
    private static readonly StringComparison PathComparison=OperatingSystem.IsWindows()?StringComparison.OrdinalIgnoreCase:StringComparison.Ordinal;

    /// <summary>Resolves a manifest path under a root without traversal or delimiter ambiguity.</summary>
    private static string Child(string root,string relative)
    {
        if(Path.IsPathRooted(relative)||relative.IndexOfAny(['\r','\n','\t','\0'])>=0)throw new IOException("Invalid archive-relative path");
        string path=Path.GetFullPath(Path.Combine(root,relative));
        if(!path.StartsWith(Path.TrimEndingDirectorySeparator(root)+Path.DirectorySeparatorChar,PathComparison))throw new IOException("Path escapes handoff root");
        return path;
    }

    /// <summary>Forbids worktree overlays/deletions from targeting Git's internal control directory.</summary>
    private static string RepoChild(string root,string relative)
    {
        if(relative.Replace('\\','/').Split('/').Any(part=>part.Equals(".git",StringComparison.OrdinalIgnoreCase)))throw new IOException("Worktree entry targets Git internals");
        return Child(root,relative);
    }

    /// <summary>Rejects source/restore links rather than following them outside the selected root.</summary>
    private static void NoLinks(string root,string path)
    {
        string cursor=root;
        if((File.GetAttributes(cursor)&FileAttributes.ReparsePoint)!=0)throw new IOException("Linked root is unsupported: "+root);
        foreach(string part in Path.GetRelativePath(root,path).Split(Path.DirectorySeparatorChar))
        {
            cursor=Path.Combine(cursor,part);
            if((File.GetAttributes(cursor)&FileAttributes.ReparsePoint)!=0)throw new IOException("Linked handoff entry: "+cursor);
        }
    }

    /// <summary>Hashes regular bounded inputs with fixed memory use.</summary>
    private static string Digest(string path,long limit=FileLimit)
    {
        using var stream=File.OpenRead(path);using var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var buffer=new byte[65536];long bytes=0;int count;
        while((count=stream.Read(buffer))!=0){if((bytes+=count)>limit)throw new IOException("Handoff file exceeds byte limit: "+path);hash.AppendData(buffer,0,count);}
        return Convert.ToHexStringLower(hash.GetHashAndReset());
    }

    /// <summary>Copies without replacing existing Unix permissions, preserving executable modes from Git clone.</summary>
    private static void Copy(string source,string destination)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        using var input=File.OpenRead(source);using var output=new FileStream(destination,FileMode.Create,FileAccess.Write);
        var buffer=new byte[65536];long bytes=0;int count;
        while((count=input.Read(buffer))!=0){if((bytes+=count)>FileLimit)throw new IOException("Growing handoff file exceeds limit");output.Write(buffer,0,count);}
    }

    /// <summary>Removes only a fresh, resolved GUID directory beneath the OS temporary root.</summary>
    private static void DeleteOwnedTemporary(string path)
    {
        string temporary=Path.GetFullPath(Path.GetTempPath());
        string owned=Child(temporary,Path.GetRelativePath(temporary,Path.GetFullPath(path)));
        if(!Path.GetFileName(owned).StartsWith("nexrt-",StringComparison.Ordinal))throw new IOException("Refusing unowned temporary cleanup");
        if(Directory.Exists(owned))
        {
            NoLinks(temporary,owned);
            // Git marks object files read-only on Windows. Clear that bit only
            // in this verified, owned temporary tree before removing the tree.
            if(OperatingSystem.IsWindows())foreach(string file in Tree(owned))
                File.SetAttributes(file,File.GetAttributes(file)&~FileAttributes.ReadOnly);
            Directory.Delete(owned,true);
        }
    }

    /// <summary>Enumerates a bounded tree while rejecting links before recursion.</summary>
    private static List<string> Tree(string root)
    {
        var result=new List<string>();int seen=0;
        void Walk(string directory,int depth)
        {
            if(depth>32)throw new IOException("Handoff nesting limit exceeded");
            foreach(string path in Directory.EnumerateFileSystemEntries(directory))
            {
                if(++seen>EntryLimit)throw new IOException("Handoff entry limit exceeded");NoLinks(root,path);
                if(Directory.Exists(path))Walk(path,depth+1);else result.Add(path);
            }
        }
        Walk(root,0);return result.Order(StringComparer.Ordinal).ToList();
    }

    /// <summary>Records exact sizes/hashes for all payloads; the manifest itself is deliberately not self-hashed.</summary>
    private static void Manifest(string root)
    {
        var lines=new List<string>();long total=0;
        foreach(string file in Tree(root))
        {
            long size=new FileInfo(file).Length;if((total+=size)>TotalLimit)throw new IOException("Handoff exceeds 2 GiB");
            string relative=Path.GetRelativePath(root,file).Replace('\\','/');_ =Child(root,relative);
            lines.Add(Digest(file)+"\t"+size+"\t"+relative);
        }
        File.WriteAllLines(Path.Combine(root,"MANIFEST.tsv"),lines,new UTF8Encoding(false));
    }

    /// <summary>Checks payload completeness, containment and integrity before any restore operation.</summary>
    private static void Verify(string root)
    {
        string manifest=Path.Combine(root,"MANIFEST.tsv");NoLinks(root,manifest);
        if(new FileInfo(manifest).Length>8*1024*1024)throw new IOException("Oversized handoff manifest");
        var expected=new HashSet<string>(OperatingSystem.IsWindows()?StringComparer.OrdinalIgnoreCase:StringComparer.Ordinal);long total=0;
        foreach(string line in File.ReadLines(manifest))
        {
            var fields=line.Split('\t');
            if(fields.Length!=3||fields[0].Length!=64||!long.TryParse(fields[1],out long size)||size<0||size>FileLimit)throw new IOException("Malformed handoff manifest");
            string file=Child(root,fields[2]);NoLinks(root,file);
            if(!expected.Add(file)||expected.Count>EntryLimit||(total+=size)>TotalLimit)throw new IOException("Duplicate/oversized handoff");
            if(new FileInfo(file).Length!=size||Digest(file)!=fields[0])throw new IOException("Handoff hash mismatch: "+fields[2]);
        }
        var actual=Tree(root);
        if(expected.Count==0||actual.Count!=expected.Count+1||actual.Any(file=>file!=manifest&&!expected.Contains(file)))throw new IOException("Missing or unlisted handoff entry");
        Console.WriteLine($"HANDOFF_VERIFIED files={expected.Count} bytes={total}");
    }

    /// <summary>Strips HTTP credentials/query data; source credentials and Git configuration are never packaged.</summary>
    private static string Remote(string value)
    {
        value=value.Trim();
        if(Uri.TryCreate(value,UriKind.Absolute,out var uri)&&uri.Scheme is "http" or "https")
            return new UriBuilder(uri){UserName="",Password="",Query="",Fragment=""}.Uri.AbsoluteUri;
        if(value.StartsWith("git@",StringComparison.Ordinal)&&!value.Contains('\n'))return value;
        return ""; // Local machine paths are not useful portable origin URLs.
    }

    /// <summary>Snapshots Git-reachable history plus index patch, tracked deletions and all nonignored working files.</summary>
    private static async Task SnapshotRepo(string stage,string name,string source)
    {
        Console.WriteLine("SNAPSHOT_BEGIN "+name);
        source=Path.GetFullPath(source);string reports=Path.Combine(stage,"reports",name);Directory.CreateDirectory(reports);
        string head=(await Git(source,"rev-parse","HEAD")).Trim();
        string status=await Git(source,"status","--porcelain=v1","-z","--untracked-files=all");
        var branchResult=await Execute("git",["-C",source,"symbolic-ref","--quiet","--short","HEAD"]);
        string branch=branchResult.ExitCode==0?branchResult.Output.Trim():"";
        var origin=await Execute("git",["-C",source,"remote","get-url","origin"]);
        File.WriteAllText(Path.Combine(reports,"head.txt"),head+"\n");File.WriteAllText(Path.Combine(reports,"branch.txt"),branch+"\n");
        File.WriteAllText(Path.Combine(reports,"origin.txt"),origin.ExitCode==0?Remote(origin.Output):"");
        File.WriteAllText(Path.Combine(reports,"status.txt"),status.Replace('\0','\n'));
        await Git(source,"diff","--cached","--binary","--full-index","--output="+Path.Combine(reports,"index.patch"));
        Directory.CreateDirectory(Path.Combine(stage,"history"));
        await Git(source,"bundle","create",Path.Combine(stage,"history",name+".bundle"),"--all","HEAD");
        Console.WriteLine("SNAPSHOT_HISTORY "+name);
        var files=(await Git(source,"ls-files","--cached","--others","--exclude-standard","-z")).Split('\0',StringSplitOptions.RemoveEmptyEntries)
            .Concat((await Git(source,"ls-tree","-r","--name-only","-z","HEAD")).Split('\0',StringSplitOptions.RemoveEmptyEntries)).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
        var hashes=new Dictionary<string,string>(StringComparer.Ordinal);var deleted=new List<string>();
        foreach(string relative in files)
        {
            string from=Child(source,relative);
            if(!File.Exists(from)&&!Directory.Exists(from)){deleted.Add(relative);continue;}
            NoLinks(source,from);if(Directory.Exists(from))throw new IOException("Submodule/directory requires an explicit repository snapshot: "+from);
            string to=Child(stage,"working/"+name+"/"+relative),before=Digest(from);Copy(from,to);
            if(Digest(to)!=before)throw new IOException("Changing source during handoff: "+from);hashes.Add(from,before);
        }
        File.WriteAllLines(Path.Combine(reports,"deleted.txt"),deleted,new UTF8Encoding(false));
        foreach(var pair in hashes)if(Digest(pair.Key)!=pair.Value)throw new IOException("Source changed before snapshot completed: "+pair.Key);
        if((await Git(source,"rev-parse","HEAD")).Trim()!=head||await Git(source,"status","--porcelain=v1","-z","--untracked-files=all")!=status)
            throw new IOException("Repository changed during handoff; retry: "+source);
        Console.WriteLine($"SNAPSHOT_COMPLETE {name} files={hashes.Count}");
    }

    /// <summary>Builds a fresh ZIP and checksum, preserving current local work without committing or pushing.</summary>
    private static async Task Pack(string root,string output,Dictionary<string,string> options)
    {
        if(File.Exists(output)||File.Exists(output+".sha256"))throw new IOException("Handoff ZIP/checksum already exists");
        string stage=Path.Combine(Path.GetTempPath(),"nexrt-handoff-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(stage);
        string partial=output+".partial-"+Guid.NewGuid().ToString("N");
        try
        {
            await SnapshotRepo(stage,"rtos",root);
            if(options.TryGetValue("riscue",out var riscue))await SnapshotRepo(stage,"RISCue",riscue);
            if(options.TryGetValue("sharp",out var sharp))await SnapshotRepo(stage,"SharpTestBench",sharp);
            string prompt=Path.Combine(root,"OPTIPLEX-RESTART-PROMPT.txt");Copy(prompt,Path.Combine(stage,"OPTIPLEX-RESTART-PROMPT.txt"));
            Copy(Path.Combine(root,"HANDOFF-START-HERE.txt"),Path.Combine(stage,"START-HERE.txt"));
            if(options.TryGetValue("native",out var native))
            {
                Copy(native,Path.Combine(stage,"tools/linux-x64/NexRT.Handoff"));
                if(File.Exists(native+".xml"))Copy(native+".xml",Path.Combine(stage,"tools/linux-x64/NexRT.Handoff.xml"));
            }
            if(options.TryGetValue("managed",out var managed))
                foreach(string file in Directory.GetFiles(managed).Where(file=>file.EndsWith(".dll",StringComparison.Ordinal)||file.EndsWith(".json",StringComparison.Ordinal)||file.EndsWith(".xml",StringComparison.Ordinal)))
                    Copy(file,Path.Combine(stage,"tools/managed",Path.GetFileName(file)));
            if(options.TryGetValue("cocotb",out var cocotb))
                foreach(string folder in new[]{"libs","share"})foreach(string file in Tree(Path.Combine(cocotb,folder)))
                    Copy(file,Child(stage,"runtime/cocotb/"+folder+"/"+Path.GetRelativePath(Path.Combine(cocotb,folder),file)));
            if(options.TryGetValue("sharp-host",out var host))Copy(host,Path.Combine(stage,"runtime/libsharptestbench_host.so"));
            if(options.TryGetValue("report",out var report))Copy(report,Path.Combine(stage,"reports/source-dependencies.txt"));
            File.WriteAllText(Path.Combine(stage,"repositories.txt"),"rtos\n"+(options.ContainsKey("riscue")?"RISCue\n":"")+(options.ContainsKey("sharp")?"SharpTestBench\n":""));
            Manifest(stage);Verify(stage);
            Directory.CreateDirectory(Path.GetDirectoryName(output)!);ZipFile.CreateFromDirectory(stage,partial,CompressionLevel.Optimal,false);
            string digest=Digest(partial,TotalLimit);
            File.Move(partial,output);File.WriteAllText(output+".sha256",digest+"  "+Path.GetFileName(output)+"\n");
            Console.WriteLine("HANDOFF_CREATED "+output+" bytes="+new FileInfo(output).Length);
        }
        finally{if(File.Exists(partial))File.Delete(partial);DeleteOwnedTemporary(stage);}
    }

    /// <summary>Restores into a fresh destination; existing work is never overwritten or reset.</summary>
    private static async Task Restore(string package,string destination)
    {
        Verify(package);
        if(destination.StartsWith(package+Path.DirectorySeparatorChar,PathComparison))throw new IOException("Restore outside the verified package directory");
        if(Directory.Exists(destination)||File.Exists(destination))throw new IOException("Restore destination must not exist");
        string existing=Path.GetDirectoryName(destination)!;
        while(!Directory.Exists(existing))existing=Path.GetDirectoryName(existing)??throw new IOException("Missing restore parent");
        NoLinks(Path.GetPathRoot(existing)!,existing);
        Directory.CreateDirectory(destination);
        foreach(string name in File.ReadAllLines(Path.Combine(package,"repositories.txt")))
        {
            if(name is not ("rtos" or "RISCue" or "SharpTestBench"))throw new IOException("Unknown handoff repository");
            string repo=Child(destination,name),reports=Child(package,"reports/"+name);
            var clone=await Execute("git",["clone",Child(package,"history/"+name+".bundle"),repo]);if(clone.ExitCode!=0)throw new IOException(clone.Output);
            string head=File.ReadAllText(Path.Combine(reports,"head.txt")).Trim(),branch=File.ReadAllText(Path.Combine(reports,"branch.txt")).Trim();
            if(!System.Text.RegularExpressions.Regex.IsMatch(head,@"\A(?:[0-9a-f]{40}|[0-9a-f]{64})\z"))throw new IOException("Invalid Git object identity");
            if(branch.Length==0)await Git(repo,"checkout","--detach",head);else await Git(repo,"checkout","-B",branch,head);
            string index=Path.Combine(reports,"index.patch");if(new FileInfo(index).Length!=0)await Git(repo,"apply","--cached","--binary","--whitespace=nowarn",index);
            foreach(string relative in File.ReadAllLines(Path.Combine(reports,"deleted.txt")))
            {
                string file=RepoChild(repo,relative);if(File.Exists(file)){NoLinks(repo,file);File.Delete(file);}
            }
            string working=Child(package,"working/"+name);
            foreach(string file in Tree(working))
            {
                string to=RepoChild(repo,Path.GetRelativePath(working,file));
                string parent=Path.GetDirectoryName(to)!;while(!Directory.Exists(parent))parent=Path.GetDirectoryName(parent)!;
                NoLinks(repo,parent);if(File.Exists(to))NoLinks(repo,to);Copy(file,to);
                if(Digest(file)!=Digest(to))throw new IOException("Restored file mismatch");
            }
            string remote=File.ReadAllText(Path.Combine(reports,"origin.txt")).Trim();
            if(remote.Length>0)await Git(repo,"remote","set-url","origin",remote);else await Git(repo,"remote","remove","origin");
            File.WriteAllText(Path.Combine(destination,name+"-restored-status.txt"),(await Git(repo,"status","--short")));
        }
        string runtime=Path.Combine(package,"runtime");
        if(Directory.Exists(runtime))foreach(string file in Tree(runtime))Copy(file,Child(destination,"runtime/"+Path.GetRelativePath(runtime,file)));
        string host=Path.Combine(runtime,"libsharptestbench_host.so"),sharpTarget=Path.Combine(destination,"SharpTestBench");
        if(File.Exists(host)&&Directory.Exists(sharpTarget))Copy(host,Path.Combine(sharpTarget,"build/libsharptestbench_host.so"));
        Copy(Path.Combine(package,"OPTIPLEX-RESTART-PROMPT.txt"),Path.Combine(destination,"OPTIPLEX-RESTART-PROMPT.txt"));
        Console.WriteLine("HANDOFF_RESTORED "+destination+"; run doctor before building");
    }

    /// <summary>Extracts and restores a real archive in an owned temporary directory, comparing all worktree payloads and index patches.</summary>
    private static async Task AuditZip(string zip)
    {
        string root=Path.Combine(Path.GetTempPath(),"nexrt-archive-audit-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        try
        {
            string package=Path.Combine(root,"package"),destination=Path.Combine(root,"restored");
            ZipFile.ExtractToDirectory(zip,package);await Restore(package,destination);
            foreach(string name in File.ReadAllLines(Path.Combine(package,"repositories.txt")))
            {
                string working=Path.Combine(package,"working",name),repo=Path.Combine(destination,name),reports=Path.Combine(package,"reports",name);
                foreach(string file in Tree(working))
                    if(Digest(file)!=Digest(RepoChild(repo,Path.GetRelativePath(working,file))))throw new IOException("Archive restore differs: "+file);
                foreach(string deleted in File.ReadAllLines(Path.Combine(reports,"deleted.txt")))
                    if(File.Exists(RepoChild(repo,deleted)))throw new IOException("Deleted source file was restored");
                string index=Path.Combine(root,name+"-index.patch");await Git(repo,"diff","--cached","--binary","--full-index","--output="+index);
                if(Digest(index)!=Digest(Path.Combine(reports,"index.patch")))throw new IOException("Restored index differs: "+name);
                if((await Git(repo,"rev-parse","HEAD")).Trim()!=File.ReadAllText(Path.Combine(reports,"head.txt")).Trim())throw new IOException("Restored HEAD differs");
                Console.WriteLine("PASS archive restore: "+name+"; HEAD, index, worktree hashes and deletions");
            }
        }
        finally{DeleteOwnedTemporary(root);}
    }

    /// <summary>Proves staged, unstaged, untracked and deleted working files survive Git bundle reconstruction.</summary>
    private static async Task SelfTest()
    {
        string root=Path.Combine(Path.GetTempPath(),"nexrt-handoff-test-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        try
        {
            string source=Path.Combine(root,"source");Directory.CreateDirectory(source);
            await Git(source,"init");await Git(source,"config","user.name","Handoff Fixture");await Git(source,"config","user.email","handoff@example.invalid");
            File.WriteAllText(Path.Combine(source,"tracked.txt"),"base\n");File.WriteAllText(Path.Combine(source,"deleted.txt"),"base\n");
            File.WriteAllText(Path.Combine(source,".gitignore"),"ignored/\n");await Git(source,"add",".");await Git(source,"commit","-m","fixture");
            File.WriteAllText(Path.Combine(source,"tracked.txt"),"staged\n");await Git(source,"add","tracked.txt");
            File.WriteAllText(Path.Combine(source,"tracked.txt"),"unstaged\n");File.Delete(Path.Combine(source,"deleted.txt"));
            File.WriteAllText(Path.Combine(source,"untracked.txt"),"new\n");
            Directory.CreateDirectory(Path.Combine(source,"ignored"));File.WriteAllText(Path.Combine(source,"ignored/secret.txt"),"excluded\n");
            File.WriteAllText(Path.Combine(source,"OPTIPLEX-RESTART-PROMPT.txt"),"fixture\n");File.WriteAllText(Path.Combine(source,"HANDOFF-START-HERE.txt"),"fixture\n");
            string zip=Path.Combine(root,"test.zip");await Pack(source,zip,new Dictionary<string,string>());
            string extracted=Path.Combine(root,"extracted");ZipFile.ExtractToDirectory(zip,extracted);Verify(extracted);
            string restored=Path.Combine(root,"restored");await Restore(extracted,restored);
            string repo=Path.Combine(restored,"rtos");
            // Porcelain labels can differ with the destination's newline
            // settings. Prove index bytes, worktree bytes and tracking instead.
            var untracked=await Execute("git",["-C",repo,"ls-files","--error-unmatch","untracked.txt"]);
            if(await Git(repo,"show",":tracked.txt")!="staged\n"||
                File.ReadAllText(Path.Combine(repo,"tracked.txt"))!="unstaged\n"||
                File.ReadAllText(Path.Combine(repo,"untracked.txt"))!="new\n"||untracked.ExitCode==0||
                File.Exists(Path.Combine(repo,"deleted.txt"))||File.Exists(Path.Combine(repo,"ignored/secret.txt")))
                throw new IOException("Git index bytes, working content, tracking or deletion round-trip failed");
            File.AppendAllText(Path.Combine(extracted,"working/rtos/tracked.txt"),"tamper");
            bool rejected=false;try{Verify(extracted);}catch(IOException){rejected=true;}
            if(!rejected)throw new IOException("Handoff tamper was not rejected");
            Console.WriteLine("PASS: Git history/index/worktree/deletion/untracked round-trip, ignored-file exclusion and tamper rejection");
        }
        finally{DeleteOwnedTemporary(root);}
    }
}
