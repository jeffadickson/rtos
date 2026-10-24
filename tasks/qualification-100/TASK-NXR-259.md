# TASK-NXR-259: Task requirement closure audit

Status: Planned

Requirement: NXR-259; qualification review 99/100.

## Required behavior

Each qualification task shall retain a stable requirement link, specific acceptance criteria, dependency list and unresolved evidence status.

## Reason and source finding

The earlier HIL design CSV was outside the canonical StrictDoc export and needs explicit implementation task traceability.

## Acceptance criteria

Duplicate IDs, missing acceptance, cyclic dependencies and stale generated task text fail the repository check.

## Verification and deliverables

Run negative tracker fixtures and tamper with one generated task to prove stale-output detection.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Exactly one task per imported qualification review; at most 100 in this campaign.

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
