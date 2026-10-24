# TASK-NXR-194: Replay bundle completeness

Status: Planned

Requirement: NXR-194; qualification review 34/100.

## Required behavior

A replay bundle shall include firmware, source revision, dirty diff, tool versions, configuration, seed and the terminal classification.

## Reason and source finding

A commit ID cannot identify uncommitted source or substituted firmware bytes.

## Acceptance criteria

Removing any mandatory identity makes the bundle incomplete; a clean and dirty build remain distinguishable.

## Verification and deliverables

Validate bundle schema against intentionally missing fields and hash mismatches.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Host storage quota declared per run; archive exhaustion cannot be reported as a pass.

## Dependencies

NXR-072, NXR-073, NXR-094

## Inspected sources

- include/rtos_trace.hpp
- include/rtos_fault_record.hpp
- docs/hardware-in-loop.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
