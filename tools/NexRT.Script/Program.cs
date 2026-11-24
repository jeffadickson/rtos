using System.Globalization;
using System.Text;

namespace NexRT.Script;

/// <summary>Compiles lightweight NexRT diagnostic scripts into CRC-32C verified .nxs bytecode packages.</summary>
public static class Program
{
    private const uint PackageMagic = 0x3153584Eu; // "NXS1"
    private const ushort PackageVersion = 1;
    private const ushort FlagAllowPoke = 0x0001;
    private const ushort FlagAllowVaultWrite = 0x0002;
    private const int MaxRegisters = 14; // r0..r13 for user variables; r14, r15 compiler scratch
    private const byte TempA = 14;
    private const byte TempB = 15;

    private const byte OpHalt = 0x00;
    private const byte OpMovi = 0x01;
    private const byte OpLui = 0x02;
    private const byte OpOri = 0x03;
    private const byte OpMov = 0x04;
    private const byte OpAdd = 0x05;
    private const byte OpSub = 0x06;
    private const byte OpMul = 0x07;
    private const byte OpDiv = 0x08;
    private const byte OpAnd = 0x09;
    private const byte OpOr = 0x0A;
    private const byte OpXor = 0x0B;
    private const byte OpShl = 0x0C;
    private const byte OpShr = 0x0D;
    private const byte OpAddi = 0x0E;
    private const byte OpJmp = 0x10;
    private const byte OpBeq = 0x11;
    private const byte OpBne = 0x12;
    private const byte OpBlt = 0x13;
    private const byte OpBge = 0x14;
    private const byte OpSys = 0x20;

    private const ushort SysSensor = 1;
    private const ushort SysPeek = 2;
    private const ushort SysPoke = 3;
    private const ushort SysSleep = 4;
    private const ushort SysLog = 5;
    private const ushort SysVaultRead = 6;
    private const ushort SysVaultWrite = 7;
    private const ushort SysEmit = 8;

    /// <summary>CLI entry point supporting <c>compile</c> and <c>self-test</c> commands.</summary>
    public static int Main(string[] args)
    {
        try
        {
            if (args.Length == 0)
            {
                throw new ArgumentException("Usage: NexRT.Script compile <input> <output> [--fuel N] [--allow-poke] [--allow-vault-write] | self-test");
            }
            switch (args[0])
            {
                case "self-test" when args.Length == 1:
                    SelfTest();
                    return 0;
                case "compile" when args.Length >= 3:
                {
                    string inputPath = Path.GetFullPath(args[1]);
                    string outputPath = Path.GetFullPath(args[2]);
                    uint maxFuel = 1024;
                    ushort flags = 0;
                    for (int i = 3; i < args.Length; ++i)
                    {
                        if (args[i] == "--fuel" && i + 1 < args.Length)
                        {
                            maxFuel = uint.Parse(args[++i], CultureInfo.InvariantCulture);
                        }
                        else if (args[i] == "--allow-poke")
                        {
                            flags |= FlagAllowPoke;
                        }
                        else if (args[i] == "--allow-vault-write")
                        {
                            flags |= FlagAllowVaultWrite;
                        }
                        else
                        {
                            throw new ArgumentException($"Unknown option: {args[i]}");
                        }
                    }
                    string source = File.ReadAllText(inputPath);
                    byte[] package = Compile(source, maxFuel, flags);
                    Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
                    File.WriteAllBytes(outputPath + ".tmp", package);
                    File.Move(outputPath + ".tmp", outputPath, true);
                    Console.WriteLine($"Compiled {inputPath} -> {outputPath} ({package.Length} bytes, {(package.Length - 20) / 4} instructions, fuel={maxFuel}).");
                    return 0;
                }
                default:
                    throw new ArgumentException("Unknown command or invalid argument count.");
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"NexRT.Script error: {ex.Message}");
            return 1;
        }
    }

