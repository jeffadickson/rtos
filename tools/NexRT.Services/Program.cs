using System.Diagnostics;
using System.IO.Compression;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

/// <summary>Reproducible optional-service requirement and gcov evidence maintenance.</summary>
internal static class Program
{
    /// <summary>Runs explicit requirements update or gcov aggregation; never builds or changes a dependency implicitly.</summary>
    public static async Task<int> Main(string[] args)
    {
        try
        {
            if(args.Length<2)throw new ArgumentException("Usage: NexRT.Services requirements ROOT | configuration BUILD | coverage BUILD OUTPUT | mutations ROOT OUTPUT");
            if(args[0]=="requirements")Requirements(Path.GetFullPath(args[1]));
            else if(args[0]=="coverage"&&args.Length==3)await Coverage(Path.GetFullPath(args[1]),Path.GetFullPath(args[2]));
            else if(args[0]=="mutations"&&args.Length==3)await Mutations(Path.GetFullPath(args[1]),Path.GetFullPath(args[2]));
            else if(args[0]=="configuration")Configuration(Path.GetFullPath(args[1]));
            else throw new ArgumentException("Unknown command");
            return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
    }

    /// <summary>Checks effective source removal, not merely feature variable names, without compiling or fetching dependencies.</summary>
    private static void Configuration(string build)
    {
        if(!Directory.Exists(build))throw new DirectoryNotFoundException(build);
        var cache=File.ReadAllLines(Path.Combine(build,"CMakeCache.txt"));
        bool Enabled(string name)=>cache.Contains(name+":BOOL=ON",StringComparer.Ordinal);
        using var commands=JsonDocument.Parse(File.ReadAllText(Path.Combine(build,"compile_commands.json")));
        var sources=commands.RootElement.EnumerateArray().Select(c=>c.GetProperty("file").GetString()!.Replace('\\','/')).ToArray();
        bool Has(string suffix)=>sources.Any(p=>p.EndsWith(suffix,StringComparison.Ordinal));
        if(Has("/src/storage.cpp")!=Enabled("RTOS_STORAGE")||Has("/lfs.c")!=Enabled("RTOS_STORAGE"))throw new IOException("littlefs source stripping mismatch");
        if(Has("/src/virtio_net.cpp")!=Enabled("RTOS_VIRTIO_NET"))throw new IOException("virtio source stripping mismatch");
        if(Enabled("RTOS_BUILD_FIRMWARE")&&Has("/src/monitor.cpp")!=Enabled("RTOS_TASK_TRACE"))throw new IOException("monitor source stripping mismatch");
        if(Has("/src/udp.cpp")!=Enabled("RTOS_LWIP"))throw new IOException("lwIP source stripping mismatch");
        if(Has("/tests/etl_evaluation.cpp")!=(Enabled("RTOS_ETL_EVALUATION")&&!Enabled("RTOS_BUILD_FIRMWARE")))throw new IOException("ETL evaluation stripping mismatch");
        foreach(string forbidden in new[]{"/core/tcp.c","/core/dns.c","/ipv4/dhcp.c","/api/sockets.c","/ipv6/ip6.c"})
            if(Has(forbidden))throw new IOException("Disabled protocol remains in compilation graph: "+forbidden);
        if(!Enabled("RTOS_LWIP")&&sources.Any(p=>p.Contains("/lwip/",StringComparison.Ordinal)||p.Contains("/lwip-src/",StringComparison.Ordinal)))throw new IOException("Disabled lwIP dependency remains in compilation graph");
        Console.WriteLine($"PASS effective source graph: storage={Enabled("RTOS_STORAGE")}, lwip={Enabled("RTOS_LWIP")}, etl={Enabled("RTOS_ETL_EVALUATION")}; no TCP/DNS/DHCP/socket/IPv6 compilation");
    }

    /// <summary>Executes three explicit domain fault controls in an isolated source copy, never editing the active checkout.</summary>
    private static async Task Mutations(string root,string output)
    {
        if(!Directory.Exists(root)||!Directory.Exists(output)||!Directory.Exists(Path.Combine(root,"build")))throw new DirectoryNotFoundException("Mutation root/output must already exist");
        async Task<(int Exit,string Text)> Run(string cwd,string tool,params string[] arguments)
        {
            var start=new ProcessStartInfo(tool){WorkingDirectory=cwd,RedirectStandardOutput=true,RedirectStandardError=true,UseShellExecute=false};
            foreach(var arg in arguments)start.ArgumentList.Add(arg);
            using var process=Process.Start(start)??throw new IOException("Cannot start "+tool);
            var stdout=process.StandardOutput.ReadToEndAsync();var stderr=process.StandardError.ReadToEndAsync();
            using var deadline=new CancellationTokenSource(TimeSpan.FromMinutes(5));
            try{await process.WaitForExitAsync(deadline.Token);}catch(OperationCanceledException){process.Kill(true);await process.WaitForExitAsync();throw new IOException("Mutation command exceeded five minutes");}
            return (process.ExitCode,await stdout+await stderr);
        }
        string copy=Path.Combine(root,"build","mutation-controls-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(copy);
        var listed=await Run(root,"git","ls-files","--cached","--others","--exclude-standard");
        if(listed.Exit!=0)throw new IOException(listed.Text);
        foreach(string relative in listed.Text.Split('\n',StringSplitOptions.RemoveEmptyEntries|StringSplitOptions.TrimEntries))
        {
            if(Path.IsPathRooted(relative)||relative.Split('/').Contains(".."))throw new IOException("Unsafe source-copy path");
            string source=Path.Combine(root,relative);if(!File.Exists(source))continue;
            string target=Path.Combine(copy,relative);string parent=Path.GetDirectoryName(target)!;
            if(!Directory.Exists(parent))Directory.CreateDirectory(parent);File.Copy(source,target);
        }
        var configure=await Run(copy,"cmake","-S",copy,"-B",Path.Combine(copy,"build"),"-G","Ninja","-DCMAKE_BUILD_TYPE=Release","-DRTOS_STORAGE=ON","-DRTOS_VIRTIO_NET=ON","-DRTOS_LWIP=ON","-DRTOS_WEB=OFF","-DRTOS_NATIVE_TOOLS=OFF",
            "-DFETCHCONTENT_SOURCE_DIR_LITTLEFS="+Path.Combine(root,"build/static-deps/littlefs"),"-DFETCHCONTENT_SOURCE_DIR_LWIP="+Path.Combine(root,"build/static-deps/lwip"));
        File.WriteAllText(Path.Combine(output,"mutation-configure.log"),configure.Text);if(configure.Exit!=0)throw new IOException("Mutation configuration failed");
        async Task Build(){var b=await Run(copy,"cmake","--build",Path.Combine(copy,"build"),"--target","littlefs_service","udp_service","monitor_test","virtio_net_test","-j2");if(b.Exit!=0)throw new IOException("Mutation compile failure is not a killed mutant: "+b.Text);}
        await Build();var baseline=await Run(copy,"ctest","--test-dir",Path.Combine(copy,"build"),"-R","littlefs_service|udp_service|monitor|virtio_net","--output-on-failure");
        File.WriteAllText(Path.Combine(output,"mutation-baseline.log"),baseline.Text);if(baseline.Exit!=0)throw new IOException("Mutation baseline is not green");
        var controls=new[]{
            ("mount-state","src/storage.cpp","mounted_=error_==0;","mounted_=false;","littlefs_service"),
            ("nor-overwrite","include/rtos_littlefs.hpp","if((bytes_[b][off+i]&p[i])!=p[i])","if(false)","littlefs_service"),
            ("udp-destination","src/udp.cpp","udp_sendto(pcb,reply,&peer,port)","udp_sendto(pcb,reply,&peer,u16_t(port+1))","udp_service"),
            ("trace-crc","include/rtos_monitor.hpp","crc^=UINT32_MAX;","crc^=0u;","monitor"),
            ("virtio-copy","src/virtio_net.cpp","transmit_bytes_[virtio::header_bytes+i]=bytes[i]","transmit_bytes_[virtio::header_bytes+i]=0","virtio_net")};
        foreach(var (name,path,before,after,test) in controls)
        {
            string file=Path.Combine(copy,path);string original=File.ReadAllText(file);
            if(!original.Contains(before,StringComparison.Ordinal))throw new IOException("Mutation source no longer matches: "+name);
            try{
                File.WriteAllText(file,original.Replace(before,after,StringComparison.Ordinal));await Build();
                var result=await Run(copy,"ctest","--test-dir",Path.Combine(copy,"build"),"-R","^"+test+"$","--output-on-failure");
                File.WriteAllText(Path.Combine(output,"mutation-"+name+".log"),result.Text);
                if(result.Exit==0)throw new IOException("Surviving mutation control: "+name);
                Console.WriteLine("KILLED: "+name);
            }finally{File.WriteAllText(file,original);}
        }
        Console.WriteLine("Five targeted controls killed; this is not a comprehensive mutation score.");
    }

    /// <summary>Adds stable implementation requirements while preserving existing review identities and evidence limitations.</summary>
    private static void Requirements(string root)
    {
        if(!Directory.Exists(root))throw new DirectoryNotFoundException(root);
        string path=Path.Combine(root,"requirements/catalogue.json");
        var rows=JsonNode.Parse(File.ReadAllText(path))!.AsArray();
        foreach(var row in rows)row!["configuration"]=row["configuration"]!.GetValue<string>().Replace("C++17","C++20",StringComparison.Ordinal);
        string[][] authored=[
            ["Project C++20 migration","The project shall build firmware, host tests and analysis configurations using C++20 while preserving existing kernel behavior.","Effective C++ commands use C++20; existing host tests and target static audit pass; Release remains -O3 with loop and SLP vectorization enabled.","CMakeLists.txt,cmake/static-analysis.cmake,cmake/riscue-toolchain.cmake","tests/public_api.cpp","NXR-077"],
            ["Static littlefs service","The optional storage service shall use caller-owned volume and file caches without a general heap and shall never format on mount failure.","Blank mount fails unchanged; explicit format succeeds; a file survives unmount/remount; invalid input and injected I/O fail explicitly.","src/storage.cpp,include/rtos_littlefs.hpp","tests/static_services.cpp","NXR-002"],
            ["NOR simulation and reset boundaries","The NOR simulator shall reject invalid ranges and zero-to-one programming before mutation and support reproducible operation-boundary failures.","NOR bit rules and range tests pass; 64 injected reset boundaries retain the prior or new committed inline value; physical power-loss behavior remains unqualified.","include/rtos_littlefs.hpp","tests/static_services.cpp","NXR-262"],
            ["Pool-only lwIP ownership","The lwIP profile shall use fixed packet, allocation and control-block pools owned by one permanent worker with no general heap fallback.","PBUF and variable-size allocation exhaustion return failure; releasing blocks restores service; complete target ELF contains no forbidden allocator or dynamic initializer.","include/lwip/lwipopts.h,include/lwip/lwippools.h,src/udp.cpp","tests/udp.cpp","NXR-002"],
            ["Wire-level UDP echo","The UDP service shall echo at most 512 payload bytes on its configured IPv4 port through the existing Ethernet adapter with synchronous ownership transfer.","Independently built ARP/IPv4/UDP frames produce matching response payload, ports and checksums; bad checksums, oversized data and exhausted pools do not produce successful echoes.","src/udp.cpp,include/rtos_udp.hpp","tests/udp.cpp,examples/static_service_probe.hpp","NXR-264,NXR-051"],
            ["Independent optional-service removal","Configuration shall exclude dependency resolution, compilation and linked storage for disabled littlefs, lwIP and ETL evaluation features independently.","Disabled and each single-feature build pass; all-off configuration performs no dependency download; linked maps and symbols contain no disabled library implementation.","cmake/static-services.cmake,cmake/configuration.cmake","tests/static_services.cpp,tests/udp.cpp,tests/etl_evaluation.cpp","NXR-078,NXR-079"],
            ["ETL typed-pool evaluation","The ETL evaluation shall measure fixed typed-pool alignment, exhaustion guarding, reuse and initialization constraints before replacing NexRT ownership helpers.","Four aligned packet loans exhaust capacity and return to empty; constructor/exception policy and absence of generation handles are recorded; no kernel replacement is implied.","cmake/static-services.cmake","tests/etl_evaluation.cpp","NXR-002"],
            ["Scheduled static-service firmware","The simulation demo shall run littlefs remount and 32 wire-level UDP echoes in an actual NexRT worker while another worker and scheduling timer make progress.","Instruction model reports completed, one persisted file, 32 echoes, nonzero peer progress, multiple dispatches and timer traps; target ELF passes no-heap/no-initializer audit; cutoff is not completion.","examples/static_services.cpp,sim/interpreter/main.cpp","examples/static_service_probe.hpp,tests/static_services.cpp,tests/udp.cpp","NXR-262,NXR-265"],            ["QEMU RV32 host network profile","The optional QEMU virt RV32 profile shall run the actual single-hart kernel with a 10 MHz CLINT timebase and independent address map.","QEMU boots the audited no-heap ELF; a peer worker progresses while a host socket receives echoes.","ports/qemu-virt-rv32/link.ld,examples/qemu_udp_echo.cpp","tools/NexRT.NetTest/Program.cs","NXR-268"],
            ["Bounded modern virtio network","The optional polled virtio-MMIO driver shall use eight permanently posted RX buffers and one copied outstanding TX with bounded initialization and malformed completion rejection.","Host device model checks copy ownership, busy recovery, invalid features, malformed completions and 16-bit ring wrap; actual QEMU networking transfers packets.","include/rtos_virtio_net.hpp,src/virtio_net.cpp","tests/virtio_net.cpp","NXR-051"],
            ["Host UDP forwarding acceptance","The native host client shall verify byte-exact bounded UDP echo through loopback QEMU forwarding and observe cleanup of its owned simulator.","Seeded 0/1/511/512-byte and randomized datagrams echo; 513-byte input is rejected and following valid input succeeds; owned QEMU stops.","tools/NexRT.NetTest/Program.cs,src/udp.cpp","tests/udp_address.cpp,tools/NexRT.NetTest/Program.cs","NXR-265,NXR-269"],
            ["Optional bounded scheduler recording","Optional RTOS instrumentation shall record scheduler, sleep, priority, mutex and termination events into a boot-static bounded ring without device IO inside kernel hooks.","Disabled build has no recorder symbols; overflow drops new events with observable sequence discontinuity and cumulative loss record; FIFO and wrap checks pass.","include/rtos_monitor.hpp,src/monitor.cpp,src/kernel.cpp,src/mutex.cpp,src/scheduler.hpp,src/sync.cpp","tests/monitor.cpp","NXR-002"],
            ["Framed monitor UART transport","One thread shall drain trace records through bounded partial writes using versioned little-endian COBS frames with CRC-32C and a startup delimiter.","Every encoded frame is bounded at 34 bytes; independent reference decoding and seeded properties pass; blocked UART retains partial frame without blocking producers.","include/rtos_monitor.hpp,include/rtos_monitor_stream.hpp,examples/qemu_udp_echo.cpp","tests/monitor.cpp","NXR-272"],
            ["Native task timeline viewer","The optional CSharp Avalonia application shall display captured or TCP-streamed task activity, base/effective priorities and mutex waits with bounded retention and explicit loss diagnostics.","Parser tests reject corruption and malformed input, handle fragmentation, metadata and sequence wrap; actual QEMU priority-inheritance trace renders in a visually inspected window.","tools/NexRT.TaskView/Core/Trace.cs,tools/NexRT.TaskView/App/Program.cs","docs/task-view.rst","NXR-273"],
            ["Static littlefs file control ownership","The NexRT littlefs adapter shall require no-malloc upstream compilation and permanently own file metadata, configuration and all caches, with one owner and reentry rejection.","Negative no-malloc compile fixture fails; upstream allocator fallback returns null; error cleanup permits slot reuse and callback reentry fails explicitly.","include/rtos_littlefs.hpp,src/storage.cpp","tests/static_services.cpp","NXR-262"]
        ];
        for(int i=0;i<authored.Length;++i)
        {
            string id=$"NXR-{261+i}";var f=authored[i];
            var existing=rows.FirstOrDefault(r=>r!["id"]!.GetValue<string>()==id);
            int index=existing is null?rows.Count:rows.IndexOf(existing);
            JsonArray Paths(string text)=>new(text.Split(',',StringSplitOptions.RemoveEmptyEntries).Select(p=>(JsonNode?)JsonValue.Create(p)).ToArray());
            var record=new JsonObject {
                ["id"]=id,["title"]=f[0],["category"]="Static service integration",["statement"]=f[1],
                ["rationale"]="Reuse pinned libraries while preserving static capacity, optional linkage and honest target evidence.",
                ["acceptance"]=f[2],["verification"]="Host regression and negative fixtures; seeded wire tests; linked ELF audit; RV32 instruction-model run. See requirements/static-services/evidence.rst and requirements/qemu-network/evidence.rst for commands and limitations.",
                ["dependencies"]=Paths(f[5]),["status"]="PartialSourceUnverified",["implementation_files"]=Paths(f[3]),["verification_files"]=Paths(f[4]),
                ["planned_test"]="TEST-"+id,["configuration"]="C++20; independent RTOS_STORAGE/RTOS_LWIP/RTOS_ETL_EVALUATION; Release -O3; larger-RAM simulation explicitly named.",
                ["resource_budget"]="Fixed volume/file caches; four RX packet blocks and four malloc-pool blocks; 512-byte echo ceiling; fixed worker stacks; no general heap.",
                ["review_iteration"]=261+i,["inspected_files"]=Paths(f[3]+","+f[4]),
                ["finding"]="Implementation and finite simulation evidence exist; board flash/MAC timing, full network conformance and formal qualification are not established."
            };
                        if(i>=8){
                record["category"]="QEMU networking and task instrumentation";
                record["verification"]="See requirements/qemu-network/evidence.rst for exact finite execution, coverage and limitations. Source traceability is not full target qualification.";
                record["configuration"]="C++20 -O3 RV32 QEMU; independent RTOS_VIRTIO_NET and RTOS_TASK_TRACE; optional CSharp net10 Avalonia host viewer.";
                record["resource_budget"]="Guest boot-static buffers, default512 trace records and34-byte wire frames; host viewer bounded100000 retained and4000 rendered records; no target general heap.";
                record["finding"]="Finite QEMU and host evidence recorded; physical UART/DMA, target overhead bounds and full platform qualification remain open.";
            }
            if(existing is null)rows.Add((JsonNode)record);else rows[index]=record;
        }
        File.WriteAllText(path,JsonSerializer.Serialize(rows,ServiceJson.Default.JsonArray)+"\n");
        Console.WriteLine("Updated NXR-261..275; no runtime Verified status claimed.");
    }

    /// <summary>Unions covered source lines and sums GCC branch occurrences across translation units; these are distinct aggregation scopes.</summary>
    private static async Task Coverage(string build,string output)
    {
        if(!Directory.Exists(build))throw new DirectoryNotFoundException(build);
        if(!Directory.Exists(Path.GetDirectoryName(output)))throw new DirectoryNotFoundException("Coverage output parent missing");
        if(!Directory.Exists(output))Directory.CreateDirectory(output);
        var lines=new Dictionary<string,Dictionary<int,bool>>(StringComparer.Ordinal);
        var branches=new Dictionary<string,(int Total,int Hit)>(StringComparer.Ordinal);
        int ordinal=0;
        foreach(string data in Directory.EnumerateFiles(build,"*.gcda",SearchOption.AllDirectories).Order(StringComparer.Ordinal))
        {
            string work=Path.Combine(output,$"gcov-{++ordinal:000}");if(!Directory.Exists(work))Directory.CreateDirectory(work);
            var start=new ProcessStartInfo("gcov"){WorkingDirectory=work,RedirectStandardOutput=true,RedirectStandardError=true,UseShellExecute=false};
            foreach(var argument in new[]{"-j","-b",data})start.ArgumentList.Add(argument);
            using var process=Process.Start(start)??throw new IOException("gcov failed to start");
            var stdout=process.StandardOutput.ReadToEndAsync();var stderr=process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync();File.WriteAllText(Path.Combine(work,"gcov.log"),await stdout+await stderr);
            if(process.ExitCode!=0)throw new IOException("gcov failed for "+data);
            foreach(string path in Directory.EnumerateFiles(work,"*.json.gz"))
            {
                using var file=File.OpenRead(path);using var gzip=new GZipStream(file,CompressionMode.Decompress);using var json=await JsonDocument.ParseAsync(gzip);
                foreach(var source in json.RootElement.GetProperty("files").EnumerateArray())
                {
                    string name=source.GetProperty("file").GetString()!;
                    if(!lines.TryGetValue(name,out var counts))lines[name]=counts=[];
                    var b=branches.GetValueOrDefault(name);
                    foreach(var line in source.GetProperty("lines").EnumerateArray())
                    {
                        int number=line.GetProperty("line_number").GetInt32();bool hit=line.GetProperty("count").GetInt64()>0;
                        counts[number]=counts.GetValueOrDefault(number)||hit;
                        foreach(var branch in line.GetProperty("branches").EnumerateArray()){++b.Total;if(branch.GetProperty("count").GetInt64()>0)++b.Hit;}
                    }
                    branches[name]=b;
                }
            }
        }
        if(ordinal==0)throw new IOException("No gcov execution data; run instrumented tests first");
        var report=new System.Text.StringBuilder("file\tlines_hit\tlines_total\tbranches_hit\tbranches_total\n");
        foreach(var (name,counts) in lines.OrderBy(p=>p.Key,StringComparer.Ordinal))
        {var b=branches[name];report.AppendLine($"{name}\t{counts.Count(p=>p.Value)}\t{counts.Count}\t{b.Hit}\t{b.Total}");}
        File.WriteAllText(Path.Combine(output,"coverage.tsv"),report.ToString());
        Console.WriteLine(report.ToString());
    }
}

/// <summary>AOT-compatible metadata for canonical requirement records.</summary>
[JsonSerializable(typeof(JsonArray))]
[JsonSourceGenerationOptions(WriteIndented=true)]
internal partial class ServiceJson:JsonSerializerContext;
