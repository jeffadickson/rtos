# TASK-NXR-210: Protocol compatibility negotiation

Status: Planned

Requirement: NXR-210; qualification review 50/100.

## Required behavior

Handshake shall reject incompatible major schemas and negotiate only documented compatible capabilities before CONFIGURE.

## Reason and source finding

Version numbers need an explicit compatibility rule before mixed host and firmware builds are safe.

## Acceptance criteria

Unknown mandatory features and incompatible packet layout fail without arming workers; optional unknown fields follow the stated policy.

## Verification and deliverables

Run old/new schema fixture pairs and unsupported-command probes.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Bounded capability bitmap and finite handshake deadline.

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