    /// <summary>Compiles diagnostic script source text into a signed .nxs binary package.</summary>
    public static byte[] Compile(string source, uint maxFuel = 1024, ushort flags = 0)
    {
        if (maxFuel == 0) throw new ArgumentException("maxFuel must be > 0.");

        var code = new List<uint>();
        var vars = new Dictionary<string, byte>(StringComparer.Ordinal);
        var labels = new Dictionary<string, int>(StringComparer.Ordinal);
        var fixups = new List<(int InsnIndex, byte Op, byte Rd, byte Ra, string TargetLabel, int LineNumber)>();

        byte AllocVar(string name, int lineNo)
        {
            if (!IsIdent(name)) throw new FormatException($"Line {lineNo}: invalid identifier '{name}'.");
            if (vars.TryGetValue(name, out byte existing)) return existing;
            if (vars.Count >= MaxRegisters) throw new InvalidOperationException($"Line {lineNo}: exceeded {MaxRegisters} local variables.");
            byte reg = (byte)vars.Count;
            vars[name] = reg;
            return reg;
        }

        byte LookupVar(string name, int lineNo)
        {
            if (vars.TryGetValue(name, out byte reg)) return reg;
            throw new FormatException($"Line {lineNo}: undefined variable '{name}'.");
        }

        void EmitConst(byte dst, int value)
        {
            if (value >= short.MinValue && value <= short.MaxValue)
            {
                code.Add(EncodeImm(OpMovi, dst, 0, (short)value));
            }
            else
            {
                uint u = unchecked((uint)value);
                ushort hi = (ushort)(u >> 16);
                ushort lo = (ushort)(u & 0xFFFFu);
                code.Add(EncodeImm(OpLui, dst, 0, unchecked((short)hi)));
                if (lo != 0)
                {
                    code.Add(EncodeImm(OpOri, dst, dst, unchecked((short)lo)));
                }
            }
        }

        byte LoadAtom(string token, byte scratchReg, int lineNo)
        {
            if (TryParseInt32(token, out int lit))
            {
                EmitConst(scratchReg, lit);
                return scratchReg;
            }
            return LookupVar(token, lineNo);
        }

        string[] lines = source.Replace("\r\n", "\n", StringComparison.Ordinal).Split('\n');
        for (int lineIdx = 0; lineIdx < lines.Length; ++lineIdx)
        {
            int lineNo = lineIdx + 1;
            string line = StripComment(lines[lineIdx]).Trim();
            if (line.Length == 0) continue;

            // Label declaration: "name:"
            if (line.EndsWith(':'))
            {
                string label = line[..^1].Trim();
                if (!IsIdent(label)) throw new FormatException($"Line {lineNo}: invalid label '{label}'.");
                if (labels.ContainsKey(label)) throw new FormatException($"Line {lineNo}: duplicate label '{label}'.");
                labels[label] = code.Count;
                continue;
            }

            // Unconditional jump: "goto label"
            if (line.StartsWith("goto ", StringComparison.Ordinal))
            {
                string target = line[5..].Trim();
                fixups.Add((code.Count, OpJmp, 0, 0, target, lineNo));
                code.Add(0);
                continue;
            }

            // Conditional branch: "if <lhs> <cmp> <rhs> goto <label>"
            if (line.StartsWith("if ", StringComparison.Ordinal))
            {
                var parts = SplitTokens(line);
                if (parts.Length != 6 || parts[0] != "if" || parts[4] != "goto")
                {
                    throw new FormatException($"Line {lineNo}: expected 'if <lhs> <cmp> <rhs> goto <label>'.");
                }
                byte lhs = LoadAtom(parts[1], TempA, lineNo);
                byte rhs = LoadAtom(parts[3], TempB, lineNo);
                string cmp = parts[2];
                string target = parts[5];
                switch (cmp)
                {
                    case "==": fixups.Add((code.Count, OpBeq, lhs, rhs, target, lineNo)); break;
                    case "!=": fixups.Add((code.Count, OpBne, lhs, rhs, target, lineNo)); break;
                    case "<":  fixups.Add((code.Count, OpBlt, lhs, rhs, target, lineNo)); break;
                    case ">=": fixups.Add((code.Count, OpBge, lhs, rhs, target, lineNo)); break;
                    case ">":  fixups.Add((code.Count, OpBlt, rhs, lhs, target, lineNo)); break;
                    case "<=": fixups.Add((code.Count, OpBge, rhs, lhs, target, lineNo)); break;
                    default: throw new FormatException($"Line {lineNo}: unknown comparison operator '{cmp}'.");
                }
                code.Add(0);
                continue;
            }

            // Return: "return <atom>"
            if (line == "return" || line.StartsWith("return ", StringComparison.Ordinal))
            {
                if (line == "return")
                {
                    EmitConst(TempA, 0);
                    code.Add(EncodeImm(OpHalt, TempA, 0, 0));
                }
                else
                {
                    string arg = line[7..].Trim();
                    byte r = LoadAtom(arg, TempA, lineNo);
                    code.Add(EncodeImm(OpHalt, r, 0, 0));
                }
                continue;
            }

            // Standalone syscall statements: poke(a, v), sleep(t), log(tag, v), vault_write(k, v), emit(ch, v)
            if (TryParseCall(line, out string stmtCall, out string[] stmtArgs))
            {
                EmitSyscall(stmtCall, stmtArgs, TempA, code, LoadAtom, ref flags, lineNo);
                continue;
            }

            // Variable declaration or assignment: "[let] name = expr"
            bool isLet = line.StartsWith("let ", StringComparison.Ordinal);
            string body = isLet ? line[4..].Trim() : line;
            int eq = body.IndexOf('=');
            if (eq <= 0)
            {
                throw new FormatException($"Line {lineNo}: unrecognized statement '{line}'.");
            }
            string lhsName = body[..eq].Trim();
            string rhsExpr = body[(eq + 1)..].Trim();
            byte dst = isLet ? AllocVar(lhsName, lineNo) : LookupVar(lhsName, lineNo);

            if (TryParseCall(rhsExpr, out string exprCall, out string[] exprArgs))
            {
                EmitSyscall(exprCall, exprArgs, dst, code, LoadAtom, ref flags, lineNo);
                continue;
            }

            var rhsTokens = SplitTokens(rhsExpr);
            if (rhsTokens.Length == 1)
            {
                if (TryParseInt32(rhsTokens[0], out int lit))
                {
                    EmitConst(dst, lit);
                }
                else
                {
                    byte src = LookupVar(rhsTokens[0], lineNo);
                    if (dst != src) code.Add(EncodeImm(OpMov, dst, src, 0));
                }
                continue;
            }

            if (rhsTokens.Length == 3)
            {
                string op = rhsTokens[1];
                if (op == "+" && !TryParseInt32(rhsTokens[0], out _) && TryParseInt32(rhsTokens[2], out int immAdd)
                    && immAdd >= short.MinValue && immAdd <= short.MaxValue)
                {
                    byte ra = LookupVar(rhsTokens[0], lineNo);
                    code.Add(EncodeImm(OpAddi, dst, ra, (short)immAdd));
                    continue;
                }
                if ((op == "<<" || op == ">>") && TryParseInt32(rhsTokens[2], out int shiftAmt))
                {
                    byte ra = LoadAtom(rhsTokens[0], TempA, lineNo);
                    code.Add(EncodeImm(op == "<<" ? OpShl : OpShr, dst, ra, (short)(shiftAmt & 31)));
                    continue;
                }
                byte a = LoadAtom(rhsTokens[0], TempA, lineNo);
                byte b = LoadAtom(rhsTokens[2], TempB, lineNo);
                byte binOp = op switch
                {
                    "+" => OpAdd,
                    "-" => OpSub,
                    "*" => OpMul,
                    "/" => OpDiv,
                    "&" => OpAnd,
                    "|" => OpOr,
                    "^" => OpXor,
                    _ => throw new FormatException($"Line {lineNo}: unsupported binary operator '{op}'.")
                };
                code.Add(EncodeReg(binOp, dst, a, b));
                continue;
            }

            throw new FormatException($"Line {lineNo}: unsupported expression '{rhsExpr}'.");
        }

        // Ensure implicit halt if script omitted trailing return.
        if (code.Count == 0 || ((code[^1] >> 24) & 0xFFu) != OpHalt)
        {
            code.Add(EncodeImm(OpHalt, 0, 0, 0));
        }

        if (code.Count > 1024)
        {
            throw new InvalidOperationException($"Compiled script exceeds 1024 instructions ({code.Count}).");
        }

        // Resolve label fixups.
        foreach (var f in fixups)
        {
            if (!labels.TryGetValue(f.TargetLabel, out int targetPc))
            {
                throw new FormatException($"Line {f.LineNumber}: undefined label '{f.TargetLabel}'.");
            }
            code[f.InsnIndex] = EncodeImm(f.Op, f.Rd, f.Ra, checked((short)targetPc));
        }

        return PackNxs(code, maxFuel, flags);
    }

