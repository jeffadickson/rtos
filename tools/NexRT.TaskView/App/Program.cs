using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;
using NexRT.TaskView.Core;
namespace NexRT.TaskView;
internal static class Program
{
    [STAThread] public static int Main(string[] args)
    {
        if(args.Length!=0 && (args.Length!=2 || args[0] is not ("--trace" or "--serial"))) { Console.Error.WriteLine("Usage: NexRT.TaskView [--trace FILE | --serial HOST:PORT]"); return 2; }
        App.Arguments=args; Build().StartWithClassicDesktopLifetime(args); return 0;
    }
    public static AppBuilder Build()=>AppBuilder.Configure<App>().UsePlatformDetect();
}
internal sealed class App : Application
{
    internal static string[] Arguments=[];
    public override void Initialize() { Styles.Add(new FluentTheme()); RequestedThemeVariant=Avalonia.Styling.ThemeVariant.Dark; }
    public override void OnFrameworkInitializationCompleted()
    {
        if(ApplicationLifetime is IClassicDesktopStyleApplicationLifetime life)life.MainWindow=new ViewerWindow(Arguments);
        base.OnFrameworkInitializationCompleted();
    }
}
internal sealed class ViewerWindow : Window
{
    private TraceDecoder decoder=new();
    private TraceStore store=new();
    private readonly Timeline timeline=new();
    private readonly TextBlock status=new(){Text="No trace loaded · target timestamps are raw timer counts",Foreground=Brushes.LightSlateGray};
    private readonly TextBox source=new(){PlaceholderText="Capture path or localhost:5556",Width=420};
    private readonly TextBox details=new(){IsReadOnly=true,AcceptsReturn=true,TextWrapping=TextWrapping.Wrap,FontFamily=new("Consolas"),FontSize=12,Text="Select an event marker to inspect its exact fields.",MinHeight=100};
    private CancellationTokenSource? cancellation;
    public ViewerWindow(string[] args)
    {
        Title="NexRT · Task View"; Width=1280; Height=940; MinWidth=850; MinHeight=600; Background=Brush.Parse("#101622");
        var title=new TextBlock{Text="NexRT  /  TASK VIEW",FontSize=27,FontWeight=FontWeight.Bold};
        var subtitle=new TextBlock{Text="See execution, blocking and priority inheritance across your target.",Foreground=Brushes.LightSlateGray,Margin=new(0,5,0,20)};
        var replay=new Button{Content="Open capture"}; replay.Click+=async(_,_)=>await Run(false,source.Text??"");
        var connect=new Button{Content="Connect UART TCP"}; connect.Click+=async(_,_)=>await Run(true,source.Text??"");
        var stop=new Button{Content="Disconnect"}; stop.Click+=(_,_)=>cancellation?.Cancel();
        var toolbar=new StackPanel{Orientation=Avalonia.Layout.Orientation.Horizontal,Spacing=10}; toolbar.Children.Add(source); toolbar.Children.Add(replay); toolbar.Children.Add(connect); toolbar.Children.Add(stop);
        var head=new StackPanel{Margin=new(24,22,24,16)}; head.Children.Add(title); head.Children.Add(subtitle);head.Children.Add(toolbar); var zoom=new Slider{Minimum=1,Maximum=40,Value=1,Width=240};zoom.ValueChanged+=(_,_)=>timeline.MinWidth=950*zoom.Value;var zoomRow=new StackPanel{Orientation=Avalonia.Layout.Orientation.Horizontal,Spacing=14,Margin=new(0,10,0,0)};zoomRow.Children.Add(new TextBlock{Text="Timeline zoom",VerticalAlignment=Avalonia.Layout.VerticalAlignment.Center,Foreground=Brushes.LightSlateGray});zoomRow.Children.Add(zoom);zoomRow.Children.Add(new TextBlock{Text="Scroll horizontally to inspect short mutex / inheritance events",VerticalAlignment=Avalonia.Layout.VerticalAlignment.Center,Foreground=Brushes.LightSlateGray});head.Children.Add(zoomRow);
        var foot=new StackPanel{Margin=new(24,10,24,20),Spacing=10}; foot.Children.Add(new TextBlock{Text="● Running   ● Ready   ● Block / sleep   ● Priority change   ◆ Mutex wait → owner",Foreground=Brushes.LightSlateGray});foot.Children.Add(status);foot.Children.Add(details);
        var grid=new Grid{RowDefinitions=new("Auto,*,Auto")};grid.Children.Add(head);Grid.SetRow(foot,2);grid.Children.Add(foot);
        var scroll=new ScrollViewer{Content=timeline,HorizontalScrollBarVisibility=Avalonia.Controls.Primitives.ScrollBarVisibility.Auto,VerticalScrollBarVisibility=Avalonia.Controls.Primitives.ScrollBarVisibility.Auto,Margin=new(24,0)};Grid.SetRow(scroll,1);grid.Children.Add(scroll);Content=grid;
        timeline.Selected=e=>details.Text=$"{e.Kind} · task {e.Thread} → related {e.Related}   sequence {e.Sequence}\nTimer count {e.Timestamp}   base priority {e.BasePriority}   effective priority {e.EffectivePriority}   flags 0x{e.Flags:x4}   value {e.Value}\n{timeline.DescribeInterval(e)}\n{(e.Kind==EventKind.MutexWait?"Wait edge reports the owner at instrumentation time; inversion duration requires complete scheduling evidence.":e.Name)}";
        Closed+=(_,_)=>cancellation?.Cancel();
        if(args.Length==2)Opened+=async(_,_)=>await Run(args[0]=="--serial",args[1]);
    }
    internal async Task Run(bool live,string input)
    {
        cancellation?.Cancel(); var current=new CancellationTokenSource();cancellation=current;decoder=new();store=new();timeline.Update([]);details.Text="Select an event marker to inspect its exact fields.";source.Text=input;status.Text=live?"Connecting to UART…":"Reading capture…";
        // Decode on the UI dispatcher: ordering and mutable store ownership are explicit.
        void Consume(byte[] bytes)=>Dispatcher.UIThread.Invoke(()=>{if(cancellation!=current)return;store.Add(decoder.Feed(bytes));timeline.Update(store.Events);status.Text=$"{store.Events.Count:N0} retained · {decoder.Rejected:N0} rejected frames · {store.SequenceGaps:N0} sequence discontinuities · {store.Evicted:N0} evicted · target drops {(store.TargetDropped.HasValue?$"{store.TargetDropped.Value:N0}":"unknown")} · clock {(store.ClockHz.HasValue?$"{store.ClockHz.Value:N0} Hz":"unknown")}";});
        try { var result=await Task.Run(()=>live?TraceCommands.ConnectAsync(input,Consume,current.Token):TraceCommands.ReplayAsync(input,Consume,current.Token)); if(cancellation==current)status.Text+=$" · {result.Message}"; }
        catch(OperationCanceledException) { if(cancellation==current)status.Text+=" · disconnected"; }
    }
}
internal sealed class Timeline : Control
{
    private TraceEvent[] events=[];
    private TraceEvent[] raw=[];
    private readonly Dictionary<TraceEvent,StateInterval> intervals=new(ReferenceEqualityComparer.Instance);
    private byte[] threads=[];
    private readonly Dictionary<byte,string> names=new();
    private readonly List<(Rect Bounds,TraceEvent Event)> markers=[];
    internal Action<TraceEvent>? Selected;
    internal string DescribeInterval(TraceEvent e) { var interval=intervals[e];return interval.End.HasValue?$"Observed interval ends at {interval.End.Value}":interval.UnknownReason=="Instant event"?"Instant event (no duration)":$"Interval unknown: {interval.UnknownReason}"; }
    internal void Update(IEnumerable<TraceEvent> records)
    {
        var all=records.ToArray();raw=all;if(all.Length==0)names.Clear();foreach(var e in all.Where(e=>e.Kind==EventKind.ThreadInfo))names[e.Thread]=e.Name??"";
        // Bound the visible window independently of retention, so repaint cost stays predictable.
        var visible=all.Select((e,i)=>(Event:e,Index:i)).Where(item=>item.Event.Kind is not (EventKind.ThreadInfo or EventKind.ClockInfo or EventKind.Loss)).TakeLast(4000).ToArray();events=visible.Select(item=>item.Event).ToArray();intervals.Clear();foreach(var item in visible)intervals[item.Event]=TimelineIntervals.Derive(raw,item.Index);threads=events.Select(e=>e.Thread).Distinct().Order().ToArray();Height=Math.Max(340,threads.Length*66+70);InvalidateVisual();
    }
    public Timeline() { MinWidth=950; PointerPressed+=(_,e)=>{var p=e.GetPosition(this);var hit=markers.LastOrDefault(x=>x.Bounds.Contains(p));if(hit.Event is not null)Selected?.Invoke(hit.Event);}; }
    public override void Render(DrawingContext context)
    {
        base.Render(context);markers.Clear();
        void Text(string s,double x,double y,IBrush? b=null,double size=12)=>context.DrawText(new FormattedText(s,System.Globalization.CultureInfo.InvariantCulture,FlowDirection.LeftToRight,new Typeface("Segoe UI"),size,b??Brushes.LightSlateGray),new Point(x,y));
        if(events.Length==0){Text("Your target's story starts here",30,65,Brushes.White,24);Text("Enable optional instrumentation, then open a binary capture or connect the QEMU monitor UART.",30,112);Text("No activity is synthesized. Rejected frames and sequence gaps remain visible.",30,139);return;}
        ulong min=events.Min(e=>e.Timestamp), max=events.Max(e=>e.Timestamp);double span=Math.Max(1,(double)(max-min));double width=Math.Max(700,Bounds.Width-205);
        double X(ulong t)=>185+((double)(t-min)/span)*width;
        Text($"TIMER COUNTS   {min} → {max}     /     latest {events.Length:N0} events",185,12);
        for(int i=0;i<threads.Length;i++)
        {
            double y=50+i*66;byte id=threads[i];context.DrawRectangle(Brush.Parse(i%2==0?"#172131":"#141C2A"),null,new Rect(0,y,Bounds.Width,62));Text(names.GetValueOrDefault(id,$"Task {id}"),12,y+8,Brushes.White,14);Text($"ID {id}  ·  base / effective",12,y+32);
            var lane=events.Where(e=>e.Thread==id).ToArray();
            for(int n=0;n<lane.Length;n++)
            {
                var e=lane[n];double x=X(e.Timestamp);
                IBrush color=e.Kind switch{EventKind.Dispatch=>Brush.Parse("#49D4BB"),EventKind.Ready=>Brush.Parse("#669DF6"),EventKind.Priority=>Brush.Parse("#C69AF8"),EventKind.MutexWait=>Brush.Parse("#FFB454"),_=>Brush.Parse("#68758D")};
                var interval=intervals[e]; double w=interval.End.HasValue?Math.Max(4,X(interval.End.Value)-x):4;if(e.Kind is EventKind.Dispatch or EventKind.Ready or EventKind.Block or EventKind.Sleep)context.DrawRectangle(color,null,new Rect(x,y+19,w,20),3,3);
                else context.DrawRectangle(color,null,new Rect(x,y+14,5,30),2,2);
                if(interval.UnknownReason is not null && interval.UnknownReason!="Instant event")Text("?",x,y+2,Brushes.Orange,10);
                markers.Add((new Rect(x-2,y+8,Math.Max(w,8),45),e));
                if(e.Kind==EventKind.MutexWait){int owner=Array.IndexOf(threads,e.Related);if(owner>=0)context.DrawLine(new Pen(color,1),new Point(x,y+38),new Point(x,50+owner*66+18));if(owner>=0){double tip=50+owner*66+18;double direction=owner<i?1:-1;context.DrawLine(new Pen(color,1),new Point(x,tip),new Point(x-4,tip+6*direction));context.DrawLine(new Pen(color,1),new Point(x,tip),new Point(x+4,tip+6*direction));}}
                if(e.Kind==EventKind.Priority || e.Kind==EventKind.MutexWait)Text($"{e.BasePriority}/{e.EffectivePriority}",x+5,y+43,color,10);
            }
        }
    }
}
