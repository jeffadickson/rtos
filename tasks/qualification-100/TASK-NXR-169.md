# TASK-NXR-169: Reproducible optimized builds

Status: Planned

Requirement: NXR-169; qualification review 9/100.

## Required behavior

Qualified firmware and interpreter builds shall record effective compiler options and require the designated optimized profile to use -O3.

## Reason and source finding

Requested optimization must be verified from effective commands rather than preset names.

## Acceptance criteria

A substituted -O2 interpreter or wrong language standard rejects the optimized qualification label.

## Verification and deliverables

Inspect compilation databases and embedded build identity; execute a wrong-flags negative fixture.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

C++20 interpreter; -O3 release profile; debug and sanitizer profiles remain separately named.

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
