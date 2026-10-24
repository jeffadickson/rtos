# TASK-NXR-204: Packet rejection ordering

Status: Planned

Requirement: NXR-204; qualification review 44/100.

## Required behavior

Packet validation shall reject magic, version, reserved fields, length and CRC errors before command side effects.

## Reason and source finding

A checksum pass is insufficient to authorize a structurally invalid command.

## Acceptance criteria

Each malformed field independently causes rejection and leaves run state, output ownership and command cache unchanged.

## Verification and deliverables

Compare state snapshots around one-field mutations and wrong-endian fixtures.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Constant frame ceiling; no payload-dependent allocation or callback before validation.

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