    private static void EmitSyscall(
        string name, string[] args, byte dst, List<uint> code,
        Func<string, byte, int, byte> loadAtom, ref ushort flags, int lineNo)
    {
        switch (name)
        {
            case "sensor" when args.Length == 1:
            {
                byte a0 = loadAtom(args[0], TempA, lineNo);
                code.Add(EncodeImm(OpSys, dst, a0, (short)SysSensor));
                break;
            }
            case "peek" when args.Length == 1:
            {
                byte a0 = loadAtom(args[0], TempA, lineNo);
                code.Add(EncodeImm(OpSys, dst, a0, (short)SysPeek));
                break;
            }
            case "vault_read" when args.Length == 1:
            {
                byte a0 = loadAtom(args[0], TempA, lineNo);
                code.Add(EncodeImm(OpSys, dst, a0, (short)SysVaultRead));
                break;
            }
            case "sleep" when args.Length == 1:
            {
                byte a0 = loadAtom(args[0], TempA, lineNo);
                code.Add(EncodeImm(OpSys, dst, a0, (short)SysSleep));
                break;
            }
            case "poke" when args.Length == 2:
            {
                byte a0 = loadAtom(args[0], TempA, lineNo);
                byte a1 = loadAtom(args[1], TempB, lineNo);
                if (a1 != TempB) code.Add(EncodeImm(OpMov, TempB, a1, 0));
                code.Add(EncodeImm(OpSys, TempB, a0, (short)SysPoke));
                if (dst != TempA && dst != TempB) code.Add(EncodeImm(OpMov, dst, TempB, 0));
                break;
            }
            case "log" when args.Length == 2:
            {
                byte a0 = loadAtom(args[0], TempA, lineNo);
                byte a1 = loadAtom(args[1], TempB, lineNo);
                if (a1 != TempB) code.Add(EncodeImm(OpMov, TempB, a1, 0));
                code.Add(EncodeImm(OpSys, TempB, a0, (short)SysLog));
                if (dst != TempA && dst != TempB) code.Add(EncodeImm(OpMov, dst, TempB, 0));
                break;
            }
            case "vault_write" when args.Length == 2:
            {
                byte a0 = loadAtom(args[0], TempA, lineNo);
                byte a1 = loadAtom(args[1], TempB, lineNo);
                if (a1 != TempB) code.Add(EncodeImm(OpMov, TempB, a1, 0));
                code.Add(EncodeImm(OpSys, TempB, a0, (short)SysVaultWrite));
                if (dst != TempA && dst != TempB) code.Add(EncodeImm(OpMov, dst, TempB, 0));
                break;
            }
            case "emit" when args.Length == 2:
            {
                byte a0 = loadAtom(args[0], TempA, lineNo);
                byte a1 = loadAtom(args[1], TempB, lineNo);
                if (a1 != TempB) code.Add(EncodeImm(OpMov, TempB, a1, 0));
                code.Add(EncodeImm(OpSys, TempB, a0, (short)SysEmit));
                if (dst != TempA && dst != TempB) code.Add(EncodeImm(OpMov, dst, TempB, 0));
                break;
            }
            default:
                throw new FormatException($"Line {lineNo}: unknown syscall or wrong arity '{name}({args.Length} args)'.");
        }
        _ = flags;
    }

