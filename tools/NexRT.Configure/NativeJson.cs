using System.Text.Json.Serialization;
namespace NexRT.Configure;
/// <summary>Compile-time configuration metadata avoids reflection-dependent JSON in native executables.</summary>
[JsonSourceGenerationOptions(PropertyNamingPolicy=JsonKnownNamingPolicy.CamelCase,UnmappedMemberHandling=JsonUnmappedMemberHandling.Disallow)]
[JsonSerializable(typeof(Catalogue))]
internal partial class ConfigurationJsonContext : JsonSerializerContext { }
