# TASK-NXR-202: CRC coverage controls

Status: Planned

Requirement: NXR-202; qualification review 42/100.

## Required behavior

The codec shall validate CRC-32C over all first 508 decoded bytes including padding before dispatch.

## Reason and source finding

Validating only meaningful payload would leave framing metadata and padding unchecked.

## Acceptance criteria

Single-bit changes in header, payload and padding fail; the standard check string yields E3069283.

## Verification and deliverables

Use independent bitwise and optimized implementations plus fixed known-answer frames.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Four-byte little-endian CRC; polynomial 82F63B78 with declared initial and final XOR.

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
