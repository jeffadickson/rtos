# TASK-NXR-233: UART framing fault sensitivity

Status: Planned

Requirement: NXR-233; qualification review 73/100.

## Required behavior

UART checker qualification shall detect wrong baud, stop-bit violations, truncated records and FIFO overrun distinctly.

## Reason and source finding

Successful text decoding does not qualify framing and overload error detection.

## Acceptance criteria

Inject each fault independently and preserve the first affected byte or record identity.

## Verification and deliverables

Generate controlled waveforms and throttle the checker input sink.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Declared baud tolerance, FIFO depth and maximum record length.

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
