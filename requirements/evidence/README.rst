Retained checkpoint evidence
============================

``checkpoint-300`` contains explicitly selected engineering evidence collected
and verified by the native XDoc C# NexRT.Evidence tool. ``manifest.tsv`` records
SHA-256, byte count and retained relative path. Under ``inputs/`` paths mirror
their original workspace-relative locations. Run ``NexRT.Evidence verify`` on
the collection directory to detect changed, missing, duplicate or extra files.

The final matrix is summarized in ../checkpoints/implementation-300.txt.
Complete redirected host transcripts are retained because focused reruns can
replace CTest LastTest.log. Detailed sanitizer/Memcheck logs, current ARM run
manifests and logs, the UART run, unstripped ELF images, compiler versions,
native-tool digests and requirement exports are included.

``negative-asan.log``, ``negative-ubsan.log``, ``negative-valgrind.log`` and
``checkpoint-300-negative-audit.log`` are EXPECTED FAILURE diagnostics from
controlled detection gates. Their corresponding CTest/contract gate passes
only when the intended diagnostic and failing exit are observed.

Source changes and each checkpoint are identified by repository history. Final
panel dispositions are in ../checkpoints/panel-300.txt, outside this pre-review
collection to avoid a circular commit identity. Hash consistency is not artifact
authenticity, certification, physical board execution or worst-case timing proof.
