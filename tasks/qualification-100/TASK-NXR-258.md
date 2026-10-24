# TASK-NXR-258: Fair RTOS comparison profile

Status: Planned

Requirement: NXR-258; qualification review 98/100.

## Required behavior

Any published NexRT comparison shall match workload semantics, optimization, allocation, clock and enabled services across compared systems.

## Reason and source finding

Small default images cannot fairly be compared with another RTOS configured for additional services.

## Acceptance criteria

An unmatched interrupt profile or omitted feature cost invalidates the comparison label.

## Verification and deliverables

Retain complete build inputs, measurement method and raw repeated samples for every participant.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Comparison scope and memory accounting fixed before measurements.

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
