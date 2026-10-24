# TASK-NXR-246: Interrupt storm containment

Status: Planned

Requirement: NXR-246; qualification review 86/100.

## Required behavior

The BSP shall contain a continuously asserted peripheral source without permanently starving the scheduling timer.

## Reason and source finding

Acknowledging a misbehaving device indefinitely can turn bounded driver code into scheduler starvation.

## Acceptance criteria

Hold a level source active; the declared mask or defer policy activates and timer progress remains observable.

## Verification and deliverables

Inject a stuck source while measuring timer and high-priority worker progress externally.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite events-per-entry budget and bounded deferred queue.

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
