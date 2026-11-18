using System.Buffers.Binary;
using System.Text;
namespace NexRT.TaskView.Core;

/// <summary>Stable target instrumentation event identifiers.</summary>
public enum EventKind : byte {
    /// <summary>Task selected to run.</summary>
    Dispatch=1,
    /// <summary>Task made runnable.</summary>
    Ready,
    /// <summary>Task blocked.</summary>
    Block,
    /// <summary>Effective priority changed.</summary>
    Priority,
    /// <summary>Waiter depends on mutex owner.</summary>
    MutexWait,
    /// <summary>Mutex ownership acquired.</summary>
    MutexAcquire,
    /// <summary>Mutex ownership released.</summary>
    MutexRelease,
    /// <summary>Task execution finished.</summary>
    Finish,
    /// <summary>Task sleeping.</summary>
    Sleep,
    /// <summary>Task name metadata; timestamp bytes hold name.</summary>
    ThreadInfo=128,
    /// <summary>Target clock frequency in the timestamp field, in hertz.</summary>
    ClockInfo=129,
    /// <summary>Cumulative target event loss in the value field.</summary>
    Loss=130
}
/// <summary>A validated target event. Timestamps are target ticks, not host time.</summary>
public sealed record TraceEvent(EventKind Kind, byte Thread, byte Related, byte BasePriority, byte EffectivePriority, ushort Flags, uint Sequence, ulong Timestamp, uint Value, string? Name, bool BoundaryBefore=false);
/// <summary>Incremental, bounded COBS decoder. Oversized frames are discarded through the next delimiter.</summary>
public sealed class TraceDecoder
{
    private readonly byte[] frame = new byte[33];
    private int used;
    private bool overflow;
    private bool boundary;
    /// <summary>Rejected nonempty or oversized frames.</summary>
    public long Rejected { get; private set; }
    /// <summary>Consumes arbitrary UART chunks and returns validated records.</summary>
    public IReadOnlyList<TraceEvent> Feed(ReadOnlySpan<byte> bytes)
    {
        var result = new List<TraceEvent>();
        foreach (byte b in bytes)
        {
            if (b != 0) { if (used < frame.Length && !overflow) frame[used++] = b; else overflow = true; continue; }
            if (overflow) { Rejected++; boundary=true; }
            else if (used > 0) { var e = Decode(frame.AsSpan(0,used)); if (e is null) { Rejected++; boundary=true; } else { result.Add(e with { BoundaryBefore=boundary }); boundary=false; } }
            used=0; overflow=false;
        }
        return result;
    }
    private static TraceEvent? Decode(ReadOnlySpan<byte> input)
    {
        Span<byte> raw=stackalloc byte[32]; int source=0, target=0;
        while (source<input.Length)
        {
            int code=input[source++];
            if (code==0 || source+code-1>input.Length || target+code-1>32) return null;
            for (int n=1;n<code;n++) raw[target++]=input[source++];
            if (code<255 && source<input.Length) { if(target==32)return null; raw[target++]=0; }
        }
        if(target!=32 || !raw[..4].SequenceEqual("NXT1"u8) || raw[4]!=1 || !Enum.IsDefined((EventKind)raw[5])) return null;
        if(Crc32C(raw[..28])!=BinaryPrimitives.ReadUInt32LittleEndian(raw[28..]))return null;
        bool info=raw[5]==128;
        return new((EventKind)raw[5],raw[6],raw[7],raw[8],raw[9],BinaryPrimitives.ReadUInt16LittleEndian(raw[10..]),BinaryPrimitives.ReadUInt32LittleEndian(raw[12..]),info?0:BinaryPrimitives.ReadUInt64LittleEndian(raw[16..]),BinaryPrimitives.ReadUInt32LittleEndian(raw[24..]),info?Encoding.UTF8.GetString(raw[16..28]).TrimEnd('\0'):null);
    }
    /// <summary>Castagnoli CRC, reflected polynomial 0x82f63b78, initial/final XOR all ones.</summary>
    public static uint Crc32C(ReadOnlySpan<byte> bytes)
    {
        uint crc=uint.MaxValue;
        foreach(byte b in bytes) { crc^=b; for(int i=0;i<8;i++)crc=(crc>>1)^((crc&1)!=0?0x82f63b78u:0u); }
        return ~crc;
    }
}
/// <summary>Bounded retained evidence and sequence diagnostics. Higher priority numbers have greater urgency in NexRT.</summary>
public sealed class TraceStore
{
    private readonly Queue<TraceEvent> events = new();
    private uint? sequence;
    /// <summary>Maximum number of retained events.</summary>
    public const int Capacity=100_000;
    /// <summary>Retained events in arrival order.</summary>
    public IReadOnlyCollection<TraceEvent> Events => events;
    /// <summary>Discontinuities, including target restart and missing frames.</summary>
    public long SequenceGaps { get; private set; }
    /// <summary>Old records evicted by the retention bound.</summary>
    public long Evicted { get; private set; }
    /// <summary>Reported target clock, or unknown until clock metadata is received.</summary>
    public ulong? ClockHz { get; private set; }
    /// <summary>Latest cumulative target-side loss report, or unknown before a report.</summary>
    public uint? TargetDropped { get; private set; }
    /// <summary>Adds validated records without synthesizing missing target activity.</summary>
    public void Add(IEnumerable<TraceEvent> batch)
    {
        foreach(var e in batch) { if(e.Kind==EventKind.ClockInfo)ClockHz=e.Timestamp; if(e.Kind==EventKind.Loss)TargetDropped=e.Value; if(sequence.HasValue && unchecked(sequence.Value+1)!=e.Sequence)SequenceGaps++; sequence=e.Sequence; if(events.Count==Capacity){events.Dequeue();Evicted++;} events.Enqueue(e); }
    }
}
/// <summary>Structured command outcome for expected source failures.</summary>
public sealed record CommandResult(bool Success, string Message);
/// <summary>Shared typed file/live commands used by the desktop and CLI.</summary>
public sealed class TraceCommands
{
    /// <summary>Loads a binary COBS capture without retaining the whole source file.</summary>
    public static async Task<CommandResult> ReplayAsync(string path, Action<byte[]> consume, CancellationToken token)
    {
        try { await using var stream=File.OpenRead(path); await ReadAsync(stream,consume,token); return new(true,"Replay complete"); }
        catch(Exception ex) when(ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException) { return new(false,ex.Message); }
    }
    /// <summary>Connects to a QEMU UART TCP endpoint and awaits data without polling.</summary>
    public static async Task<CommandResult> ConnectAsync(string endpoint, Action<byte[]> consume, CancellationToken token)
    {
        int colon=endpoint.LastIndexOf(':');
        if(colon<=0 || !int.TryParse(endpoint[(colon+1)..],out int port) || port is <1 or >65535)return new(false,"Expected HOST:PORT (port 1–65535)");
        try { using var client=new System.Net.Sockets.TcpClient(); await client.ConnectAsync(endpoint[..colon].Trim('[',']'),port,token); await ReadAsync(client.GetStream(),consume,token); return new(true,"UART connection closed"); }
        catch(Exception ex) when(ex is System.Net.Sockets.SocketException or IOException or ArgumentException) { return new(false,ex.Message); }
    }
    private static async Task ReadAsync(Stream stream, Action<byte[]> consume,CancellationToken token)
    { byte[] bytes=new byte[4096]; int count; while((count=await stream.ReadAsync(bytes,token))>0)consume(bytes.AsSpan(0,count).ToArray()); }
}






