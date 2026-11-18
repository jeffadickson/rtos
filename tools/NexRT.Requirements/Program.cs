using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace NexRT.Requirements;

/// <summary>A reviewed implementation requirement; file links describe source evidence, not runtime proof.</summary>
internal sealed record Requirement
{
    public required string Id { get; init; }
    public required string Title { get; init; }
    public required string Category { get; init; }
    public required string Statement { get; init; }
    public required string Rationale { get; init; }
    public required string Acceptance { get; init; }
    public required string Verification { get; init; }
    public required string[] Dependencies { get; init; }
    public required string Status { get; init; }
    public required string[] ImplementationFiles { get; init; }
    public required string[] VerificationFiles { get; init; }
    public required string PlannedTest { get; init; }
    public required string Configuration { get; init; }
    public required string ResourceBudget { get; init; }
    public required int ReviewIteration { get; init; }
    public required string[] InspectedFiles { get; init; }
    public required string Finding { get; init; }
}

/// <summary>Validates a C#-managed tracker and generates StrictDoc, CSV, and Sphinx views deterministically.</summary>
internal static class Program
{
    /// <summary>Runs integrate, generate, check, self-test, or a real builder checkpoint.</summary>
    /// <param name="args">Command, repository root, and optional checkpoint iteration.</param>
    /// <returns>Zero only if all requested validations and subprocesses pass.</returns>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if (args.Length < 2) throw new ArgumentException("Usage: NexRT.Requirements <integrate|generate|check|self-test|checkpoint|roadmap|qualification-checkpoint|qualification-resume> <root> [boundary]");
            var root = Path.GetFullPath(args[1]);
            switch (args[0])
            {
                case "integrate": Integrate(root); break;
                case "import-pipe": ImportPipe(root, Console.In.ReadToEnd()); break;
                case "generate": Views(root, false); break;
                case "check": Views(root, true); break;
                case "self-test": SelfTest(root); break;
                case "roadmap":
                    if (args.Length != 3 || !int.TryParse(args[2], out int count) || count < 10 || count > 100 || count % 10 != 0)
                        throw new ArgumentException("Roadmap boundary must be 10,20,...,100.");
                    Roadmap(root, count); break;
                case "qualification-checkpoint":
                case "qualification-resume":
                    if (args.Length != 3 || !int.TryParse(args[2], out int boundary) || boundary < 10 || boundary > 100 || boundary % 10 != 0)
                        throw new ArgumentException("Qualification boundary must be 10,20,...,100.");
                    if (args[0] == "qualification-checkpoint") Roadmap(root, boundary);
                    else
                    {
                        if (Load(root).Length != 160 + boundary) throw new InvalidDataException("Resume boundary does not match catalogue.");
                        Views(root, true);
                    }
                    await QualificationCheckpoint.Run(root, boundary); break;
                case "checkpoint":
                    if (args.Length != 3 || !int.TryParse(args[2], out int round) || round is not (25 or 50 or 75 or 100))
                        throw new ArgumentException("Checkpoint must be 25, 50, 75 or 100.");
                    await Checkpoint(root, round); break;
                default: throw new ArgumentException("Unknown command.");
            }
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
    }

    /// <summary>Resolves existing evidence without permitting rooted paths, traversal, or symbolic-link escapes.</summary>
    private static string EvidencePath(string root, string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) || relative.Contains('\\') ||
            relative.Split('/').Any(p => p is ".." or "." or "")) throw new InvalidDataException("Unsafe evidence path: " + relative);
        string path = root;
        foreach (var part in relative.Split('/'))
        {
            path = Path.Combine(path, part);
            if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Symbolic-link evidence is not accepted: " + relative);
        }
        if (!File.Exists(path)) throw new InvalidDataException("Missing evidence file: " + relative);
        return path;
    }

    /// <summary>Loads canonical records; every requirement has a distinct, contiguous substantive review entry.</summary>
    private static Requirement[] Load(string root)
    {
        var records = JsonSerializer.Deserialize(File.ReadAllText(Path.Combine(root, "requirements/catalogue.json")), TrackerJsonContext.Default.Requirements)
            ?? throw new InvalidDataException("Null catalogue.");
        Validate(root, records);
        return records.OrderBy(r => r.ReviewIteration).ToArray();
    }

    /// <summary>Checks traceability, reviewed paths, allocation budgets, status honesty, and an acyclic dependency graph.</summary>
    private static void Validate(string root, Requirement[] records)
    {
        if (records.Length is < 1 or > 10000) throw new InvalidDataException("Expected 1..10000 reviewed requirements.");
        var byId = new Dictionary<string, Requirement>(StringComparer.Ordinal);
        var iterations = new HashSet<int>();
        foreach (var r in records)
        {
            if (!Regex.IsMatch(r.Id, "^NXR-[0-9]{3}$") || !byId.TryAdd(r.Id, r) || !iterations.Add(r.ReviewIteration))
                throw new InvalidDataException("Duplicate/invalid identity: " + r.Id);
            if (r.ReviewIteration < 1 || r.ReviewIteration > records.Length)
                throw new InvalidDataException("Noncontiguous review identity: " + r.Id);
            if (new[] { r.Title, r.Category, r.Statement, r.Rationale, r.Acceptance, r.Verification,
                r.PlannedTest, r.Configuration, r.ResourceBudget, r.Finding }.Any(string.IsNullOrWhiteSpace) ||
                !r.Statement.Contains(" shall ", StringComparison.Ordinal) || r.Finding.Length < 20)
                throw new InvalidDataException("Incomplete normative requirement: " + r.Id);
            if (r.Status is not ("Planned" or "SourceUnverified" or "PartialSourceUnverified"))
                throw new InvalidDataException("Unsupported evidence status; runtime Verified requires a separate qualified evidence workflow: " + r.Id);
            if (r.Status == "Planned" && (r.ImplementationFiles.Length != 0 || r.VerificationFiles.Length != 0))
                throw new InvalidDataException("Planned work cannot claim existing implementation/test evidence: " + r.Id);
            if (r.Status != "Planned" && r.ImplementationFiles.Length == 0)
                throw new InvalidDataException("Source claim lacks source: " + r.Id);
            if (r.InspectedFiles.Length == 0) throw new InvalidDataException("Review lacks inspected source: " + r.Id);
            foreach (var path in r.ImplementationFiles.Concat(r.VerificationFiles).Concat(r.InspectedFiles)) EvidencePath(root, path);
        }
        var visiting = new HashSet<string>(); var done = new HashSet<string>();
        void Visit(string id)
        {
            if (done.Contains(id)) return;
            if (!byId.TryGetValue(id, out var r)) throw new InvalidDataException("Unknown dependency: " + id);
            if (!visiting.Add(id)) throw new InvalidDataException("Dependency cycle at " + id);
            foreach (var dependency in r.Dependencies) Visit(dependency);
            visiting.Remove(id); done.Add(id);
        }
        foreach (var id in byId.Keys) Visit(id);
    }

    /// <summary>Imports a source-reviewed pipe-delimited batch from stdin, validating all records before any canonical write.</summary>
    private static void ImportPipe(string root, string input)
    {
        var prior = Load(root);
        var batch = new List<Requirement>();
        string[] List(string value) => value.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        foreach (var line in input.Replace("\r\n", "\n").Split('\n').Where(s => !string.IsNullOrWhiteSpace(s)))
        {
            var f = line.Split('|').Select(s => s.Trim()).ToArray();
            if (f.Length is < 12 or > 15) throw new InvalidDataException("Expected 12..15 reviewed batch fields.");
            var implementation = f.Length > 12 ? List(f[12]) : [];
            batch.Add(new Requirement {
                Id=f[0], Title=f[1], Category=f[2], Statement=f[3], Rationale=f[4], Acceptance=f[5],
                Verification=f[6], Dependencies=List(f[7]), Configuration=f[8], ResourceBudget=f[9],
                InspectedFiles=List(f[10]), Finding=f[11], ImplementationFiles=implementation,
                VerificationFiles=f.Length > 13 ? List(f[13]) : [],
                Status=f.Length > 14 ? f[14] : implementation.Length==0 ? "Planned" : "PartialSourceUnverified",
                ReviewIteration=int.Parse(f[0].AsSpan(4)), PlannedTest="TEST-"+f[0]
            });
        }
        if (batch.Count == 0) throw new InvalidDataException("No reviewed input received.");
        var all=prior.Concat(batch).ToArray(); Validate(root, all);
        var canonical=Path.Combine(root,"requirements/catalogue.json");
        File.WriteAllText(canonical+".tmp",JsonSerializer.Serialize(all,TrackerJsonContext.Default.Requirements)+"\n",new UTF8Encoding(false));
        File.Move(canonical+".tmp",canonical,true);
        File.WriteAllText(Path.Combine(root,$"requirements/batch-{batch.Min(r=>r.ReviewIteration):000}-{batch.Max(r=>r.ReviewIteration):000}.psv"),input,new UTF8Encoding(false));
        Views(root,false);
        Console.WriteLine($"Imported {batch.Count} source-reviewed records using C#.");
    }

    /// <summary>Hashes exact bytes for reproducible catalogue and source fingerprints.</summary>
    private static string Hash(string path) => Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)));

    /// <summary>Imports the next ten authored qualification reviews; never marks proposed services implemented.</summary>
    private static void Roadmap(string root, int count)
    {
        var prior = Load(root);
        if (prior.Length != 160 + count - 10) throw new InvalidDataException("Roadmap batches must be imported once in order.");
        var input = File.ReadAllLines(Path.Combine(root, "requirements/qualification-100/reviews.psv"));
        if (input.Length != 100) throw new InvalidDataException("Expected 100 authored reviews.");
        string[] categories = ["Resource qualification", "Kernel lifecycle", "RISC-V configuration", "Failure reproduction", "HIL wire protocol", "Host executive", "Target executive", "Independent observer", "Driver qualification", "DMA and release"];
        string[] inspected = ["include/rtos.hpp,src/kernel.cpp,src/scheduler.hpp,docs/memory.rst", "include/rtos.hpp,src/kernel.cpp,include/rtos_mutex.hpp,include/rtos_queue.hpp", "include/rtos_hal.hpp,include/rtos_hal_atomic.hpp,sim/interpreter/machine.hpp,sim/interpreter/machine.cpp,ports/riscue/context.S", "include/rtos_trace.hpp,include/rtos_fault_record.hpp,docs/hardware-in-loop.rst", "docs/hardware-in-loop.rst,docs/hil-verification-plan.rst", "docs/hil-verification-plan.rst,requirements/hil-design.csv", "src/kernel.cpp,docs/hil-verification-plan.rst,include/rtos_queue.hpp", "docs/hardware-in-loop.rst,docs/hil-verification-plan.rst", "include/rtos_uart.hpp,docs/drivers.rst,tests/uart.cpp", "include/rtos_dma.hpp,src/dma.cpp,tests/dma.cpp,docs/dma.rst"];
        string[] dependencies = ["NXR-081,NXR-025", "NXR-001,NXR-128,NXR-129", "NXR-102,NXR-139", "NXR-072,NXR-073,NXR-094", "NXR-137", "NXR-076,NXR-094", "NXR-039,NXR-048", "NXR-099", "NXR-041,NXR-119,NXR-149", "NXR-134,NXR-135,NXR-159"];
        var pipe = new StringBuilder();
        for (int i = count - 10; i < count; ++i)
        {
            var f = input[i].Split('|');
            if (f.Length != 7 || f[0] != $"NXR-{161+i:000}") throw new InvalidDataException("Malformed roadmap review at " + (i+1));
            int group = i / 10;
            pipe.AppendLine(string.Join('|', f[0], f[1], categories[group], f[2], f[6], f[3], f[4], dependencies[group],
                "Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.", f[5], inspected[group], f[6]));
        }
        ImportPipe(root, pipe.ToString());
    }

    /// <summary>Quotes CSV values including embedded quotes, commas, and newlines.</summary>
    private static string Csv(string value) => "\"" + value.Replace("\"", "\"\"") + "\"";

    /// <summary>Writes an artifact atomically, or rejects missing/stale generated output in check mode.</summary>
    private static void Artifact(string root, string relative, string contents, bool check)
    {
        var path = Path.Combine(root, relative);
        contents = contents.Replace("\r\n", "\n");
        if (check)
        {
            if (!File.Exists(path) || File.ReadAllText(path) != contents) throw new InvalidDataException("Stale/missing generated artifact: " + relative);
            return;
        }
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".tmp";
        File.WriteAllText(temporary, contents, new UTF8Encoding(false));
        File.Move(temporary, path, true);
    }

    /// <summary>Emits a documented StrictDoc-format export, complete CSV, substantive review log, and Sphinx manual view.</summary>
    private static void Views(string root, bool check)
    {
        var rows = Load(root);
        var digest = Hash(Path.Combine(root, "requirements/catalogue.json"));
        var sdoc = new StringBuilder($"[DOCUMENT]\nTITLE: NexRT Implementation Requirements\nUID: NXR-BASELINE\nVERSION: catalogue-{digest[..12]}\n\n");
        var csv = new StringBuilder("requirement_id,title,category,implementation_status,requirement,acceptance_criteria,verification,planned_test,dependencies,implementation_files,verification_files,configuration,resource_budget\n");
        var reviews = new StringBuilder("iteration,requirement_id,inspected_files,finding,catalogue_sha256\n");
        var rst = new StringBuilder("Implementation requirements tracker\n===================================\n\nGenerated by C#; edit requirements/catalogue.json, then regenerate.\n\nThis is a requirements baseline, not a claim of completed runtime services.\nSourceUnverified and PartialSourceUnverified do not mean target-tested.\nReal StrictDoc validation is a separate build gate; see static-services for execution evidence.\n\n");
        rst.AppendLine($"Reviewed requirements: **{rows.Length}**. Catalogue SHA-256: ``{digest}``.\n");
        foreach (var r in rows)
        {
            sdoc.Append($"[REQUIREMENT]\nUID: {r.Id}\nSTATUS: {r.Status}\nTITLE: {r.Title}\nSTATEMENT: >>>\n{r.Statement}\nAcceptance: {r.Acceptance}\nVerification: {r.Verification}\nConfiguration: {r.Configuration}\nStatic budget: {r.ResourceBudget}\nDependencies: {string.Join(", ",r.Dependencies)}\nImplementation: {string.Join(", ",r.ImplementationFiles.DefaultIfEmpty("none"))}\nVerification sources: {string.Join(", ",r.VerificationFiles.DefaultIfEmpty("none"))}\n<<<\n\n");
            csv.AppendLine(string.Join(',', new[] { r.Id,r.Title,r.Category,r.Status,r.Statement,r.Acceptance,r.Verification,r.PlannedTest,
                string.Join(';',r.Dependencies),string.Join(';',r.ImplementationFiles),string.Join(';',r.VerificationFiles),r.Configuration,r.ResourceBudget }.Select(Csv)));
            reviews.AppendLine(string.Join(',',new[] { r.ReviewIteration.ToString(),r.Id,string.Join(';',r.InspectedFiles),r.Finding,digest }.Select(Csv)));
            string heading = r.Id + " " + r.Title;
            rst.AppendLine(heading); rst.AppendLine(new string('-',heading.Length));
            foreach (var pair in new[] { ("Status",r.Status),("Category",r.Category),("Requirement",r.Statement),("Rationale",r.Rationale),
                ("Acceptance",r.Acceptance),("Verification",r.Verification),("Planned test",r.PlannedTest),
                ("Dependencies",r.Dependencies.Length==0 ? "None" : string.Join(", ",r.Dependencies)),
                ("Configuration",r.Configuration),("Static resource budget",r.ResourceBudget),("Review finding",r.Finding) })
                rst.AppendLine($"\n**{pair.Item1}:** {pair.Item2}\n");
        }
        Artifact(root,"requirements/nexrt.sdoc",sdoc.ToString(),check);
        Artifact(root,"requirements/traceability.csv",csv.ToString(),check);
        Artifact(root,"requirements/review-iterations.csv",reviews.ToString(),check);
        var sources = rows.SelectMany(r=>r.InspectedFiles.Concat(r.ImplementationFiles).Concat(r.VerificationFiles))
            .Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToDictionary(p=>p,p=>Hash(EvidencePath(root,p)));
        Artifact(root,"requirements/reviewed-sources.json",JsonSerializer.Serialize(sources,TrackerJsonContext.Default.SourceHashes)+"\n",check);
        Artifact(root,"docs/requirements-tracker.rst",rst.ToString(),check);
        QualificationTasks(root, rows, check);
        Console.WriteLine($"Tracker {(check ? "check" : "generation")} passed: {rows.Length} substantive review iterations; {rows.Count(r=>r.Status=="Planned")} planned; no runtime verification claims.");
    }

    /// <summary>Creates one Git-tracked task per qualification requirement with deterministic, checkable traceability.</summary>
    private static void QualificationTasks(string root, Requirement[] rows, bool check)
    {
        var tasks = rows.Where(r => r.ReviewIteration is >= 161 and <= 260).ToArray();
        if (tasks.Length == 0) return;
        var index = new StringBuilder("# NexRT qualification tasks\n\nThese are local Git-tracked tasks, not remote issues. Review completion does not mean implementation completion. All new acceptance work is planned until its specified evidence is reviewed.\n\n");
        foreach (var r in tasks)
        {
            string id = "TASK-" + r.Id;
            index.AppendLine($"- [ ] [{id}: {r.Title}]({id}.md)");
            string text = $"# {id}: {r.Title}\n\nStatus: Planned\n\nRequirement: {r.Id}; qualification review {r.ReviewIteration - 160}/100.\n\n## Required behavior\n\n{r.Statement}\n\n## Reason and source finding\n\n{r.Finding}\n\n## Acceptance criteria\n\n{r.Acceptance}\n\n## Verification and deliverables\n\n{r.Verification}\n\nDeliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.\n\n## Configuration and resource ceiling\n\n{r.Configuration}\n\n{r.ResourceBudget}\n\n## Dependencies\n\n{string.Join(", ",r.Dependencies)}\n\n## Inspected sources\n\n{string.Join("\n", r.InspectedFiles.Select(p=>"- "+p))}\n\n## Completion checklist\n\n- [ ] Implement required behavior without weakening default contracts.\n- [ ] Pass the specific positive and negative acceptance checks above.\n- [ ] Record resource measurements and limits for the tested profile.\n- [ ] Retain evidence and update requirement status through the qualified evidence workflow.\n";
            Artifact(root, $"tasks/qualification-100/{id}.md", text, check);
        }
        Artifact(root, "tasks/qualification-100/README.md", index.ToString(), check);
    }

    /// <summary>Adds CMake/CTest/Sphinx integration once; source generation remains an explicit C# action.</summary>
    private static void Integrate(string root)
    {
        Directory.CreateDirectory(Path.Combine(root,"requirements"));
        const string include = "include(cmake/requirements.cmake)";
        string cmake = Path.Combine(root,"CMakeLists.txt");
        if (!File.ReadAllText(cmake).Contains(include,StringComparison.Ordinal)) File.AppendAllText(cmake,"\n"+include+"\n");
        string index = Path.Combine(root,"docs/index.rst");
        var text = File.ReadAllText(index);
        if (!text.Contains("   requirements-tracker",StringComparison.Ordinal)) File.WriteAllText(index,text.Replace("   tooling","   tooling\n   requirements-tracker",StringComparison.Ordinal));
        File.WriteAllText(Path.Combine(root,"cmake/requirements.cmake"), """
if(DOTNET_EXECUTABLE)
  set(NXR_REQUIREMENTS_DLL ${CMAKE_BINARY_DIR}/requirements-tool/NexRT.Requirements.dll)
  add_custom_command(OUTPUT ${NXR_REQUIREMENTS_DLL}
    COMMAND ${DOTNET_EXECUTABLE} build ${CMAKE_SOURCE_DIR}/tools/NexRT.Requirements/NexRT.Requirements.csproj
      --configuration Release --output ${CMAKE_BINARY_DIR}/requirements-tool --nologo
      --property:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/requirements-obj/
    DEPENDS tools/NexRT.Requirements/NexRT.Requirements.csproj tools/NexRT.Requirements/Program.cs VERBATIM)
  add_custom_target(requirements_tool ALL DEPENDS ${NXR_REQUIREMENTS_DLL})
  add_custom_target(requirements_check ALL
    COMMAND ${DOTNET_EXECUTABLE} ${NXR_REQUIREMENTS_DLL} check ${CMAKE_SOURCE_DIR}
    DEPENDS requirements_tool VERBATIM)
  if(NOT RTOS_BUILD_FIRMWARE)
    add_test(NAME requirements_tracker COMMAND ${DOTNET_EXECUTABLE} ${NXR_REQUIREMENTS_DLL} check ${CMAKE_SOURCE_DIR})
    add_test(NAME requirements_negative COMMAND ${DOTNET_EXECUTABLE} ${NXR_REQUIREMENTS_DLL} self-test ${CMAKE_SOURCE_DIR})
  endif()
  add_dependencies(docs requirements_check)
endif()
"""+"\n");
        Console.WriteLine("Requirements tracker integrated with CMake, CTest and Sphinx.");
    }

    /// <summary>Runs negative checks against real baseline records without changing source or artifacts.</summary>
    private static void SelfTest(string root)
    {
        var rows=Load(root); var sample=rows[0];
        // Stable requirement identity is independent of task-local review numbering.
        Validate(root,[sample with { Id="NXR-901", Dependencies=[] }]);
        void Reject(Requirement[] changed,string label)
        {
            try { Validate(root,changed); } catch (Exception e) when (e is InvalidDataException or IOException or UnauthorizedAccessException)
            { Console.WriteLine("PASS: rejected "+label); return; }
            throw new InvalidDataException("Validator accepted "+label);
        }
        Reject([sample,sample],"duplicate IDs/reviews");
        Reject([sample with { Dependencies=[sample.Id] }],"dependency cycle");
        Reject([sample with { Dependencies=["NXR-999"] }],"unknown dependency");
        Reject([sample with { Acceptance="" }],"missing acceptance criteria");
        Reject([sample with { Status="Verified" }],"false runtime verification");
        Reject([sample with { InspectedFiles=["../outside.cs"] }],"path traversal");
        Reject([sample with { InspectedFiles=["missing/nonexistent.cs"] }],"missing inspected source");
        if(Csv("a,\"b\"")!="\"a,\"\"b\"\"\"") throw new InvalidDataException("CSV escaping failed.");
        Console.WriteLine("PASS: CSV delimiters/quotes preserved");
        TaskExportSelfTest(rows);
    }

    /// <summary>Proves task checking rejects changed acceptance, missing files and stale indexes without modifying repository artifacts.</summary>
    private static void TaskExportSelfTest(Requirement[] rows)
    {
        var tasks = rows.Where(r => r.ReviewIteration is >= 161 and <= 260).ToArray();
        if (tasks.Length == 0) return;
        string scratch = Path.Combine(Path.GetTempPath(), "nexrt-task-check-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(scratch);
        try
        {
            QualificationTasks(scratch, rows, false);
            QualificationTasks(scratch, rows, true);
            string task = Path.Combine(scratch, "tasks/qualification-100/TASK-" + tasks[0].Id + ".md");
            void Reject(string label)
            {
                try { QualificationTasks(scratch, rows, true); }
                catch (InvalidDataException) { Console.WriteLine("PASS: rejected " + label); return; }
                throw new InvalidDataException("Task checker accepted " + label);
            }
            File.AppendAllText(task, "\nUnreviewed acceptance change\n");
            Reject("modified task acceptance export");
            QualificationTasks(scratch, rows, false);
            File.Delete(task);
            Reject("missing task export");
            QualificationTasks(scratch, rows, false);
            File.AppendAllText(Path.Combine(scratch, "tasks/qualification-100/README.md"), "\nStale index\n");
            Reject("stale task index");
            QualificationTasks(scratch, rows, false);
            QualificationTasks(scratch, rows, true);
            Console.WriteLine("PASS: deterministic task exports round-trip");
        }
        finally { Directory.Delete(scratch, true); }
    }

    /// <summary>Runs real builds at each requested boundary and records commands, exit status, log hashes, and baseline fingerprint.</summary>
    private static async Task Checkpoint(string root,int iteration)
    {
        var rows=Load(root);
        if(rows.Length!=iteration) throw new InvalidDataException("Checkpoint count must match current reviewed baseline.");
        Views(root,true); SelfTest(root);
        var directory=Path.Combine(root,$"requirements/checkpoints/{iteration:000}"); Directory.CreateDirectory(directory);
        File.Copy(Path.Combine(root,"requirements/catalogue.json"),Path.Combine(directory,"catalogue.snapshot.json"),true);
        File.Copy(Path.Combine(root,"requirements/reviewed-sources.json"),Path.Combine(directory,"reviewed-sources.snapshot.json"),true);
        var commands=new (string Tool,string[] Args)[] {
            ("cmake",["--build","--preset","host"]),("ctest",["--preset","host"]),
            ("cmake",["--build","--preset","riscue"]),("cmake",["--build","--preset","riscue","--target","static_contract_check"]),
            ("cmake",["--build","--preset","host","--target","static_analysis"]),
            ("cmake",["--build","--preset","host","--target","docs"])
        };
        var results=new List<CommandResult>();
        foreach(var (tool,arguments) in commands)
        {
            var start=new ProcessStartInfo(tool){WorkingDirectory=root,UseShellExecute=false,RedirectStandardOutput=true,RedirectStandardError=true,CreateNoWindow=true};
            foreach(var a in arguments) start.ArgumentList.Add(a);
            using var process=Process.Start(start) ?? throw new IOException("Cannot start "+tool);
            var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync();
            string log=Path.Combine(directory,$"{results.Count+1:00}-{tool}.log");
            File.WriteAllText(log,await output+await error);
            results.Add(new CommandResult(tool,arguments,process.ExitCode,Path.GetRelativePath(root,log).Replace(Path.DirectorySeparatorChar,'/'),Hash(log)));
            Console.WriteLine($"Checkpoint {iteration}: {tool} {string.Join(' ',arguments)} => {process.ExitCode}");
            if(process.ExitCode!=0) throw new InvalidDataException("Builder checkpoint failed; inspect "+log);
        }
        File.WriteAllText(Path.Combine(directory,"result.json"),JsonSerializer.Serialize(new CheckpointResult(iteration,rows.Length,Hash(Path.Combine(root,"requirements/catalogue.json")),Hash(Path.Combine(root,"requirements/reviewed-sources.json")),DateTimeOffset.UtcNow,"passed","requirements/tooling/unchanged C++ baseline; no new runtime services implemented",results),TrackerJsonContext.Default.Checkpoint)+"\n");
    }
}
