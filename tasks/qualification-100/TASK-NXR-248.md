# TASK-NXR-248: Watchdog progress quorum

Status: Planned

Requirement: NXR-248; qualification review 88/100.

## Required behavior

Watchdog feeding shall require progress from every mandatory worker in the current supervision epoch.

## Reason and source finding

A heartbeat from one healthy monitor cannot establish overall scheduler or application progress.

## Acceptance criteria

Freeze one required worker while the monitor stays alive; feeding stops and the declared reset is observed.

## Verification and deliverables

Inject worker stalls at differing priorities and compare independent reset timing.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed worker bitmap and finite epoch; optional workers explicitly excluded.

## Dependencies

NXR-041, NXR-119, NXR-149

## Inspected sources

- include/rtos_uart.hpp
- docs/drivers.rst
- tests/uart.cpp

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
