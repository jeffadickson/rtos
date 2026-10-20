using System.Diagnostics;
using System.Text.RegularExpressions;
using NexRT.Host;

namespace Rtos.Tools;

/// <summary>Runs static-storage firmware audits and compiler contract checks without a shell.</summary>
internal static class Program
{
    /// <summary>Names forbidden by the project's static-only firmware policy.</summary>
    private static readonly Regex Forbidden = new(
        @"\b(malloc|calloc|realloc|free|aligned_alloc|memalign|valloc|pvalloc|sbrk|_sbrk|_malloc_r|_calloc_r|_realloc_r|_free_r|_memalign_r|_sbrk_r|__cxa_guard_acquire|__cxa_atexit)\b|operator (new|delete)|_GLOBAL__sub_I[._]|__cxx_global_var_init|__static_initialization_and_destruction",
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
                        Required(options, "source-root"), Required(options, "build-dir"),
                        options.GetValueOrDefault("architecture","riscue"));
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
        var result=await HostProcess.Run(start,TimeSpan.FromSeconds(120));
        if(result.TimedOut||result.OutputLimit||result.IoError is not null)
            throw new IOException($"Tool execution/capture failed: {executable}\n{result.Output}");
        return new Result(result.ExitCode,result.Output,"");
    }

    /// <summary>Finds known heap and dynamic-initialization symbols in demangled nm output.</summary>
    private static string[] Violations(string symbols) => symbols.Split('\n').Where(line => Forbidden.IsMatch(line)).ToArray();

    /// <summary>Requires a defined symbol; stripped/empty listings cannot support the name audit.</summary>
    private static bool HasDefinedSymbols(string symbols)=>Regex.IsMatch(symbols,
        @"(?m)^[ \t]*[0-9a-fA-F]+[ \t]+[A-Za-z][ \t]+\S",RegexOptions.CultureInvariant,TimeSpan.FromSeconds(1));

    /// <summary>Checks an image and preserves failures from nm rather than treating empty output as a pass.</summary>
    private static async Task<int> Audit(string nm, string elf)
    {
        var result = await Run(nm, ["-C", elf]);
        if (result.ExitCode != 0) throw new InvalidOperationException($"nm failed ({result.ExitCode}): {result.Error}");
        if(!HasDefinedSymbols(result.Output))throw new IOException("Static-only audit requires an unstripped image with defined symbols");
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
    private static async Task CheckContract(string cxx, string nm, string root, string directory,string architecture)
    {
        Directory.CreateDirectory(directory);
        bool arm=architecture=="qemu-mps2-m3";
        if(!arm&&architecture!="riscue")throw new ArgumentException("Unsupported static-contract architecture");
        string[] isa=arm?["-mcpu=cortex-m3","-mthumb","-mfloat-abi=soft"]:
            ["-march=rv32im_zicsr", "-mabi=ilp32", "-mno-relax", "-msmall-data-limit=0"];
        string[] flags = [..isa,
            "-ffreestanding", "-fno-builtin", "-fno-exceptions", "-fno-rtti", "-nostdlib",
            "-ffunction-sections", "-fdata-sections",arm?"-Wl,--gc-sections":"-Wl,--gc-sections,--no-relax",
            "-T" + Path.Combine(root, "ports",arm?"arm-mps2":"riscue", "link.ld")];
        const string entry="extern \"C\" void _start() { for (;;) asm volatile(\"\" ::: \"memory\"); }";
        (string Name,string Source,string? Rejection)[] fixtures = [
            ("constant", "int value=3; "+entry,null),
            ("stripped",entry,null),
            ("constructor", "volatile int input; int read() { return input; } int value=read(); "+entry,"Runtime C++ constructors are unsupported"),
            ("priority-constructor","volatile int input; struct Object{Object(){input=7;}}; Object object __attribute__((init_priority(200))); "+entry,"Runtime C++ constructors are unsupported"),
            ("priority-init-section","void init(){}; __attribute__((used,section(\".init_array.00200\"))) void(*hook)()=init; "+entry,"Runtime C++ constructors are unsupported"),
            ("preinit-section","void init(){}; __attribute__((used,section(\".preinit_array\"))) void(*hook)()=init; "+entry,"Runtime preinitialization is unsupported"),
            ("priority-preinit-section","void init(){}; __attribute__((used,section(\".preinit_array.00200\"))) void(*hook)()=init; "+entry,"Runtime preinitialization is unsupported"),
            ("fini-section","void fini(){}; __attribute__((used,section(\".fini_array.00200\"))) void(*hook)()=fini; "+entry,"Runtime finalization is unsupported"),
            ("allocator", "extern \"C\" __attribute__((noinline)) void* malloc(unsigned) { static char pool[16]; return pool; } extern \"C\" void _start() { void* p=malloc(4); asm volatile(\"\" :: \"r\"(p) : \"memory\"); for (;;) {} }",null)
        ];
        foreach (var fixture in fixtures)
        {
            var source = Path.Combine(directory, fixture.Name + ".cpp");
            var elf = Path.Combine(directory, fixture.Name + ".elf");
            await File.WriteAllTextAsync(source,arm?fixture.Source.Replace("_start","Reset_Handler",StringComparison.Ordinal):fixture.Source);
            var compile = await Run(cxx, flags.Concat(fixture.Name=="stripped"?["-s",source,"-o",elf]:[source,"-o",elf]));
            if (fixture.Rejection is not null)
            {
                if (compile.ExitCode == 0 || !(compile.Output+compile.Error).Contains(fixture.Rejection, StringComparison.Ordinal))
                    throw new InvalidOperationException("Constructor rejection check failed:\n" + compile.Output + compile.Error);
                Console.WriteLine("PASS: "+fixture.Name+" rejected by linker, even with section GC");
                continue;
            }
            if (compile.ExitCode != 0) throw new InvalidOperationException(compile.Output + compile.Error);
            var symbols = await Run(nm, ["-C", elf]);
            if (symbols.ExitCode != 0) throw new InvalidOperationException("nm failed: " + symbols.Error);
            if(fixture.Name=="stripped"){
                if(HasDefinedSymbols(symbols.Output))throw new IOException("Stripped fixture retained defined symbols");
                Console.WriteLine("PASS: stripped image rejected as unauditable");continue;
            }
            if(!HasDefinedSymbols(symbols.Output))throw new IOException("Compiler fixture produced no defined symbols");
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
        if(HasDefinedSymbols("")||HasDefinedSymbols(" U memcpy\n U memset")||
           !HasDefinedSymbols("00000000 T Reset_Handler\n00000100 b storage"))
            throw new IOException("Defined-symbol presence boundary failed");
        string[] rejected = ["0000 T malloc", "U free", "T operator new(unsigned long)",
            "T operator delete[](void*)", "t _GLOBAL__sub_I_main", "t _GLOBAL__sub_I.00200_main", "t __cxx_global_var_init", "U __cxa_guard_acquire",
            "U _malloc_r","U _calloc_r","U _realloc_r","U _free_r","U _memalign_r","U _sbrk_r","U aligned_alloc","U memalign","U valloc","U pvalloc"];
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
