using System.Buffers.Binary;
using NexRT.TaskView.Core;
using NUnit.Framework;
namespace NexRT.TaskView.Tests;
public class DecoderTests
{
    internal static byte[] Frame(uint sequence=1, byte kind=1,uint value=0)
    {
        byte[] raw=new byte[32];"NXT1"u8.CopyTo(raw);raw[4]=1;raw[5]=kind;raw[6]=3;raw[8]=5;raw[9]=2;
        BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(12),sequence);BinaryPrimitives.WriteUInt64LittleEndian(raw.AsSpan(16),12345);
        BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(24),value);BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(28),TraceDecoder.Crc32C(raw.AsSpan(0,28)));
        return Encode(raw);
    }
    internal static byte[] Encode(byte[] raw)
    {
        var output=new List<byte>{0};int codeIndex=0;byte code=1;
        foreach(byte b in raw){if(b==0){output[codeIndex]=code;codeIndex=output.Count;output.Add(0);code=1;}else{output.Add(b);code++;}}
        output[codeIndex]=code;output.Add(0);return output.ToArray();
    }
    [Test] public void IndependentCrcVector()=>Assert.That(TraceDecoder.Crc32C("123456789"u8),Is.EqualTo(0xe3069283u));
    [Test] public void EverySplitPreservesOneRecord()
    {
        byte[] bytes=Frame();for(int split=0;split<=bytes.Length;split++){var d=new TraceDecoder();var events=d.Feed(bytes.AsSpan(0,split)).Concat(d.Feed(bytes.AsSpan(split))).ToArray();Assert.That(events,Has.Length.EqualTo(1));Assert.That(events[0].Timestamp,Is.EqualTo(12345));Assert.That(events[0].EffectivePriority,Is.EqualTo(2));}
    }
    [Test] public void EverySingleBitCorruptionRejected()
    {
        byte[] bytes=Frame();for(int i=0;i<bytes.Length-1;i++)for(int bit=0;bit<8;bit++){var copy=(byte[])bytes.Clone();copy[i]^=(byte)(1<<bit);Assert.That(new TraceDecoder().Feed(copy),Is.Empty,$"byte {i} bit {bit}");}
    }
    [Test] public void OversizedNoiseResynchronizes()
    {var d=new TraceDecoder();Assert.That(d.Feed(Enumerable.Repeat((byte)42,10000).Append((byte)0).ToArray()),Is.Empty);Assert.That(d.Feed(Frame()),Has.Count.EqualTo(1));Assert.That(d.Rejected,Is.EqualTo(1));}
    [Test] public void SeededMalformedChunksAreBoundedAndRecover()
    {
        var random=new Random(741923);var d=new TraceDecoder();for(int run=0;run<5000;run++){byte[] bytes=new byte[random.Next(0,100)];random.NextBytes(bytes);d.Feed(bytes);}d.Feed(new byte[]{0});Assert.That(d.Feed(Frame()),Has.Count.EqualTo(1));
    }
    [Test] public void RetentionAndSequenceWrap()
    {var store=new TraceStore();store.Add(new[]{new TraceEvent(EventKind.Ready,1,0,1,1,0,uint.MaxValue,1,0,null),new TraceEvent(EventKind.Ready,1,0,1,1,0,0,2,0,null)});Assert.That(store.SequenceGaps,Is.Zero);store.Add(Enumerable.Range(1,100005).Select(i=>new TraceEvent(EventKind.Ready,1,0,1,1,0,(uint)i,3,0,null)));Assert.That(store.Events,Has.Count.EqualTo(TraceStore.Capacity));Assert.That(store.Evicted,Is.EqualTo(7));}
    [Test] public async Task MissingFileIsStructuredFailure()=>Assert.That((await TraceCommands.ReplayAsync("missing-nexrt-capture.bin",_=>Assert.Fail(),default)).Success,Is.False);
    [TestCase("")][TestCase("localhost")][TestCase("host:0")][TestCase("host:65536")][TestCase("host:abc")]
    public async Task InvalidEndpointIsStructuredFailure(string endpoint)=>Assert.That((await TraceCommands.ConnectAsync(endpoint,_=>Assert.Fail(),default)).Success,Is.False);
    [Test] public void MetadataAndEveryNumericFieldDecode()
    {
        byte[] raw=new byte[32];"NXT1"u8.CopyTo(raw);raw[4]=1;raw[5]=128;raw[6]=12;raw[7]=13;raw[8]=14;raw[9]=15;BinaryPrimitives.WriteUInt16LittleEndian(raw.AsSpan(10),0x1234);BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(12),0x87654321);"worker"u8.CopyTo(raw.AsSpan(16));BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(28),TraceDecoder.Crc32C(raw.AsSpan(0,28)));
        var e=new TraceDecoder().Feed(Encode(raw)).Single();Assert.Multiple(()=>{Assert.That(e.Name,Is.EqualTo("worker"));Assert.That(e.Timestamp,Is.Zero);Assert.That(e.Thread,Is.EqualTo(12));Assert.That(e.Related,Is.EqualTo(13));Assert.That(e.BasePriority,Is.EqualTo(14));Assert.That(e.EffectivePriority,Is.EqualTo(15));Assert.That(e.Flags,Is.EqualTo(0x1234));Assert.That(e.Sequence,Is.EqualTo(0x87654321));});
        foreach(int offset in new[]{0,4,5}){var bad=(byte[])raw.Clone();bad[offset]=255;BinaryPrimitives.WriteUInt32LittleEndian(bad.AsSpan(28),TraceDecoder.Crc32C(bad.AsSpan(0,28)));Assert.That(new TraceDecoder().Feed(Encode(bad)),Is.Empty);}
        Assert.That(new TraceDecoder().Feed(Encode(new byte[31])),Is.Empty);Assert.That(new TraceDecoder().Feed(Encode(new byte[33])),Is.Empty);
    }
    [Test] public void EmptyDelimiterAndBadFrameDiagnostics()
    {var d=new TraceDecoder();Assert.That(d.Feed(new byte[]{0}),Is.Empty);Assert.That(d.Rejected,Is.Zero);Assert.That(d.Feed(new byte[]{1,0}),Is.Empty);Assert.That(d.Rejected,Is.EqualTo(1));var store=new TraceStore();store.Add(new TraceDecoder().Feed(Frame(1)));store.Add(new TraceDecoder().Feed(Frame(3)));Assert.That(store.SequenceGaps,Is.EqualTo(1));}    [TestCase(0,88)][TestCase(4,2)][TestCase(5,127)]
    public void ValidCrcDoesNotBypassHeaderContract(int offset,byte value)
    {
        byte[] raw=new byte[32];"NXT1"u8.CopyTo(raw);raw[4]=1;raw[5]=1;raw[offset]=value;BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(24),value);BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(28),TraceDecoder.Crc32C(raw.AsSpan(0,28)));var d=new TraceDecoder();Assert.That(d.Feed(Encode(raw)),Is.Empty);Assert.That(d.Rejected,Is.EqualTo(1));
    }    [Test] public void ClockAndLossMetadataPreserveContinuity()
    {var d=new TraceDecoder();var store=new TraceStore();Assert.That(store.ClockHz,Is.Null);Assert.That(store.TargetDropped,Is.Null);store.Add(d.Feed(Frame(1,129)));store.Add(d.Feed(Frame(2,130,123)));Assert.That(store.ClockHz,Is.EqualTo(12345));Assert.That(store.TargetDropped,Is.EqualTo(123));Assert.That(store.SequenceGaps,Is.Zero);Assert.That(d.Rejected,Is.Zero);}}
