# TASK-NXR-201: Fixed-frame wire schema

Status: Planned

Requirement: NXR-201; qualification review 41/100.

## Required behavior

The HIL codec shall encode the documented 512-byte frame using explicit little-endian fields and zero padding.

## Reason and source finding

The wire proposal exists but no frozen executable codec currently establishes compatibility.

## Acceptance criteria

Golden frames match byte for byte on host and target; native struct packing changes do not alter encoding.

## Verification and deliverables

Cross-check independent encoders and a published offset table including all boundary fields.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

512 decoded bytes: 40 header, 468 payload region and 4 CRC bytes.

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