    private static byte[] PackNxs(List<uint> code, uint maxFuel, ushort flags)
    {
        byte[] buf = new byte[20 + code.Count * 4];
        WriteU32(buf, 0, PackageMagic);
        WriteU16(buf, 4, PackageVersion);
        WriteU16(buf, 6, flags);
        WriteU32(buf, 8, (uint)code.Count);
        WriteU32(buf, 12, maxFuel);
        WriteU32(buf, 16, 0);
        for (int i = 0; i < code.Count; ++i)
        {
            WriteU32(buf, 20 + i * 4, code[i]);
        }
        uint crc = 0xFFFFFFFFu;
        crc = Crc32cUpdate(crc, buf.AsSpan(0, 16));
        crc = Crc32cUpdate(crc, buf.AsSpan(20, code.Count * 4));
        WriteU32(buf, 16, ~crc);
        return buf;
    }

    /// <summary>Computes Castagnoli CRC-32C matching <c>rtos::script::crc32c</c>.</summary>
    public static uint Crc32cUpdate(uint crc, ReadOnlySpan<byte> data)
    {
        foreach (byte b in data)
        {
            crc ^= b;
            for (int i = 0; i < 8; ++i)
            {
                uint mask = unchecked((uint)-(int)(crc & 1u));
                crc = (crc >> 1) ^ (0x82F63B78u & mask);
            }
        }
        return crc;
    }

    private static uint EncodeImm(byte op, byte rd, byte ra, short imm) =>
        ((uint)op << 24) | (((uint)rd & 0xFu) << 20) | (((uint)ra & 0xFu) << 16) | unchecked((ushort)imm);

