# TASK-NXR-164: Stack evidence completeness

Status: Planned

Requirement: NXR-164; qualification review 4/100.

## Required behavior

The stack report shall distinguish untouched sentinel bytes from proven unused call-chain space.

## Reason and source finding

Watermarks are observations and cannot independently bound unexecuted paths.

## Acceptance criteria

A deliberately deep call and an interrupt at its deepest point are detected; incomplete scanning is reported as incomplete.

## Verification and deliverables

Combine compiler stack usage, call-chain review and target watermark fixtures including boot and fault paths.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Every enabled stack has a byte ceiling including nested exception frames.

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
