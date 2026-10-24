# TASK-NXR-240: Observer storage overflow result

Status: Planned

Requirement: NXR-240; qualification review 80/100.

## Required behavior

A checker shall report capture loss and withhold complete-trace or timing-pass claims after overflow.

## Reason and source finding

An apparently valid tail of captured output can hide dropped earlier failures.

## Acceptance criteria

Exceed FIFO and capture depth; loss counters and terminal incomplete status appear while a bounded control passes.

## Verification and deliverables

Drive maximal-rate streams and delayed host drains at the declared input limit.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed FIFO and capture RAM; no silent circular overwrite of mandatory first-fault evidence.

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
