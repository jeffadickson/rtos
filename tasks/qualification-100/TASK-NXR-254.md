# TASK-NXR-254: DMA failed-start side effects

Status: Planned

Requirement: NXR-254; qualification review 94/100.

## Required behavior

A backend start failure shall guarantee that no transfer was issued and no application buffer remains hardware-owned.

## Reason and source finding

The interface's false-start guarantee is essential to safe immediate reuse.

## Acceptance criteria

A false start result followed by any bus activity fails; retry preserves channel ownership and counters.

## Verification and deliverables

Script rejected starts and monitor bus activity or FPGA transfer counters.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No application pointer retained; fixed channel staging allocation.

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
