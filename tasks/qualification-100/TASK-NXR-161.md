# TASK-NXR-161: Profile RAM reconciliation

Status: Planned

Requirement: NXR-161; qualification review 1/100.

## Required behavior

The release gate shall reconcile every allocated RAM section and boot-reserved stack against the declared board memory map.

## Reason and source finding

Current static allocation contracts do not establish a complete per-profile board reconciliation.

## Acceptance criteria

Sum all disjoint load and runtime ranges; reject an omitted boot stack and any overlap or address overflow.

## Verification and deliverables

Compare linker map and ELF program headers with a separately authored board manifest; inject each omission.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Zero unexplained RAM bytes; board capacity is a hard ceiling.

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
