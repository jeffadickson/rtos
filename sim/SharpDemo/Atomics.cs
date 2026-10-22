using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using SharpTestBench;

/// <summary>Matched baseline/Zaamo real-kernel completion and atomic-history checks.</summary>
public static partial class Entry
{
    /// <summary>Requires complete firmware success and actual hardware AMO activity only when enabled.</summary>
    [SimTest(TimeoutSteps=20_000_000_000)]
    public static async Task Atomics(TestContext context)
    {
        var address=context.Dut.Child("monitor_address").Signal<W32>();
        var data=context.Dut.Child("monitor_data").Signal<W32>();
        var fatal=context.Dut.Child("fatal").Signal<W1>();
        var bad=context.Dut.Child("bad_traps").Signal<W32>();
        var timers=context.Dut.Child("timer_traps").Signal<W32>();
        var requests=context.Dut.Child("amo_requests").Signal<W32>();
        uint location=uint.Parse(Environment.GetEnvironmentVariable("NEXRT_SOAK_ADDRESS")!,NumberStyles.HexNumber,CultureInfo.InvariantCulture);
        uint feature=uint.Parse(Environment.GetEnvironmentVariable("NEXRT_ATOMIC_ENABLED")!,CultureInfo.InvariantCulture);
        int limit=int.Parse(Environment.GetEnvironmentVariable("NEXRT_SOAK_HOST_SECONDS")!,CultureInfo.InvariantCulture);
        string run=Environment.GetEnvironmentVariable("NEXRT_SOAK_RUN")!;
        uint[] words=new uint[12];var wall=Stopwatch.StartNew();
        void Publish(string outcome)
        {
            string json=JsonSerializer.Serialize(new {outcome,feature,wallSeconds=wall.Elapsed.TotalSeconds,
                phase=words[0],firmwareFeature=words[1],failure=words[2],workers=words[3],total=words[4],
                flags=words[5],ticketWords=words[6],exchangeOk=words[7],casChecks=words[8],guards=words[9],
                clockHz=words[10],rounds=words[11],amoRequests=checked((uint)requests.ReadUInt().Value),
                timerTraps=checked((uint)timers.ReadUInt().Value)});
            File.WriteAllText(Path.Combine(run,"status.json"),json+"\n");
        }
        try
        {
            for(;;)
            {
                await Triggers.Timer(100_000);
                for(int i=0;i<words.Length;++i){
                    address.Write(new UInt<W32>(location+(uint)i*4));
                    await Triggers.Timer(10);await Triggers.ReadOnly();words[i]=checked((uint)data.ReadUInt().Value);
                    await Triggers.Timer(1);
                }
                if(fatal.ReadBit()||bad.ReadUInt().Value!=0)throw new IOException("CPU fault/unexpected trap");
                if(words[0]==3)throw new IOException("Atomic firmware failure="+words[2]);
                if(words[0]>3)throw new IOException("Invalid atomic fixture phase");
                if(words[0]==2){
                    if(words[1]!=feature||words[2]!=0||words[3]!=7||words[4]!=192||words[5]!=0||
                       words[6]!=6||words[7]!=1||words[8]!=384||words[9]!=5||words[10]!=100_000_000||words[11]!=64||
                       timers.ReadUInt().Value<3||(feature==0?requests.ReadUInt().Value!=0:requests.ReadUInt().Value==0))
                        throw new IOException("Atomic fixture result/AMO activity mismatch");
                    Publish("completed");return;
                }
                if(wall.Elapsed.TotalSeconds>limit)throw new TimeoutException("Atomic fixture host timeout");
            }
        }
        catch(Exception error){Publish("failed");File.WriteAllText(Path.Combine(run,"failure.txt"),error.ToString());throw;}
    }
}
