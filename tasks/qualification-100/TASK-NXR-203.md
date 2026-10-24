# TASK-NXR-203: COBS resynchronization bounds

Status: Planned

Requirement: NXR-203; qualification review 43/100.

## Required behavior

The decoder shall bound encoded accumulation and recover at a delimiter after malformed or oversized input.

## Reason and source finding

USB and serial read boundaries do not correspond to logical frame boundaries.

## Acceptance criteria

All-zero payload and maximal nonzero runs round-trip; oversized or truncated frames never grow storage or dispatch commands.

## Verification and deliverables

Feed arbitrary read fragments, consecutive delimiters and corrupted code bytes followed by a valid frame.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

At most 516 encoded bytes including delimiter; statically reserved 576-byte slots.

## Dependencies

NXR-137

## Inspected sources

- docs/hardware-in-loop.rst
- docs/hil-verification-plan.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
