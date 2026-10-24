# TASK-NXR-257: Evidence-qualified feature claims

Status: Planned

Requirement: NXR-257; qualification review 97/100.

## Required behavior

Release notes shall link every supported feature and board claim to its configuration and retained acceptance evidence.

## Reason and source finding

Source presence and generated requirements are not sufficient evidence of target support.

## Acceptance criteria

Delete or mismatch an evidence fingerprint and the corresponding qualified claim is rejected.

## Verification and deliverables

Cross-check release manifest, requirement IDs, board identities and evidence hashes.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One finite support matrix; planned work is never promoted by documentation generation.

## Dependencies

NXR-134, NXR-135, NXR-159

## Inspected sources

- include/rtos_dma.hpp
- src/dma.cpp
- tests/dma.cpp
- docs/dma.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
