# TASK-NXR-205: Session and boot identity binding

Status: Planned

Requirement: NXR-205; qualification review 45/100.

## Required behavior

Every state-changing command shall match the negotiated session nonce and current boot identity.

## Reason and source finding

Sequence numbers alone cannot isolate sessions across reboot and USB enumeration.

## Acceptance criteria

A captured pre-reset command and a command from another host session cannot start or stop work.

## Verification and deliverables

Replay frames across reconnection and controlled resets with unchanged command numbers.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

128-bit session nonce and 64-bit boot identity per wire proposal; entropy quality stated separately.

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
