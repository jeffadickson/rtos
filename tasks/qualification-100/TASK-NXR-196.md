# TASK-NXR-196: Trace overflow semantics

Status: Planned

Requirement: NXR-196; qualification review 36/100.

## Required behavior

Trace consumers shall report lost events and forbid complete-history conclusions after overflow.

## Reason and source finding

A bounded trace is useful evidence only when omitted history remains explicit.

## Acceptance criteria

Fill the ring, drop events, drain and refill; loss count is visible and saturates without wrapping.

## Verification and deliverables

Exercise capacity one, full and counter-boundary fixtures and malformed consumer sequence claims.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Configured event capacity only; overflow never blocks producers.

## Dependencies

NXR-072, NXR-073, NXR-094

## Inspected sources

- include/rtos_trace.hpp
- include/rtos_fault_record.hpp
- docs/hardware-in-loop.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