/// <summary>A measured end boundary, or the reason an interval remains unknown.</summary>
public sealed record StateInterval(ulong? End,string? UnknownReason);
/// <summary>Derives spans only from contiguous, ordered raw evidence, including metadata sequence numbers.</summary>
public static class TimelineIntervals
{
    /// <summary>Finds the next state endpoint, refusing to bridge missing/corrupt data or a target reset.</summary>
    public static StateInterval Derive(IReadOnlyList<TraceEvent> raw,int start)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(start);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(start,raw.Count);
        var first=raw[start];
        if(first.Kind is not (EventKind.Dispatch or EventKind.Ready or EventKind.Block or EventKind.Sleep))return new(null,"Instant event");
        uint sequence=first.Sequence;ulong time=first.Timestamp;
        for(int i=start+1;i<raw.Count;i++)
        {
            var next=raw[i];
            if(next.BoundaryBefore)return new(null,"Rejected frame boundary");
            if(next.Sequence!=unchecked(sequence+1))return new(null,"Sequence discontinuity / target restart");
            sequence=next.Sequence;
            if(next.Kind==EventKind.Loss)return new(null,"Target event loss");
            if(next.Kind is EventKind.ThreadInfo or EventKind.ClockInfo)continue;
            if(next.Timestamp<time)return new(null,"Target clock reset / unordered timestamps");
            time=next.Timestamp;
            bool end=first.Kind==EventKind.Dispatch
                ? next.Kind==EventKind.Dispatch || (next.Thread==first.Thread && next.Kind is EventKind.Block or EventKind.Sleep or EventKind.Finish)
                : next.Thread==first.Thread && next.Kind is EventKind.Dispatch or EventKind.Ready or EventKind.Block or EventKind.Sleep or EventKind.Finish;
            if(end)return new(time,null);
        }
        return new(null,"No observed endpoint");
    }
}
