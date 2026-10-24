# TASK-NXR-162: Allocation gate detection controls

Status: Planned

Requirement: NXR-162; qualification review 2/100.

## Required behavior

The allocator audit shall prove detection of each forbidden allocator family using deliberately linked negative fixtures.

## Reason and source finding

A passing symbol scan alone cannot show that archive extraction and renamed wrappers are detected.

## Acceptance criteria

Fixtures referencing malloc, new, aligned allocation and allocator wrappers fail; a heap-free control passes.

## Verification and deliverables

Build positive and negative ELF fixtures with the same release flags and linker garbage collection.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Zero permitted allocator references; fixtures remain host build artifacts.

## Dependencies

NXR-081, NXR-025

## Inspected sources

- include/rtos.hpp
- src/kernel.cpp
- src/scheduler.hpp
- docs/memory.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
