# TASK-NXR-168: Idle residency accounting

Status: Planned

Requirement: NXR-168; qualification review 8/100.

## Required behavior

CPU residency reports shall identify measured busy and idle intervals without counting blocked host simulation time as target execution.

## Reason and source finding

Existing metrics need a calibrated interpretation before they support performance claims.

## Acceptance criteria

A controlled idle-heavy workload and busy loop produce ordered residency results; totals reconcile within declared sampling error.

## Verification and deliverables

Compare target counter accounting with external GPIO intervals and explicit sampling overhead.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Accounting work is constant per transition; error ceiling declared in target counter units.

## Dependencies

NXR-081, NXR-025

## Inspected sources

- include/rtos.hpp
- src/kernel.cpp
- src/scheduler.hpp
- docs/memory.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
