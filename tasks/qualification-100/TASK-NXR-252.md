# TASK-NXR-252: DMA quiescence proof boundary

Status: Planned

Requirement: NXR-252; qualification review 92/100.

## Required behavior

A DMA backend shall report completed or stopped only when no further bus writes from that operation can occur.

## Reason and source finding

The portable channel relies on backend quiescence guarantees that host state tests cannot prove.

## Acceptance criteria

Delay the final write beyond the completion indication; qualification fails a backend that declares quiescence early.

## Verification and deliverables

Instrument bus writes or FPGA markers and compare final access against ownership release.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite drain deadline; uncertain stop enters quarantine.

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
