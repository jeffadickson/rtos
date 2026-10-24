# TASK-NXR-251: DMA late-completion isolation

Status: Planned

Requirement: NXR-251; qualification review 91/100.

## Required behavior

DMA qualification shall reject a completion for an old ticket even after cancellation and a later transfer starts.

## Reason and source finding

A device completion bit without transfer identity can corrupt the next owner's result.

## Acceptance criteria

Replay old completion before and after new completion; only the current generation changes state or data.

## Verification and deliverables

Use an independently scripted backend and physical delayed IRQ where available.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One active ticket per channel; generations never wrap within object lifetime.

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
