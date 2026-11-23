using System.Diagnostics;
using System.Numerics;
using NexRT.Host;
using NexRTSchedulerProof;

namespace NexRT.FormalOracle;

/// <summary>
/// Closes the formal Proof-to-Silicon loop: runs the Z3-verified Dafny-translated C#
/// scheduler (<c>NexRTSchedulerProof.__default.Pick</c> and <c>MoveToBack</c>) in lockstep
/// against live arbitration traces emitted by the Verilated SystemVerilog hardware
/// scheduler (<c>rtl/nexrt_hw_sched.sv</c>).
/// </summary>
internal static class Program
{
    /// <summary>Executes the Verilated RTL trace and verifies every arbitration step against Dafny.</summary>
    /// <param name="args">Single argument: path to the Verilated <c>Vnexrt_hw_sched</c> binary.</param>
    /// <returns>Zero when every RTL step matches the Z3-proved Dafny oracle; non-zero on mismatch.</returns>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if (args.Length != 1)
            {
                throw new ArgumentException("Usage: NexRT.FormalOracle <path-to-Vnexrt_hw_sched>");
            }

            var psi = new ProcessStartInfo(args[0])
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };
            psi.ArgumentList.Add("--trace");

            var run = await HostProcess.Run(psi, TimeSpan.FromSeconds(120));
            if (run.ExitCode != 0 || run.TimedOut || run.OutputLimit || run.IoError is not null)
            {
                Console.Error.WriteLine($"Verilator trace process failed (exit {run.ExitCode}): {run.Output}");
                return 1;
            }

            const int threadCount = 16;
            BigInteger n = new(threadCount);
            Dafny.ISequence<BigInteger> order = Dafny.Sequence<BigInteger>.FromArray(
                Enumerable.Range(0, threadCount).Select(i => new BigInteger(i)).ToArray());

            int pickChecks = 0;
            int rotateSteps = 0;
            bool sawVerilatorPass = false;

            foreach (var rawLine in run.Output.Split('\n', StringSplitOptions.RemoveEmptyEntries))
            {
                var line = rawLine.Trim();
                if (line.StartsWith("HW_SCHED_VERILATOR_PASS", StringComparison.Ordinal))
                {
                    sawVerilatorPass = true;
                    continue;
                }

                if (line.StartsWith("ROTATE ", StringComparison.Ordinal))
                {
                    int slot = int.Parse(line.AsSpan(7));
                    order = __default.MoveToBack(order, n, new BigInteger(slot));
                    rotateSteps++;
                    continue;
                }

                if (line.StartsWith("PICK ", StringComparison.Ordinal))
                {
                    var tokens = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                    if (tokens.Length != 1 + threadCount + 2)
                    {
                        throw new InvalidDataException($"Malformed PICK trace line: {line}");
                    }

                    var states = new _IThreadState[threadCount];
                    var prios = new BigInteger[threadCount];
                    for (int t = 0; t < threadCount; t++)
                    {
                        var pair = tokens[1 + t].Split(':');
                        states[t] = DecodeState(int.Parse(pair[0]));
                        prios[t] = new BigInteger(int.Parse(pair[1]));
                    }

                    bool rtlValid = int.Parse(tokens[1 + threadCount]) != 0;
                    int rtlTid = int.Parse(tokens[1 + threadCount + 1]);

                    __default.Pick(
                        order,
                        Dafny.Sequence<_IThreadState>.FromArray(states),
                        Dafny.Sequence<BigInteger>.FromArray(prios),
                        n,
                        out bool dafnyFound,
                        out BigInteger dafnyChosen,
                        out _);

                    if (dafnyFound != rtlValid || (dafnyFound && (int)dafnyChosen != rtlTid))
                    {
                        Console.Error.WriteLine(
                            $"DAFNY-RTL MISMATCH at step {pickChecks}: " +
                            $"Dafny=({dafnyFound},{dafnyChosen}) RTL=({rtlValid},{rtlTid})");
                        return 1;
                    }

                    pickChecks++;
                }
            }

            if (!sawVerilatorPass || pickChecks < 200 || rotateSteps == 0)
            {
                Console.Error.WriteLine(
                    $"Insufficient formal trace coverage: pass={sawVerilatorPass}, picks={pickChecks}, rotates={rotateSteps}");
                return 1;
            }

            Console.WriteLine(
                $"DAFNY_RTL_LOCKSTEP_PASS verified_picks={pickChecks} verified_rotations={rotateSteps} oracle=SchedulerProof.dfy(Z3)");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex.Message);
            return 1;
        }
    }

    /// <summary>Maps NexRT hardware state encodings (0..5) to Z3-verified Dafny ThreadState values.</summary>
    private static _IThreadState DecodeState(int code) => code switch
    {
        0 => NexRTSchedulerProof.ThreadState.create_Unused(),
        1 => NexRTSchedulerProof.ThreadState.create_Ready(),
        2 => NexRTSchedulerProof.ThreadState.create_Sleeping(),
        3 => NexRTSchedulerProof.ThreadState.create_Finished(),
        4 => NexRTSchedulerProof.ThreadState.create_Blocked(),
        5 => NexRTSchedulerProof.ThreadState.create_Quarantined(),
        _ => throw new ArgumentOutOfRangeException(nameof(code), $"Unknown thread state: {code}")
    };
}
