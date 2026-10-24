# TASK-NXR-253: DMA cache-line exclusivity

Status: Planned

Requirement: NXR-253; qualification review 93/100.

## Required behavior

DMA buffers shall own all cache lines touched by prepare and finish maintenance operations.

## Reason and source finding

Correct byte bounds do not prevent cache maintenance from touching neighboring objects.

## Acceptance criteria

Adjacent application canaries survive transmit and receive tests with unaligned requested lengths.

## Verification and deliverables

Exercise first and last cache-line boundaries on a cached board with independently observed buffer contents.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Staging alignment and rounded capacity cover the hardware cache-line size.

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
