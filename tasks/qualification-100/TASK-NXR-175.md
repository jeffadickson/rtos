# TASK-NXR-175: Thread reclamation barrier

Status: Planned

Requirement: NXR-175; qualification review 15/100.

## Required behavior

A lifecycle slot shall become reusable only after all scheduler, waiter, observer and ownership references to its old generation are retired.

## Reason and source finding

Thread completion alone cannot prove that all kernel references are gone.

## Acceptance criteria

Attempt reuse with one stale waiter or pending observer reference and receive a bounded refusal.

## Verification and deliverables

Inject each retained reference separately and verify no stack or control-block overwrite.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Reference metadata bounded by configured threads and waiters; no runtime stack growth.

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
