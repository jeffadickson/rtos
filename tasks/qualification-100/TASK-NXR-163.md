# TASK-NXR-163: Critical section measurement coverage

Status: Planned

Requirement: NXR-163; qualification review 3/100.

## Required behavior

The qualification report shall enumerate every interrupt-masked path and its input-dependent work bound.

## Reason and source finding

The kernel uses local interrupt serialization, so latency claims need coverage of all masked work.

## Acceptance criteria

Queue maximum copies, waiter scans, donation chains and trace operations each have a source path and measured bound.

## Verification and deliverables

Instrument target mask entry and exit; test minimum and maximum configured capacities and calibrated probe overhead.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Per-profile maximum mask duration is declared before measurement; no unbounded callback under mask.

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
