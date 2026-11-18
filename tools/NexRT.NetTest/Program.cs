using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

/// <summary>Finite host/QEMU UDP acceptance with an owned subprocess and bounded socket operations.</summary>
internal static class Program
{
    /// <summary>Launches smoke ELF OUTPUT [PORT], or probes HOST PORT [COUNT]. Expected failures return nonzero without dialogs.</summary>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if(args.Length==1&&args[0]=="self-test")await SelfTest();
            else if(args.Length is 3 or 4&&args[0]=="smoke")await Smoke(Path.GetFullPath(args[1]),Path.GetFullPath(args[2]),args.Length>3?Port(args[3]):10007);
            else if(args.Length is 3 or 4&&args[0]=="probe")await Probe(args[1],Port(args[2]),args.Length>3?int.Parse(args[3]):128);
            else throw new ArgumentException("Usage: NexRT.NetTest smoke ELF OUTPUT [PORT] | probe HOST PORT [COUNT]");
            return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
    }
    /// <summary>Exercises malformed inputs and a real bounded no-response socket path without starting QEMU.</summary>
    private static async Task SelfTest()
    {
        foreach(string text in new[]{"0","1023","65536","no-port"}){
            bool rejected=false;try{_ = Port(text);}catch(Exception e)when(e is ArgumentException or FormatException){rejected=true;}
            if(!rejected)throw new IOException("Invalid port accepted");
        }
        foreach(int count in new[]{3,10001}){
            bool rejected=false;try{await Probe("127.0.0.1",10007,count);}catch(ArgumentException){rejected=true;}
            if(!rejected)throw new IOException("Invalid count accepted");
        }
        using var sink=new Socket(AddressFamily.InterNetwork,SocketType.Dgram,ProtocolType.Udp);
        sink.Bind(new IPEndPoint(IPAddress.Loopback,0));
        bool timedOut=false;try{await Probe("127.0.0.1",((IPEndPoint)sink.LocalEndPoint!).Port,4);}
        catch(OperationCanceledException){timedOut=true;}
        if(!timedOut)throw new IOException("Unresponsive UDP endpoint did not time out");
        Console.WriteLine("PASS six malformed-input cases and bounded UDP timeout");
    }
    /// <summary>Rejects privileged, invalid and accidental wildcard forwarding ports.</summary>
    private static int Port(string text){int value=int.Parse(text);return value is >=1024 and <=65535?value:throw new ArgumentException("Use a host port from1024 through65535");}

    /// <summary>Checks actual UDP datagrams including empty, maximum and rejected oversize payloads; no retries conceal loss.</summary>
    internal static async Task Probe(string host,int port,int count)
    {
        if(count is <4 or >10000)throw new ArgumentException("Count must be4..10000");
        using var socket=new Socket(AddressFamily.InterNetwork,SocketType.Dgram,ProtocolType.Udp);
        await socket.ConnectAsync(host,port);
        byte[] send=new byte[513],receive=new byte[1024];var random=new Random(0x4e585254);
        async Task Echo(int length)
        {
            random.NextBytes(send.AsSpan(0,length));
            using var deadline=new CancellationTokenSource(TimeSpan.FromSeconds(2));
            await socket.SendAsync(send.AsMemory(0,length),SocketFlags.None,deadline.Token);
            int got=await socket.ReceiveAsync(receive,SocketFlags.None,deadline.Token);
            if(got!=length||!send.AsSpan(0,length).SequenceEqual(receive.AsSpan(0,got)))throw new IOException($"UDP echo mismatch length{length}, received{got}");
        }
        var timer=Stopwatch.StartNew();
        for(int i=0;i<count;++i)await Echo(i switch{0=>0,1=>1,2=>511,3=>512,_=>random.Next(513)});
        using(var deadline=new CancellationTokenSource(TimeSpan.FromMilliseconds(300)))
        {
            await socket.SendAsync(send,SocketFlags.None,deadline.Token);
            try{await socket.ReceiveAsync(receive,SocketFlags.None,deadline.Token);throw new IOException("Oversize513-byte request unexpectedly echoed");}
            catch(OperationCanceledException)when(deadline.IsCancellationRequested){}
        }
        await Echo(32); // Cancellation/exhaustion must not strand the next accepted request.
        Console.WriteLine($"PASS {count+1} host UDP echoes, oversize rejection/recovery; seed0x4e585254; elapsed{timer.Elapsed.TotalSeconds:F3}s");
    }

    /// <summary>Runs an isolated single-hart QEMU with loopback-only forwarding, captures raw UART, then kills and observes its own process.</summary>
    private static async Task Smoke(string elf,string output,int port)
    {
        if(!File.Exists(elf))throw new FileNotFoundException("ELF not found",elf);
        if(!Directory.Exists(Path.GetDirectoryName(output)))throw new DirectoryNotFoundException("Output parent missing");
        if(!Directory.Exists(output))Directory.CreateDirectory(output);
        // Catch the common occupied-port case before starting QEMU; QEMU's own
        // binding remains authoritative in the unavoidable check/start race.
        using(var lease=new Socket(AddressFamily.InterNetwork,SocketType.Dgram,ProtocolType.Udp))lease.Bind(new IPEndPoint(IPAddress.Loopback,port));
        var start=new ProcessStartInfo("qemu-system-riscv32"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
        foreach(string argument in new[]{"-M","virt","-bios","none","-m","16M","-smp","1","-nographic","-monitor","none","-serial","stdio","-kernel",elf,"-global","virtio-mmio.force-legacy=false","-netdev",$"user,id=n0,hostfwd=udp:127.0.0.1:{port}-10.0.2.15:7","-device","virtio-net-device,netdev=n0,mac=02:00:00:00:00:02"})start.ArgumentList.Add(argument);
        using var qemu=Process.Start(start)??throw new IOException("QEMU failed to start");
        File.WriteAllText(Path.Combine(output,"qemu.pid"),qemu.Id.ToString());
        using var deadline=new CancellationTokenSource(TimeSpan.FromSeconds(30));
        var ready=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        async Task Capture(Stream stream,string path,bool watch)
        {
            await using var file=new FileStream(path,FileMode.Create,FileAccess.Write,FileShare.Read,4096,true);
            byte[] buffer=new byte[4096];byte[] marker=Encoding.ASCII.GetBytes("NEXRT_QEMU_NET_READY");int matched=0;long bytes=0;
            try{int n;while((n=await stream.ReadAsync(buffer,deadline.Token))!=0){
                bytes+=n;if(bytes>16_000_000)throw new IOException("UART capture quota exceeded");
                await file.WriteAsync(buffer.AsMemory(0,n),deadline.Token);await file.FlushAsync(deadline.Token);
                if(watch)for(int i=0;i<n;++i){matched=buffer[i]==marker[matched]?matched+1:buffer[i]==marker[0]?1:0;if(matched==marker.Length){ready.TrySetResult();matched=0;}}
            }}catch(OperationCanceledException)when(deadline.IsCancellationRequested){}
        }
        Task stdout=Capture(qemu.StandardOutput.BaseStream,Path.Combine(output,"uart.bin"),true);
        Task stderr=Capture(qemu.StandardError.BaseStream,Path.Combine(output,"qemu-error.log"),false);
        string outcome="failed";
        try{
            await ready.Task.WaitAsync(TimeSpan.FromSeconds(10),deadline.Token);
            if(qemu.HasExited)throw new IOException("QEMU exited before probing");
            await Probe("127.0.0.1",port,128).WaitAsync(deadline.Token);outcome="passed";
        }
        finally{
            if(!qemu.HasExited)qemu.Kill(true);
            await qemu.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(3));
            deadline.Cancel();await Task.WhenAll(stdout,stderr).WaitAsync(TimeSpan.FromSeconds(3));
            File.WriteAllText(Path.Combine(output,"result.json"),JsonSerializer.Serialize(new Result(outcome,qemu.Id,port,"10.0.2.15:7",outcome=="passed"?129:0,0x4e585254,true),NetJson.Default.Result)+"\n");
        }
        Console.WriteLine($"QEMU {outcome}; pid{qemu.Id} observed stopped; UART {Path.Combine(output,"uart.bin")}");
    }
}
/// <summary>Identifies a finite host/guest test; counts are acceptance goals and valid only for passed outcome.</summary>
internal sealed record Result(string Outcome,int Pid,int HostPort,string Guest,int Echoes,int Seed,bool CleanupObserved);
/// <summary>Native AOT JSON metadata.</summary>
[JsonSerializable(typeof(Result))]
internal partial class NetJson:JsonSerializerContext;
