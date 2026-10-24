# TASK-NXR-214: Bounded evidence writing

Status: Planned

Requirement: NXR-214; qualification review 54/100.

## Required behavior

Evidence backpressure shall terminate or explicitly degrade the run without silently losing mandatory terminal records.

## Reason and source finding

Durable evidence failures must be distinguished from a healthy target pass.

## Acceptance criteria

Fill the host storage quota and stall the writer; result becomes incomplete and first-fault retention remains accounted for.

## Verification and deliverables

Use a quota-limited sink and injected I/O failures with a healthy control run.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Declared bytes-per-run ceiling and bounded writer queue; no unlimited RAM buffering.

## Dependencies

NXR-076, NXR-094

## Inspected sources

- docs/hil-verification-plan.rst
- requirements/hil-design.csv

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
