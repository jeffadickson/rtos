# TASK-NXR-255: DMA quarantine recovery domain

Status: Planned

Requirement: NXR-255; qualification review 95/100.

## Required behavior

A quarantined channel shall remain unavailable until the declared reset domain proves hardware access has ended.

## Reason and source finding

Software state reset cannot stop a bus master that still owns memory.

## Acceptance criteria

Repeated begin and cancel calls cannot release quarantined staging; reset of an unrelated peripheral does not recover it.

## Verification and deliverables

Inject stop failure, attempt reuse and verify only the qualified reset clears ownership.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Staging remains reserved during quarantine; no automatic generation reset.

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
