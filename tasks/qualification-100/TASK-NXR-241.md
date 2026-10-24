# TASK-NXR-241: UART whole-record atomicity

Status: Planned

Requirement: NXR-241; qualification review 81/100.

## Required behavior

Queued UART qualification shall prove that concurrent producer records never interleave within a record across partial transport writes.

## Reason and source finding

The queued implementation preserves pending offsets but needs sustained concurrent transport evidence.

## Acceptance criteria

Three seeded producers emit counted records through one-byte writes; each complete record belongs to one producer and sequence.

## Verification and deliverables

Compare exact output with an independent record parser while forcing full and busy transport responses.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Configured static record slots and message size; no producer waits for device readiness.

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
