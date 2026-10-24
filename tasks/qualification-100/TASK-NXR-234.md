# TASK-NXR-234: Physical latency witnesses

Status: Planned

Requirement: NXR-234; qualification review 74/100.

## Required behavior

Latency tests shall measure stimulus arrival, handler entry and worker dispatch as separate physical events.

## Reason and source finding

USB timestamps cannot separate interrupt latency from host transport latency.

## Acceptance criteria

A delayed worker with prompt ISR entry is classified as scheduling delay rather than interrupt-entry delay.

## Verification and deliverables

Drive an external stimulus and capture three GPIO witnesses with calibrated timestamps.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Per-board limits declared before arming; GPIO overhead measured.

## Dependencies

NXR-099

## Inspected sources

- docs/hardware-in-loop.rst
- docs/hil-verification-plan.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
