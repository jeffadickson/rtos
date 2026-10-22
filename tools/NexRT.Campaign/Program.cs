using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using NexRT.Host;

/// <summary>Builds each ten authored acceptance iterations, verifies cumulatively and commits locally.</summary>
internal static class Program
{
    /// <summary>Runs one explicitly selected milestone; failed commands never produce a passing milestone commit.</summary>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if(args.Length!=2)throw new ArgumentException("Usage: NexRT.Campaign ROOT BATCH(1..20)");
            string root=Path.GetFullPath(args[0]);int batch=int.Parse(args[1]);
            if(batch<1||batch>20)throw new ArgumentException("Batch outside 1..20");
            string directory=Path.Combine(root,$"requirements/campaign-200/checkpoints/{batch:00}");
            string ledger=Path.Combine(root,"requirements/campaign-200/iterations.tsv");
            var prior=File.Exists(ledger)?File.ReadAllLines(ledger).ToList():["iteration\trequirement\tacceptance\tscope\tresult\tsource_sha256"];
            if(prior.Count!=1+(batch-1)*10)throw new IOException("Milestones must be completed once, in order");
            Directory.CreateDirectory(directory);
            async Task<string> Run(string name,string executable,params string[] parameters)
            {
                var start=new ProcessStartInfo(executable){WorkingDirectory=root};
                foreach(var parameter in parameters)start.ArgumentList.Add(parameter);
                var result=await HostProcess.Run(start,TimeSpan.FromMinutes(10));
                if(name is not ("stage" or "commit"))File.WriteAllText(Path.Combine(directory,name+".log"),result.Output);
                if(result.ExitCode!=0||result.TimedOut||result.OutputLimit||result.IoError!=null)throw new IOException(name+" failed; inspect "+directory);
                return result.Output;
            }
            string tracker=Path.Combine(root,"build/host/native-tools/NexRT.Requirements/NexRT.Requirements");
            UpdateLinks(root);
            await Run("generate",tracker,"generate",root);
            await Run("configure","cmake","-S",root,"-B","build/host","-DRTOS_INTERPRETER=ON");
            await Run("build","cmake","--build","build/host","-j2");
            await Run("tests","ctest","--test-dir","build/host","--output-on-failure");
            string executable=Path.Combine(root,$"build/host/campaign_{batch:00}");
            string listed=await Run("cases",executable,"--list");
            string[] cases=listed.Split('\n',StringSplitOptions.RemoveEmptyEntries|StringSplitOptions.TrimEntries);
            if(cases.Length!=10)throw new IOException("Each milestone requires ten distinct authored cases");
            string source=Path.Combine(root,$"tests/campaign/batch_{batch:00}.cpp");
            string hash=Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(source)));
            for(int i=0;i<10;i++)
            {
                var fields=cases[i].Split('\t');
                if(fields.Length!=3||int.Parse(fields[0])!=(batch-1)*10+i+1)throw new IOException("Noncontiguous acceptance identity");
                prior.Add(cases[i]+"\thost-acceptance\tpassed\t"+hash);
            }
            File.WriteAllLines(ledger,prior);
            await Run("identity","git","rev-parse","HEAD");
            // No network operations. Stage only the RTOS repository after all gates pass.
            await Run("stage","git","add","-A");
            await Run("commit","git","-c","user.name=Codex","-c","user.email=codex@local","commit","-m",$"Requirements campaign {(batch-1)*10+1:000}-{batch*10:000}: verified acceptance milestone");
            Console.WriteLine($"PASS milestone {batch:00}: {batch*10}/200 authored acceptance iterations; local commit created");
            return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
    }
    /// <summary>Reconciles explicitly reviewed source links; never promotes host tests to target qualification.</summary>
    private static void UpdateLinks(string root)
    {
        string links=Path.Combine(root,"requirements/campaign-200/links.tsv");
        if(!File.Exists(links))return;
        string path=Path.Combine(root,"requirements/catalogue.json");
        var records=JsonNode.Parse(File.ReadAllText(path))!.AsArray();
        foreach(string row in File.ReadLines(links).Where(s=>!string.IsNullOrWhiteSpace(s)))
        {
            var f=row.Split('\t');if(f.Length!=4)throw new IOException("Invalid reviewed requirement link");
            var record=records.Single(r=>r!["id"]!.GetValue<string>()==f[0])!;
            foreach(var pair in new[]{("implementation_files",f[1]),("verification_files",f[2])})
            {
                var array=record[pair.Item1]!.AsArray();
                foreach(string file in pair.Item2.Split(','))
                    if(!array.Any(n=>n!.GetValue<string>()==file))array.Add(file);
            }
            record["status"]="PartialSourceUnverified";record["finding"]=f[3];
        }
        File.WriteAllText(path,records.ToJsonString(new System.Text.Json.JsonSerializerOptions{WriteIndented=true})+"\n");
    }
}
