using System.Diagnostics;
using System.Text;

namespace NexRT.Host;

/// <summary>Complete bounded process outcome, including partial output on cancellation.</summary>
internal sealed record HostResult(int ExitCode,string Output,bool TimedOut,bool OutputLimit,string? IoError);

/// <summary>Runs owned subprocesses without shell parsing or unbounded redirected-pipe waits.</summary>
internal static class HostProcess
{
    /// <summary>Bounds process exit and both output drains by one deadline and a shared character cap.</summary>
    /// <param name="start">Literal executable/argument list; streams are redirected here.</param>
    /// <param name="duration">Wall-clock execution and drain budget.</param>
    /// <param name="maximumCharacters">Maximum retained output across both channels.</param>
    /// <returns>Captured outcome; timeout includes descendants retaining inherited pipes.</returns>
    public static async Task<HostResult> Run(ProcessStartInfo start,TimeSpan duration,int maximumCharacters=8_000_000)
    {
        if(duration<=TimeSpan.Zero||maximumCharacters<1)throw new ArgumentOutOfRangeException(nameof(duration));
        start.UseShellExecute=false;start.CreateNoWindow=true;
        start.RedirectStandardOutput=true;start.RedirectStandardError=true;
        using var deadline=new CancellationTokenSource(duration);
        using var process=Process.Start(start)??throw new IOException("Cannot start "+start.FileName);
        var output=new StringBuilder();var gate=new object();bool limit=false;string? ioError=null;
        async Task Drain(StreamReader reader)
        {
            var buffer=new char[4096];
            try
            {
                int count;
                while((count=await reader.ReadAsync(buffer.AsMemory(),deadline.Token))!=0)
                {
                    lock(gate)
                    {
                        int accepted=Math.Min(count,maximumCharacters-output.Length);
                        output.Append(buffer,0,accepted);Console.Write(buffer,0,accepted);
                        if(accepted!=count){limit=true;deadline.Cancel();}
                    }
                    deadline.Token.ThrowIfCancellationRequested();
                }
            }
            catch(OperationCanceledException)when(deadline.IsCancellationRequested){}
            catch(IOException error){lock(gate)ioError=error.Message;deadline.Cancel();}
            catch(ObjectDisposedException)when(deadline.IsCancellationRequested){}
        }
        Task stdout=Drain(process.StandardOutput),stderr=Drain(process.StandardError);
        bool timedOut=false;
        try{await Task.WhenAll(process.WaitForExitAsync(deadline.Token),stdout,stderr).WaitAsync(deadline.Token);}
        catch(OperationCanceledException)when(deadline.IsCancellationRequested){timedOut=!limit&&ioError is null;}
        finally
        {
            // A cancelled drain can finish just before WaitAsync observes the
            // cancellation. Preserve timeout classification in that race.
            if(deadline.IsCancellationRequested&&!limit&&ioError is null)timedOut=true;
            if(!process.HasExited)
            {
                try{process.Kill(entireProcessTree:true);}
                catch(InvalidOperationException){} // Owned child exited between observation and kill.
            }
            // Bound pipes inherited by a descendant even when its parent has exited.
            deadline.Cancel();process.StandardOutput.Dispose();process.StandardError.Dispose();
            try{await Task.WhenAll(stdout,stderr).WaitAsync(TimeSpan.FromSeconds(2));}
            catch(TimeoutException){timedOut=true;}
        }
        lock(gate)return new HostResult(process.HasExited?process.ExitCode:-1,output.ToString(),timedOut,limit,ioError);
    }
}
