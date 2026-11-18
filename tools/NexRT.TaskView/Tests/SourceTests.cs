using System.Net;
using System.Net.Sockets;
using NexRT.TaskView.Core;
using NUnit.Framework;
namespace NexRT.TaskView.Tests;
public class SourceTests
{
    [Test] public async Task FileReplayUsesActualBinarySource()
    {
        string path=Path.GetTempFileName();try {await File.WriteAllBytesAsync(path,DecoderTests.Frame());var d=new TraceDecoder();var events=new List<TraceEvent>();var result=await TraceCommands.ReplayAsync(path,b=>events.AddRange(d.Feed(b)),default);Assert.That(result.Success,Is.True);Assert.That(events,Has.Count.EqualTo(1));}finally{File.Delete(path);}
    }
    [Test] public async Task TcpUartReadsChunkedFramesAndCloses()
    {
        using var listener=new TcpListener(IPAddress.Loopback,0);listener.Start();int port=((IPEndPoint)listener.LocalEndpoint).Port;
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var sender=Task.Run(async()=>{using var socket=await listener.AcceptTcpClientAsync(timeout.Token);var stream=socket.GetStream();byte[] bytes=DecoderTests.Frame();await stream.WriteAsync(bytes.AsMemory(0,5),timeout.Token);await stream.WriteAsync(bytes.AsMemory(5),timeout.Token);});
        var d=new TraceDecoder();var events=new List<TraceEvent>();var result=await TraceCommands.ConnectAsync($"127.0.0.1:{port}",b=>events.AddRange(d.Feed(b)),timeout.Token);await sender;Assert.That(result.Success,Is.True);Assert.That(events,Has.Count.EqualTo(1));
    }
    [Test] public void CancellationRemainsCancellation()
    {using var c=new CancellationTokenSource();c.Cancel();Assert.CatchAsync<OperationCanceledException>(async()=>await TraceCommands.ConnectAsync("localhost:5556",_=>{},c.Token));}
}
