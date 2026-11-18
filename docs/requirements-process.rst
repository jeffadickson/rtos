Requirements review and implementation process
==============================================

The requirements tracker follows the local BenchForge pattern: stable
requirement identities, a StrictDoc-format source export, CSV traceability,
and C# validation. The canonical source here is requirements/catalogue.json.
The C# tracker generates the canonical views, and the ``strictdoc`` build target
invokes the real StrictDoc engine for parsing and HTML export. Sphinx and
StrictDoc are required third-party tooling exceptions; no custom Python
maintenance scripts are introduced.

Review scope
------------

The requested 100 iterations are 100 distinct, source-informed requirement
design reviews, organized into four batches of 25. Each review records the
requirement inspected, source paths, a concrete finding, a refined normative
statement, measurable acceptance, planned verification, configuration and a
finite resource budget. This does not mean 100 complete kernel implementation
cycles, 100 human reviews, or evidence of runtime verification.

A builder independently assesses each batch at 25, 50, 75 and 100 reviews.
Actual checkpoint commands build host tools and the unchanged C++ kernel,
execute host tests and static-allocation checks, and build strict Sphinx HTML.
Logs and hashed source/catalogue snapshots are retained under
requirements/checkpoints. New services remain Planned until implementation
and appropriate execution evidence exist. Source-linked existing behavior is
SourceUnverified or PartialSourceUnverified; host helper tests are not target
interrupt validation. This baseline intentionally cannot label a requirement
Verified without a separate qualified runtime-evidence workflow.

C# commands
-----------

.. code-block:: sh

   cmake --preset host
   cmake --build --preset host --target requirements_tool
   build/host/native-tools/NexRT.Requirements/NexRT.Requirements generate .
   build/host/native-tools/NexRT.Requirements/NexRT.Requirements check .
   build/host/native-tools/NexRT.Requirements/NexRT.Requirements self-test .
   cmake --build --preset host --target requirements_check

Generation is explicit. Check mode and CTest never rewrite stale documents.
CMake builds the tracker with XDoc XML output, and the documentation target
requires the tracker check. Dependencies must form a DAG. Path evidence must
refer to existing files inside the repository without traversal or symlinks.
Stable requirement IDs are independent of review ordering. Historical builder
snapshots retain the earlier baseline when later batches are appended.

Open-source C++ static checking
-------------------------------

`Cppcheck <https://cppcheck.sourceforge.io/>`_ provides open-source C/C++ static
analysis and supports compilation-database input. Its upstream manual explicitly
addresses nonstandard syntax common in embedded software.
`clang-tidy <https://clang.llvm.org/extra/clang-tidy/index.html>`_ provides C++
checks and can run Clang Static Analyzer checks as well.
`Clang Static Analyzer <https://clang.llvm.org/docs/ClangStaticAnalyzer.html>`_
provides path-sensitive defect analysis.

The CMake ``cppcheck``, ``clang_tidy`` and combined ``cpp_analysis`` targets
now provide optional checks. See :doc:`tooling` for commands and scope.
The release tool profile should combine Cppcheck and clang-tidy/Clang Static
Analyzer, with a pinned version, reviewed check list, recorded findings, and
narrowly justified suppressions. Use the correct target, includes, definitions
and compile_commands.json; native test settings alone do not describe RV32
inline assembly or machine-mode code. Assembly ABI correctness, interrupt
interleavings, stack bounds and latency still need target-specific verification.
Static analysis alone does not prove leak freedom or certification. Wrappers,
result parsing, baseline comparison and report generation must use C# with XDoc.
