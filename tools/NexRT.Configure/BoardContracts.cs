using System.Text.Json;
namespace NexRT.Configure;
/// <summary>Explicit FPGA hardware inputs; null means unknown, never an inferred register address.</summary>
internal sealed record BoardContract(int SchemaVersion,string TargetId,string Core,string Isa,string Endianness,string CachePolicy,string TimerKind,uint? RamOrigin,uint? RamBytes,uint? TimerHz,uint? TimerAddress,uint? CompareAddress,uint? InterruptController,uint? TimerInterrupt,string? HardwareReference,string? CompilerPrefix);
/// <summary>Checks the hardware handoff for the staged Tang Nano and classic MicroBlaze ports.</summary>
internal static class BoardContracts
{
    /// <summary>Loads bounded reflection-free JSON and reports missing or inconsistent hardware inputs.</summary>
    public static void Check(string path)
    {
        if(new FileInfo(path).Length>65536)throw new InvalidDataException("Board contract exceeds 64 KiB.");
        var board=JsonSerializer.Deserialize(File.ReadAllText(path),ConfigurationJsonContext.Default.BoardContract)??throw new InvalidDataException("Null board contract.");
        Validate(board);
        Console.WriteLine("Hardware contract is structurally complete. Compiler availability and hardware values still require verification; firmware remains disabled until port/BSP implementation and qualification.");
    }
    /// <summary>Validates address extents, timer/interrupt inputs and architecture identity without enabling firmware.</summary>
    internal static void Validate(BoardContract b)
    {
        var errors=new List<string>();
        if(b.SchemaVersion!=1)errors.Add("schemaVersion=1");
        bool tang=b.TargetId=="tang-nano-9k-riscue",micro=b.TargetId=="microblaze";
        if(!tang&&!micro)errors.Add("targetId");
        if(tang&&(b.Core!="riscue"||b.Isa!="rv32im_zicsr"||b.Endianness!="little"||b.TimerKind!="riscv-mtime"))errors.Add("RISCue machine-mode ISA/timer contract (PicoRV32 is a different port)");
        if(micro&&(b.Core!="microblaze-classic"||b.Isa!="microblaze32"||b.Endianness is not("little" or "big")||b.TimerKind!="axi-timer"))errors.Add("classic MicroBlaze ISA/endianness/AXI timer contract (MicroBlaze V is a different port)");
        if(b.CachePolicy is not("disabled" or "board-managed"))errors.Add("cachePolicy");
        if(b.RamOrigin is null||b.RamBytes is null or 0||b.RamOrigin%16!=0||b.RamBytes%16!=0||(ulong)b.RamOrigin.GetValueOrDefault()+b.RamBytes.GetValueOrDefault()>0x100000000UL)errors.Add("nonempty aligned RAM extent (linker still checks whether firmware fits)");
        if(b.TimerHz is null or 0)errors.Add("timerHz");
        if(b.TimerAddress is null||b.TimerAddress%4!=0||b.TimerAddress>uint.MaxValue-7)errors.Add("timerAddress");
        if(tang&&(b.CompareAddress is null||b.CompareAddress%4!=0||b.CompareAddress>uint.MaxValue-7||b.CompareAddress==b.TimerAddress))errors.Add("compareAddress");
        if(micro&&(b.InterruptController is null||b.InterruptController%4!=0||b.TimerInterrupt is null or >31))errors.Add("interruptController/timerInterrupt");
        if(string.IsNullOrWhiteSpace(b.HardwareReference))errors.Add("hardwareReference (bitstream/export identity)");
        if(string.IsNullOrWhiteSpace(b.CompilerPrefix))errors.Add("compilerPrefix");
        if(errors.Count!=0)throw new InvalidDataException("Missing/invalid board inputs: "+string.Join("; ",errors));
    }
    /// <summary>Exercises valid metadata and rejection of unknown, conflicting or overflowing hardware descriptions.</summary>
    public static void SelfTest()
    {
        var valid=new BoardContract(1,"microblaze","microblaze-classic","microblaze32","little","disabled","axi-timer",0,65536,100000000,0x40000000,null,0x41200000,0,"fixture-only", "mb-");
        Validate(valid);
        void Reject(BoardContract b){try{Validate(b);}catch(InvalidDataException){return;}throw new InvalidOperationException("Invalid board contract accepted.");}
        Reject(valid with{Core="microblaze-v"});Reject(valid with{TimerHz=null});Reject(valid with{RamOrigin=0xfffffff0,RamBytes=65536});Reject(valid with{RamBytes=0});
        Reject(valid with{InterruptController=null});Reject(valid with{TimerInterrupt=32});Reject(valid with{HardwareReference=null});
        var tang=valid with{TargetId="tang-nano-9k-riscue",Core="riscue",Isa="rv32im_zicsr",TimerKind="riscv-mtime",TimerAddress=0x0200bff8,CompareAddress=0x02004000};
        Validate(tang);Reject(tang with{Core="picorv32"});Reject(tang with{CompareAddress=tang.TimerAddress});
    }
}
