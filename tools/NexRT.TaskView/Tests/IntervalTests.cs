using NexRT.TaskView.Core;
using NUnit.Framework;
namespace NexRT.TaskView.Tests;
public class IntervalTests
{
    private static TraceEvent E(EventKind kind,uint seq,ulong time,byte thread=1,bool boundary=false)=>new(kind,thread,0,1,1,0,seq,time,0,null,boundary);
    [Test] public void MissingSequenceNeverDrawsRunningAcrossGap()
    {TraceEvent[] raw=[E(EventKind.Dispatch,10,100),E(EventKind.Ready,12,300),E(EventKind.Dispatch,13,400)];var interval=TimelineIntervals.Derive(raw,0);Assert.That(interval.End,Is.Null);Assert.That(interval.UnknownReason,Does.Contain("Sequence"));}
    [Test] public void MetadataParticipatesInSequenceButNotTimestamp()
    {TraceEvent[] raw=[E(EventKind.Dispatch,10,100),E(EventKind.ThreadInfo,11,0),E(EventKind.ClockInfo,12,10_000_000),E(EventKind.Dispatch,13,400,2)];Assert.That(TimelineIntervals.Derive(raw,0).End,Is.EqualTo(400));}
    [TestCase(EventKind.Loss,200UL,false,"loss")][TestCase(EventKind.Ready,90UL,false,"reset")][TestCase(EventKind.Ready,200UL,true,"Rejected")]
    public void UncertainEvidenceStopsInterval(EventKind kind,ulong time,bool boundary,string reason)
    {TraceEvent[] raw=[E(EventKind.Dispatch,10,100),E(kind,11,time,2,boundary),E(EventKind.Dispatch,12,400,2)];var interval=TimelineIntervals.Derive(raw,0);Assert.That(interval.End,Is.Null);Assert.That(interval.UnknownReason,Does.Contain(reason).IgnoreCase);}
    [Test] public void UnsignedSequenceWrapIsContiguous()
    {TraceEvent[] raw=[E(EventKind.Dispatch,uint.MaxValue,100),E(EventKind.Ready,0,200,2),E(EventKind.Dispatch,1,300,2)];Assert.That(TimelineIntervals.Derive(raw,0).End,Is.EqualTo(300));}
    [Test] public void ReadyEndsOnlyAtSameTaskStateChange()
    {TraceEvent[] raw=[E(EventKind.Ready,10,100),E(EventKind.Dispatch,11,200,2),E(EventKind.Block,12,300)];Assert.That(TimelineIntervals.Derive(raw,0).End,Is.EqualTo(300));}
    [Test] public void EndOfCaptureIsUnknown()
    {Assert.That(TimelineIntervals.Derive(new[]{E(EventKind.Dispatch,1,100)},0).UnknownReason,Does.Contain("endpoint"));}
    [Test] public void RejectedFrameBoundarySurvivesChunking()
    {
        var d=new TraceDecoder();var raw=new List<TraceEvent>();raw.AddRange(d.Feed(DecoderTests.Frame(10)));d.Feed(new byte[]{2});d.Feed(new byte[]{0});raw.AddRange(d.Feed(DecoderTests.Frame(11,1)));Assert.That(raw[1].BoundaryBefore,Is.True);Assert.That(TimelineIntervals.Derive(raw,0).End,Is.Null);raw.AddRange(d.Feed(DecoderTests.Frame(12,1)));Assert.That(TimelineIntervals.Derive(raw,1).End,Is.EqualTo(12345));
    }
}
