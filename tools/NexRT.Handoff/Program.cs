using System.Diagnostics;
using NexRT.Host;

namespace NexRT.Handoff;

/// <summary>Portable dependency diagnosis and Git-aware development handoff, without shell/Python automation.</summary>
internal static partial class Program
{
    /// <summary>Runs doctor ROOT PROFILES [--key value], pack ROOT ZIP [--key value], verify DIRECTORY, restore DIRECTORY NEW_DESTINATION, or self-test.</summary>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if(args.Length==1&&args[0]=="self-test"){await SelfTest();return 0;}
            if(args.Length==2&&args[0]=="verify"){Verify(Path.GetFullPath(args[1]));return 0;}
            if(args.Length==2&&args[0]=="audit-zip"){await AuditZip(Path.GetFullPath(args[1]));return 0;}
            if(args.Length==3&&args[0]=="restore"){await Restore(Path.GetFullPath(args[1]),Path.GetFullPath(args[2]));return 0;}
            if(args.Length>=3&&args[0]=="doctor")return await Doctor(Path.GetFullPath(args[1]),args[2],Options(args[3..]));
            if(args.Length>=3&&args[0]=="pack"){await Pack(Path.GetFullPath(args[1]),Path.GetFullPath(args[2]),Options(args[3..]));return 0;}
            throw new ArgumentException("doctor ROOT host,docs,analysis,arm,riscv,rtl [--key value] | pack ROOT ZIP [--key value] | verify DIRECTORY | restore DIRECTORY NEW_DESTINATION | self-test");
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
    }

    /// <summary>Parses literal named arguments and rejects duplicate or incomplete options.</summary>
    private static Dictionary<string,string> Options(string[] args)
    {
        var result=new Dictionary<string,string>(StringComparer.Ordinal);
        for(int i=0;i<args.Length;i+=2)
            if(!args[i].StartsWith("--",StringComparison.Ordinal)||i+1>=args.Length||!result.TryAdd(args[i][2..],args[i+1]))
                throw new ArgumentException("Expected unique --name value options");
        return result;
    }

    /// <summary>Runs one owned process with bounded execution/output and separate literal arguments.</summary>
    private static async Task<HostResult> Execute(string executable,IEnumerable<string> arguments,string? directory=null)
    {
        var start=new ProcessStartInfo(executable){WorkingDirectory=directory??Environment.CurrentDirectory};
        foreach(string argument in arguments)start.ArgumentList.Add(argument);
        // Git traversals on a Windows-mounted source tree can be substantially
        // slower than version probes. Retain a deadline and capture cap for both.
        var result=await HostProcess.Run(start,TimeSpan.FromMinutes(executable=="git"?10:2),8_000_000);
        if(result.TimedOut||result.OutputLimit||result.IoError is not null)
            throw new IOException($"Process/capture bound failed: {executable} {string.Join(' ',start.ArgumentList.Take(4))}; timeout={result.TimedOut}; outputLimit={result.OutputLimit}; I/O={result.IoError}");
        return result;
    }

    /// <summary>Requires successful Git execution and preserves its diagnostic on failure.</summary>
    private static async Task<string> Git(string root,params string[] arguments)
    {
        var result=await Execute("git",new[]{"-C",root}.Concat(arguments));
        if(result.ExitCode!=0)throw new IOException("Git failed: "+result.Output);
        return result.Output;
    }
}
