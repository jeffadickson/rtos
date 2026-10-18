using System.Text.Json.Serialization;
namespace NexRT.Requirements;
/// <summary>Typed command evidence preserves the checkpoint JSON schema without reflection.</summary>
internal sealed record CommandResult(string Tool,string[] Arguments,int ExitCode,string Log,string Sha256);
/// <summary>Typed checkpoint evidence can be serialized in Native AOT builds.</summary>
internal sealed record CheckpointResult(int Iteration,int ReviewedRequirements,string CatalogueSha256,string ReviewedSourcesSha256,DateTimeOffset Utc,string Result,string Scope,List<CommandResult> Commands);
/// <summary>Compile-time metadata for every requirements-tool JSON root.</summary>
[JsonSourceGenerationOptions(PropertyNamingPolicy=JsonKnownNamingPolicy.SnakeCaseLower,WriteIndented=true,UnmappedMemberHandling=JsonUnmappedMemberHandling.Disallow)]
[JsonSerializable(typeof(Requirement[]),TypeInfoPropertyName="Requirements")]
[JsonSerializable(typeof(Dictionary<string,string>),TypeInfoPropertyName="SourceHashes")]
[JsonSerializable(typeof(CheckpointResult),TypeInfoPropertyName="Checkpoint")]
internal partial class TrackerJsonContext : JsonSerializerContext { }
