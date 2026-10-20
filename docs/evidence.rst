Retained engineering evidence
================================================================================

``NexRT.Evidence`` is an XDoc-documented C# tool published as a native executable
by CMake. It preserves explicitly selected local logs, manifests and unstripped
ELFs in a fresh directory. It does not upload artifacts or change Git history.

.. code-block:: sh

   build/host/native-tools/NexRT.Evidence/NexRT.Evidence collect . \
     requirements/evidence/my-checkpoint build/checkpoint-300-tests.log \
     build/nano/demo.elf build/arm-stress/runs/CHOSEN_RUN
   build/host/native-tools/NexRT.Evidence/NexRT.Evidence verify \
     requirements/evidence/my-checkpoint

Inputs must resolve below the workspace root. The tool rejects links/reparse
points, traversal, existing destinations, more than 512 selected files, files
over four MiB, or more than 32 MiB total. Tree traversal is bounded separately.
It copies into an owned staging directory, checks source/copy/source hashes,
verifies the result and renames the directory into place. Its TSV manifest
records relative paths, lengths and SHA-256 values. Verification rejects missing,
changed, duplicate and unlisted files. Collection self-tests exercise round-trip,
tampering, overwrite, traversal, duplicate/unlisted files and a byte-limit failure.

An archive is a consistency record, not a signature or certification claim.
Someone who can replace both files and manifest can change it. The repository
commit identifies the retained package; no self-referential final commit hash
is embedded in artifacts generated before that commit.

Keep complete redirected CTest transcripts: a focused rerun replaces CTest's
``LastTest.log``. Include detailed Memcheck logs, successful simulation campaign
markers and manifests, relevant build/configuration logs, audited ELF identities,
and the requirements source/traceability records. Expected negative analyzer
diagnostics must be identified separately from passing workload logs.

The 300-pass campaign ledger is :doc:`implementation-300`; checkpoint summaries
and automated panel dispositions are in ``requirements/checkpoints``. Deferred
coverage and hardware qualifications remain visible there and in the manual.
