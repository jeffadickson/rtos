using System.Runtime.InteropServices;
using System.Text;
using SharpTestBench;

/// <summary>Runs actual RISCue firmware and checks bytes decoded from the UART pin.</summary>
public static partial class Entry
{
    /// <summary>Native bridge entry; discovery and execution stay on the simulator thread.</summary>
    [UnmanagedCallersOnly]
    public static int Initialize(nint pointer,int length)=>Regression.Initialize(typeof(Entry).Assembly);

    /// <summary>Checks repeated arithmetic, whole records, all three workers and timer preemption.</summary>
    [SimTest(TimeoutSteps=2_000_000_000)]
    public static async Task ThreeWorkers(TestContext context)
    {
        var tx=context.Dut.Child("uart_tx").Signal<W1>();
        var fatal=context.Dut.Child("fatal").Signal<W1>();
        var bad=context.Dut.Child("bad_traps").Signal<W32>();
        var timers=context.Dut.Child("timer_traps").Signal<W32>();
        var pc=context.Dut.Child("pc").Signal<W32>();
        uint[] rounds=new uint[3],values=[0x12340001,0x12340002,0x12340003];
        var line=new StringBuilder();
        using var output=new StreamWriter(Environment.GetEnvironmentVariable("NEXRT_UART_LOG")!);
        var watchdog=context.StartSoon(async cancellation=>
        {
            for(;;)
            {
                await Triggers.Timer(100_000).WithCancellation(cancellation);
                await Triggers.ReadOnly();
                if(fatal.ReadBit()||bad.ReadUInt().Value!=0)
                    throw new Exception($"CPU fault: pc={pc.ReadUInt().Value:x} fatal={fatal.ReadBit()} bad_traps={bad.ReadUInt().Value}");
            }
        });
        while(rounds.Any(round=>round<3))
        {
            try{await Triggers.WithTimeout(Triggers.FallingEdge(tx),100_000_000);}
            catch(TimeoutException error)
            {
                throw new Exception($"UART stalled: pc={pc.ReadUInt().Value:x} fatal={fatal.ReadBit()} bad_traps={bad.ReadUInt().Value} timers={timers.ReadUInt().Value}",error);
            }
            await Triggers.Timer(40); // Half start bit; 8 clocks/bit, 10 ns/clock.
            if(tx.ReadBit())throw new Exception("UART false start");
            int character=0;
            for(int bit=0;bit<8;bit++)
            {
                await Triggers.Timer(80);
                if(tx.ReadBit())character|=1<<bit;
            }
            await Triggers.Timer(80);
            await Triggers.ReadOnly();
            if(!tx.ReadBit())throw new Exception("UART stop-bit error");
            if(fatal.ReadBit()||bad.ReadUInt().Value!=0)throw new Exception("RISCue fatal/unexpected trap");
            if(character!='\n')
            {
                if(character<32||character>126||line.Length>=80)throw new Exception("Malformed/interleaved UART record");
                line.Append((char)character);continue;
            }
            string text=line.ToString();line.Clear();
            string[] fields=text.Split(' ');
            uint Field(int at,string name)
            {
                if(fields.Length!=7||!fields[at].StartsWith(name+"=",StringComparison.Ordinal)||
                   !uint.TryParse(fields[at].AsSpan(name.Length+1),out uint value))throw new Exception("Bad record: "+text);
                return value;
            }
            if(fields.Length!=7||fields[0]!="hello"||fields[1]!="from"||fields[2]!="thread"||!uint.TryParse(fields[3],out uint id))throw new Exception("Bad greeting: "+text);
            uint round=Field(4,"round"),actual=Field(5,"value"),mask=Field(6,"mask");
            if(id is <1 or >3||round!=rounds[id-1]+1||mask!=7)throw new Exception("Worker order/preemption mismatch: "+text);
            uint expected=values[id-1];
            for(int i=0;i<(round==1?30000:4096);i++)expected=unchecked(expected*1664525+1013904223+id);
            if(actual!=expected)throw new Exception("Register/arithmetic mismatch: "+text);
            rounds[id-1]=round;values[id-1]=expected;
            output.WriteLine(text);output.Flush();context.Log(text);
        }
        if(timers.ReadUInt().Value<3)throw new Exception("Missing machine timer interrupts");
        watchdog.Cancel();
        context.Log($"PASS: three workers, independent arithmetic, UART 8N1; timer traps={timers.ReadUInt().Value}");
    }
}
