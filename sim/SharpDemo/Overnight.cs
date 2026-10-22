using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using SharpTestBench;

/// <summary>Reads the real soak firmware's status through a simulation-only RAM probe.</summary>
public static partial class Entry
{
    /// <summary>Checks real target progress; host cutoff is explicitly incomplete, never a completed soak.</summary>
    [SimTest(TimeoutSteps=1_000_000_000_000_000_000)]
    public static async Task Overnight(TestContext context)
    {
        var address=context.Dut.Child("monitor_address").Signal<W32>();
        var data=context.Dut.Child("monitor_data").Signal<W32>();
        var fatal=context.Dut.Child("fatal").Signal<W1>();
        var bad=context.Dut.Child("bad_traps").Signal<W32>();
        var timers=context.Dut.Child("timer_traps").Signal<W32>();
        var pc=context.Dut.Child("pc").Signal<W32>();
        uint location=uint.Parse(Environment.GetEnvironmentVariable("NEXRT_SOAK_ADDRESS")!,NumberStyles.HexNumber,CultureInfo.InvariantCulture);
        int limit=int.Parse(Environment.GetEnvironmentVariable("NEXRT_SOAK_HOST_SECONDS")!,CultureInfo.InvariantCulture);
        bool smoke=Environment.GetEnvironmentVariable("NEXRT_SOAK_MODE")=="smoke";
        string run=Environment.GetEnvironmentVariable("NEXRT_SOAK_RUN")!;
        using var log=new StreamWriter(Path.Combine(run,"progress.jsonl")){AutoFlush=true};
        var wall=Stopwatch.StartNew();double nextLog=0,lastTimerProgress=0;uint lastTimers=0;
        uint[] words=new uint[57];
        void Publish(string outcome)
        {
            string json=JsonSerializer.Serialize(new {outcome,wallSeconds=wall.Elapsed.TotalSeconds,
                phase=words[0],seed=words[1],failure=words[2],failedWorker=words[3],failedSequence=words[4],
                targetTicks=words[5],jobs=words[6],modes=words[7..10],workerJobs=words[10..14],
                receiveTimeouts=words[14..18],busyPercent=words[18],dispatches=words[19..25],
                runtimeLow=words[25..31],stackEstimates=words[31..37],states=words[37..43],
                priorities=words[43..49],effectivePriorities=words[49..55],
                expectedTotal=words[55],observedTotal=words[56],timerTraps=checked((uint)timers.ReadUInt().Value),
                pc=checked((uint)pc.ReadUInt().Value)});
            log.WriteLine(json);
            string temporary=Path.Combine(run,"status.json.tmp");File.WriteAllText(temporary,json+"\n");
            File.Move(temporary,Path.Combine(run,"status.json"),true);
        }
        try
        {
            for(;;)
            {
                await Triggers.Timer(100_000);
                // Reads are sequential: running samples are approximate. A terminal phase is stable.
                for(int i=0;i<words.Length;++i)
                {
                    address.Write(new UInt<W32>(location+(uint)i*4));
                    await Triggers.Timer(10);await Triggers.ReadOnly();words[i]=checked((uint)data.ReadUInt().Value);
                    await Triggers.Timer(1); // Leave read-only phase before driving the next address.
                }
                if(fatal.ReadBit()||bad.ReadUInt().Value!=0)throw new IOException("CPU fatal or unexpected trap");
                if(words[0]==3)throw new IOException($"Firmware failure={words[2]} worker={words[3]} sequence={words[4]}");
                if(words[0]>3)throw new IOException("Invalid firmware phase");
                uint timerCount=checked((uint)timers.ReadUInt().Value);
                if(timerCount!=lastTimers){lastTimers=timerCount;lastTimerProgress=wall.Elapsed.TotalSeconds;}
                if(wall.Elapsed.TotalSeconds-lastTimerProgress>180)throw new IOException("No timer-trap progress for 180 host seconds");
                if(words[0]==2)
                {
                    if(words[2]!=0||words[6]==0||words[10..14].Any(value=>value==0)||
                       words[7..10].Any(value=>value==0)||words[55]!=words[56]||timerCount<3)
                        throw new IOException("Incomplete/inconsistent terminal firmware status");
                    Publish("completed");return;
                }
                if(smoke&&words[0]==1&&words[6]>=8&&words[10..14].All(value=>value>0)&&
                   words[19..25].All(value=>value>0)&&timerCount>=3)
                {Publish("smoke_passed");return;}
                if(wall.Elapsed.TotalSeconds>=limit)
                {
                    if(smoke)throw new TimeoutException("Smoke did not observe every worker completing jobs within its host bound");
                    Publish("wall_limit_reached");return;
                }
                if(wall.Elapsed.TotalSeconds>=nextLog){Publish("running");nextLog=wall.Elapsed.TotalSeconds+10;}
            }
        }
        catch(Exception error)
        {
            Publish("failed");File.WriteAllText(Path.Combine(run,"failure.txt"),error.ToString());throw;
        }
    }
}
