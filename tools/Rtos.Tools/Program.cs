using System.Diagnostics;
using System.Text.RegularExpressions;

namespace Rtos.Tools;

/// <summary>Runs static-storage firmware audits and compiler contract checks without a shell.</summary>
internal static class Program
{
    /// <summary>Names forbidden by the project's static-only firmware policy.</summary>
    private static readonly Regex Forbidden = new(
        @"\b(malloc|calloc|realloc|free|aligned_alloc|sbrk|_sbrk|__cxa_guard_acquire|__cxa_atexit)\b|operator (new|delete)|_GLOBAL__sub_I_|__static_initialization_and_destruction",
        RegexOptions.CultureInvariant, TimeSpan.FromSeconds(1));

    /// <summary>Dispatches audit, static-contract, or self-test commands.</summary>
    /// <param name="args">Command followed by explicit --name value pairs.</param>
    /// <returns>Zero on success; one on rejected input, policy violation, or tool failure.</returns>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if (args.Length == 0) throw new ArgumentException("Expected audit, static-contract, or self-test.");
            var options = ParseOptions(args);
            switch (args[0])
            {
                case "audit":
                    return await Audit(Required(options, "nm"), Required(options, "elf"));
                case "static-contract":
                    await CheckContract(Required(options, "cxx"), Required(options, "nm"),
                        Required(options, "source-root"), Required(options, "build-dir"));
                    return 0;
                case "self-test":
                    SelfTest();
                    return 0;
                default: throw new ArgumentException($"Unknown command: {args[0]}");
            }
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.Message);
            return 1;
        }
    }

    /// <summary>Parses options without evaluating shell syntax or accepting duplicate keys.</summary>
    private static Dictionary<string, string> ParseOptions(string[] args)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        for (int i = 1; i < args.Length; i += 2)
        {
            if (!args[i].StartsWith("--", StringComparison.Ordinal) || i + 1 >= args.Length ||
                !result.TryAdd(args[i][2..], args[i + 1]))
                throw new ArgumentException("Options must be unique --name value pairs.");
        }
        return result;
    }

    /// <summary>Returns a mandatory, nonempty option or reports a command-line error.</summary>
    private static string Required(Dictionary<string, string> options, string name) =>
        options.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value : throw new ArgumentException($"Missing --{name}.");

    /// <summary>Captures a child process result while draining both redirected streams concurrently.</summary>
    private sealed record Result(int ExitCode, string Output, string Error);

    /// <summary>Runs an executable directly; every argument remains a separate literal argument.</summary>
    private static async Task<Result> Run(string executable, IEnumerable<string> arguments)
    {
        var start = new ProcessStartInfo(executable)
        {
            UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true,
            CreateNoWindow = true
        };
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        using var process = Process.Start(start) ?? throw new InvalidOperationException($"Could not start {executable}.");
        var output = process.StandardOutput.ReadToEndAsync();
        var error = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        return new Result(process.ExitCode, await output, await error);
    }

    /// <summary>Finds known heap and dynamic-initialization symbols in demangled nm output.</summary>
    private static string[] Violations(string symbols) => symbols.Split('\n').Where(line => Forbidden.IsMatch(line)).ToArray();

    /// <summary>Checks an image and preserves failures from nm rather than treating empty output as a pass.</summary>
    private static async Task<int> Audit(string nm, string elf)
    {
        var result = await Run(nm, ["-C", elf]);
        if (result.ExitCode != 0) throw new InvalidOperationException($"nm failed ({result.ExitCode}): {result.Error}");
        var bad = Violations(result.Output);
        if (bad.Length != 0)
        {
            Console.Error.WriteLine("Static-only ELF audit failed:\n" + string.Join('\n', bad));
            return 1;
        }
        Console.WriteLine("Static-only ELF audit passed: no allocator or dynamic C++ initializer symbols");
        return 0;
    }

    /// <summary>Compiles real positive/negative images to verify constructor and allocator rejection.</summary>
    private static async Task CheckContract(string cxx, string nm, string root, string directory)
    {
        Directory.CreateDirectory(directory);
        string[] flags = ["-march=rv32im_zicsr", "-mabi=ilp32", "-mno-relax", "-msmall-data-limit=0",
            "-ffreestanding", "-fno-builtin", "-fno-exceptions", "-fno-rtti", "-nostdlib",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections,--no-relax",
            "-T" + Path.Combine(root, "ports", "riscue", "link.ld")];
        (string Name, string Source)[] fixtures = [
            ("constant", "int value=3; extern \"C\" void _start() { for (;;) asm volatile(\"\" ::: \"memory\"); }"),
            ("constructor", "volatile int input; int read() { return input; } int value=read(); extern \"C\" void _start() { for (;;) {} }"),
            ("allocator", "extern \"C\" __attribute__((noinline)) void* malloc(unsigned) { static char pool[16]; return pool; } extern \"C\" void _start() { void* p=malloc(4); asm volatile(\"\" :: \"r\"(p) : \"memory\"); for (;;) {} }")
        ];
        foreach (var fixture in fixtures)
        {
            var source = Path.Combine(directory, fixture.Name + ".cpp");
            var elf = Path.Combine(directory, fixture.Name + ".elf");
            await File.WriteAllTextAsync(source, fixture.Source);
            var compile = await Run(cxx, flags.Concat([source, "-o", elf]));
            if (fixture.Name == "constructor")
            {
                if (compile.ExitCode == 0 || !compile.Error.Contains("Runtime C++ constructors are unsupported", StringComparison.Ordinal))
                    throw new InvalidOperationException("Constructor rejection check failed:\n" + compile.Output + compile.Error);
                Console.WriteLine("PASS: runtime constructor rejected by linker, even with section GC");
                continue;
            }
            if (compile.ExitCode != 0) throw new InvalidOperationException(compile.Output + compile.Error);
            var symbols = await Run(nm, ["-C", elf]);
            if (symbols.ExitCode != 0) throw new InvalidOperationException("nm failed: " + symbols.Error);
            var bad = Violations(symbols.Output);
            if (fixture.Name == "constant" && bad.Length != 0)
                throw new InvalidOperationException("Constant initialization unexpectedly rejected.");
            if (fixture.Name == "allocator" && !bad.Any(line => line.Contains("malloc", StringComparison.Ordinal)))
                throw new InvalidOperationException("Reachable allocator was not rejected.");
            Console.WriteLine(fixture.Name == "constant" ? "PASS: constant initialization accepted" : "PASS: reachable allocator rejected by ELF audit");
        }
    }

    /// <summary>Checks audit boundary cases and command parsing without external dependencies.</summary>
    private static void SelfTest()
    {
        string[] rejected = ["0000 T malloc", "U free", "T operator new(unsigned long)",
            "T operator delete[](void*)", "t _GLOBAL__sub_I_main", "U __cxa_guard_acquire"];
        foreach (var symbol in rejected)
            if (Violations(symbol).Length != 1) throw new InvalidOperationException("Missed forbidden symbol: " + symbol);
        if (Violations("T my_malloc_counter\nT free_slots\nT rtos::snapshot()\nT memset").Length != 0)
            throw new InvalidOperationException("Audit rejected a permitted symbol.");
        var parsed = ParseOptions(["audit", "--elf", "a folder/a file.elf", "--nm", "nm"]);
        if (Required(parsed, "elf") != "a folder/a file.elf") throw new InvalidOperationException("Path parsing failed.");
        try { ParseOptions(["audit", "--elf", "a", "--elf", "b"]); }
        catch (ArgumentException) { Console.WriteLine("PASS: audit symbol boundaries, spaced paths, duplicate-option rejection"); return; }
        throw new InvalidOperationException("Duplicate option accepted.");
    }
}
