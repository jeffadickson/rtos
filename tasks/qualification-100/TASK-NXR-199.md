# TASK-NXR-199: Endurance completion criteria

Status: Planned

Requirement: NXR-199; qualification review 39/100.

## Required behavior

An endurance result shall require its declared target operations and target duration independently of host cutoff.

## Reason and source finding

A twelve-hour host window does not imply twelve hours of simulated target execution.

## Acceptance criteria

A host deadline reached before target goals reports wall limit or incomplete rather than passed.

## Verification and deliverables

Run deliberately slow simulation with short host cutoff and a separate completed control.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite host cutoff plus cleanup allowance; target goals recorded before start.

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
