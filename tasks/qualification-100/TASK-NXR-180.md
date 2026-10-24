# TASK-NXR-180: Lifecycle fault recovery boundary

Status: Planned

Requirement: NXR-180; qualification review 20/100.

## Required behavior

Lifecycle failure shall distinguish recoverable quiescence from a reset-required ownership domain.

## Reason and source finding

A cleanup timeout does not establish that hardware or kernel references have stopped.

## Acceptance criteria

A cooperative timeout with unsettled DMA reports reset required and never permits slot or buffer reuse.

## Verification and deliverables

Inject failed stop and incomplete resource release and verify the reset policy against observed accesses.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No automatic force-delete; finite stop deadline and retained first failure.

## Dependencies

NXR-001, NXR-128, NXR-129

## Inspected sources

- include/rtos.hpp
- src/kernel.cpp
- include/rtos_mutex.hpp
- include/rtos_queue.hpp

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