    private static uint EncodeReg(byte op, byte rd, byte ra, byte rb) =>
        ((uint)op << 24) | (((uint)rd & 0xFu) << 20) | (((uint)ra & 0xFu) << 16) | (((uint)rb & 0xFu) << 12);

    private static void WriteU16(byte[] buf, int offset, ushort v)
    {
        buf[offset] = (byte)(v & 0xFFu);
        buf[offset + 1] = (byte)((v >> 8) & 0xFFu);
    }

    private static void WriteU32(byte[] buf, int offset, uint v)
    {
        buf[offset] = (byte)(v & 0xFFu);
        buf[offset + 1] = (byte)((v >> 8) & 0xFFu);
        buf[offset + 2] = (byte)((v >> 16) & 0xFFu);
        buf[offset + 3] = (byte)((v >> 24) & 0xFFu);
    }

    private static string StripComment(string line)
    {
        int hash = line.IndexOf('#');
        int slash = line.IndexOf("//", StringComparison.Ordinal);
        int cut = line.Length;
        if (hash >= 0 && hash < cut) cut = hash;
        if (slash >= 0 && slash < cut) cut = slash;
        return line[..cut];
    }

    private static bool IsIdent(string s)
    {
        if (s.Length == 0 || (!char.IsAsciiLetter(s[0]) && s[0] != '_')) return false;
        foreach (char c in s)
        {
            if (!char.IsAsciiLetterOrDigit(c) && c != '_') return false;
        }
        return true;
    }

    private static bool TryParseInt32(string s, out int value)
    {
        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            if (uint.TryParse(s[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint u))
            {
                value = unchecked((int)u);
                return true;
            }
            value = 0;
            return false;
        }
        return int.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out value);
    }

    private static string[] SplitTokens(string expr) =>
        expr.Split([' ', '\t'], StringSplitOptions.RemoveEmptyEntries);

    private static bool TryParseCall(string text, out string name, out string[] args)
    {
        name = "";
        args = [];
        int open = text.IndexOf('(');
        if (open <= 0 || !text.EndsWith(')')) return false;
        string fn = text[..open].Trim();
        if (!IsIdent(fn)) return false;
        string inner = text[(open + 1)..^1].Trim();
        name = fn;
        args = inner.Length == 0 ? [] : inner.Split(',').Select(a => a.Trim()).ToArray();
        return true;
    }

    private static void SelfTest()
    {
        const string sample = """
            # Average 5 readings from sensor 0 and emit on channel 10
            let sum = 0
            let i = 0
            let addr = 0x40001000
            let status = peek(addr)
            loop:
              let v = sensor(0)
              sum = sum + v
              if v < 3100 goto low
              goto next
            low:
              log(1, v)
            next:
              i = i + 1
              if i < 5 goto loop
            let avg = sum / 5
            emit(10, avg)
            return avg
            """;

        byte[] pkg = Compile(sample, maxFuel: 512, flags: 0);
        if (pkg.Length <= 20) throw new InvalidOperationException("Package too short.");
        uint magic = BitConverter.ToUInt32(pkg, 0);
        if (magic != PackageMagic) throw new InvalidOperationException("Bad magic.");
        uint words = BitConverter.ToUInt32(pkg, 8);
        if (pkg.Length != 20 + words * 4) throw new InvalidOperationException("Length mismatch.");
        uint storedCrc = BitConverter.ToUInt32(pkg, 16);
        uint crc = Crc32cUpdate(0xFFFFFFFFu, pkg.AsSpan(0, 16));
        crc = Crc32cUpdate(crc, pkg.AsSpan(20));
        if (~crc != storedCrc) throw new InvalidOperationException("CRC-32C verification failed.");

        // Negative tests: undefined label and undefined variable must fail cleanly.
        void ExpectFail(string badSrc)
        {
            try
            {
                _ = Compile(badSrc);
            }
            catch (FormatException)
            {
                return;
            }
            catch (InvalidOperationException)
            {
                return;
            }
            throw new InvalidOperationException($"Bad script was unexpectedly accepted: {badSrc}");
        }

        ExpectFail("goto nowhere");
        ExpectFail("x = 5");
        ExpectFail("let a = unknown_call(1)");
        ExpectFail("if 1 === 2 goto start");

        Console.WriteLine($"PASS: NexRT.Script self-test ({words} instructions, {pkg.Length} bytes, CRC-32C=0x{storedCrc:x8}).");
    }
}
