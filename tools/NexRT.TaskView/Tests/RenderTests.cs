using Avalonia;
using Avalonia.Headless;
using Avalonia.Headless.NUnit;
using NexRT.TaskView;
using NexRT.TaskView.Core;
using NUnit.Framework;
[assembly: AvaloniaTestApplication(typeof(NexRT.TaskView.Tests.HeadlessApp))]
namespace NexRT.TaskView.Tests;
public class HeadlessApp
{
    public static AppBuilder BuildAvaloniaApp()=>Program.Build().UseHeadless(new AvaloniaHeadlessPlatformOptions{UseHeadlessDrawing=false});
}
[TestFixture]
public class RenderTests
{
    [AvaloniaTest] public void EmptyWindowRenders()
    {
        var window=new ViewerWindow([]);window.Show();
        using var bitmap=window.CaptureRenderedFrame();Assert.That(bitmap,Is.Not.Null);
        var path=Environment.GetEnvironmentVariable("NEXRT_TASKVIEW_SCREENSHOT");if(path is not null)bitmap!.Save(path,new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
        window.Close();
    }
    [AvaloniaTest] public void RealDecodedEventsRender()
    {
        var window=new Avalonia.Controls.Window{Width=1100,Height=500};var timeline=new Timeline();window.Content=timeline;
        var d=new TraceDecoder();timeline.Update(d.Feed(DecoderTests.Frame()));window.Show();
        using var bitmap=window.CaptureRenderedFrame();Assert.That(bitmap,Is.Not.Null);window.Close();
    }
    [AvaloniaTest] public async Task OptionalTargetCaptureRenders()
    {
        var input=Environment.GetEnvironmentVariable("NEXRT_TASKVIEW_TRACE");if(input is null)Assert.Ignore("Set NEXRT_TASKVIEW_TRACE to a real target capture.");
        var decoder=new TraceDecoder();var events=decoder.Feed(File.ReadAllBytes(input!));Assert.That(events.Count,Is.GreaterThan(0));
        var store=new TraceStore();store.Add(events);TestContext.WriteLine($"TARGET CAPTURE records={events.Count} rejected={decoder.Rejected} gaps={store.SequenceGaps} inherited={events.Count(e=>e.Kind==EventKind.Priority && e.EffectivePriority>e.BasePriority)} waits={events.Count(e=>e.Kind==EventKind.MutexWait)}");foreach(var group in events.GroupBy(e=>e.Kind))TestContext.WriteLine($"{group.Key}={group.Count()}");Assert.That(events.Any(e=>e.Kind==EventKind.Priority && e.EffectivePriority>e.BasePriority),Is.True);Assert.That(events.Any(e=>e.Kind==EventKind.MutexWait),Is.True);Assert.That(store.ClockHz,Is.EqualTo(10_000_000));var window=new ViewerWindow([]);window.Show();await window.Run(false,input!);
        using var bitmap=window.CaptureRenderedFrame();Assert.That(bitmap,Is.Not.Null);
        var output=Environment.GetEnvironmentVariable("NEXRT_TASKVIEW_TARGET_SCREENSHOT");if(output is not null){bitmap!.Save(output,new Avalonia.Media.Imaging.PngBitmapEncoderOptions());var slider=Avalonia.VisualTree.VisualExtensions.GetVisualDescendants(window).OfType<Avalonia.Controls.Slider>().Single();slider.Value=20;window.UpdateLayout();using var zoomed=window.CaptureRenderedFrame();zoomed!.Save(Path.Combine(Path.GetDirectoryName(output)!,Path.GetFileNameWithoutExtension(output)+"-zoom.png"),new Avalonia.Media.Imaging.PngBitmapEncoderOptions());}window.Close();
    }}
