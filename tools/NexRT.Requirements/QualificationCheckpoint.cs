using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;

namespace NexRT.Requirements;

/// <summary>Runs real ten-review build gates and creates scoped local milestone commits only after success.</summary>
internal static class QualificationCheckpoint
{
    /// <summary>Retains bounded subprocess logs, exact input hashes and pre-commit ancestry for one review boundary.</summary>
    internal static async Task Run(string root, int boundary)
    {
        string relative = $"requirements/qualification-100/checkpoints/{boundary:000}";
        string directory = Path.Combine(root, relative);
        if (Directory.Exists(directory))
        {
            int attempt = 1;
            while (Directory.Exists(directory + $"-attempt-{attempt:00}")) ++attempt;
            Directory.Move(directory, directory + $"-attempt-{attempt:00}");
        }
        Directory.CreateDirectory(directory);
        var manifest = new StringBuilder($"scope=requirements reviews and unchanged runtime regression\nreview_count={boundary}\nstatus=running\n");
        async Task<string> Execute(string label, string executable, params string[] arguments)
        {
            var start = new ProcessStartInfo(executable) { WorkingDirectory = root, RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true };
            foreach (string argument in arguments) start.ArgumentList.Add(argument);
            using var process = Process.Start(start) ?? throw new IOException("Cannot start " + executable);
            Task<string> output = process.StandardOutput.ReadToEndAsync(), error = process.StandardError.ReadToEndAsync();
            using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(5));
            try { await process.WaitForExitAsync(timeout.Token); }
            catch (OperationCanceledException) { process.Kill(true); await process.WaitForExitAsync(); throw new IOException(label + " exceeded five-minute gate deadline"); }
            string log = await output + await error;
            if (label is not ("stage" or "commit"))
            {
                string path = Path.Combine(directory, label + ".log");
                File.WriteAllText(path, log);
                manifest.AppendLine($"command={executable} {string.Join(' ', arguments)}\nexit={process.ExitCode}\nlog={relative}/{label}.log\nsha256={Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)))}");
                File.WriteAllText(Path.Combine(directory, "manifest.txt"), manifest.ToString());
            }
            if (process.ExitCode != 0) throw new IOException(label + " failed; see " + relative);
            Console.WriteLine($"Review boundary {boundary}: {label} passed");
            return log;
        }
        await Execute("parent", "git", "rev-parse", "HEAD");
        await Execute("host-build", "cmake", "--build", "build/host", "-j2");
        await Execute("host-tests", "ctest", "--test-dir", "build/host", "--output-on-failure");
        await Execute("firmware-build", "cmake", "--build", "build/overnight-stress", "-j2");
        await Execute("static-contract", "cmake", "--build", "build/overnight-stress", "--target", "static_contract_check");
        await Execute("documentation", "cmake", "--build", "build/host", "--target", "docs");
        foreach (string source in new[] { "requirements/catalogue.json", "requirements/nexrt.sdoc", "requirements/qualification-100/reviews.psv", "tools/NexRT.Requirements/Program.cs", "tools/NexRT.Requirements/QualificationCheckpoint.cs" })
            manifest.AppendLine($"input={source}\nsha256={Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(Path.Combine(root, source))))}");
        manifest.Replace("status=running\n", "status=passed\n");
        manifest.AppendLine("runtime_features_implemented=false\nhardware_qualification=false");
        File.WriteAllText(Path.Combine(directory, "manifest.txt"), manifest.ToString());
        await Execute("stage", "git", "add", "requirements/catalogue.json", "requirements/nexrt.sdoc", "requirements/traceability.csv", "requirements/review-iterations.csv", "requirements/reviewed-sources.json", "requirements/qualification-100", $"requirements/batch-{151+boundary:000}-{160+boundary:000}.psv", "tasks/qualification-100", "docs/qualification-roadmap.rst", "docs/index.rst", "docs/requirements-tracker.rst", "tools/NexRT.Requirements/Program.cs", "tools/NexRT.Requirements/QualificationCheckpoint.cs");
        await Execute("commit", "git", "-c", "user.name=Codex", "-c", "user.email=codex@local", "commit", "-m", $"Qualification reviews {boundary-9:000}-{boundary:000}: requirements, tasks and verified build gates");
    }
}
