# TASK-NXR-231: Independent UART content oracle

Status: Planned

Requirement: NXR-231; qualification review 71/100.

## Required behavior

The external checker shall validate expected per-worker content and sequence in addition to CRC and framing.

## Reason and source finding

A valid CRC only proves consistency with the sender's bytes, not correct job execution.

## Acceptance criteria

A wrong payload with a recomputed valid CRC fails; a valid multiworker stream passes.

## Verification and deliverables

Feed synthetic UART waveforms and physical target streams with independent expected payload generation.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed per-worker oracle state and declared maximum worker count.

## Dependencies

NXR-099

## Inspected sources

- docs/hardware-in-loop.rst
- docs/hil-verification-plan.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
