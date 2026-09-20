# Specification Quality Checklist: FB2 Reader Hardening

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-09-20
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

Three scope decisions were settled with the requester before drafting, so no
[NEEDS CLARIFICATION] markers were carried into the spec:

1. **#6 / #7 / #8** — the spec commits to bounding the chapter ceiling and windowing the
   chapter list (the cheap fix), leaving the SD-resident LUT (#8) as a documented upgrade
   path rather than building it here.
2. **#4 (background prefetch)** — out of scope; it needs its own Discussion and spec.
3. **#10 (untitled sections)** — labelled from the chapter's own first line of text, with the
   localized placeholder kept as the fallback.

**Post-plan amendment (2026-09-20)**: the ceiling was re-derived from measurement rather than
judgement — a firmware RAM report, `sizeof` values read from a riscv32 object file, and 2,899
real FB2 books parsed through this repo's own parser. Three spec items changed as a result:
FR-001 now requires the ceiling to be justified against measured device figures; SC-002 is
restated against the measured usable heap instead of a host test's synthetic figure; and FR-018
no longer caps real titles, because capping them was measured to save 3%. See research.md M1–M3
and Decisions 1 and 4.

Validation notes on two items that needed a second pass:

- *No implementation details*: the spec names files and line numbers in the Context and in
  FR-005 / FR-007. Kept deliberately — the project constitution's Evidence Over Claims
  principle requires a change to cite the code that justifies it, and every citation here is
  evidence for a requirement, not a prescription of how to satisfy it. No requirement names a
  function to write, a data structure to use, or a value to choose; FR-001/FR-002 state the
  ceiling's property and leave its number to planning.
- *Success criteria are technology-agnostic*: SC-002 and SC-003 cite byte figures. Kept —
  memory is this project's primary user-facing constraint (a book that will not open is a user
  outcome), and the constitution names host allocation counts as the accepted proxy. Both are
  stated as outcomes relative to a measured baseline, not as internals.

Two named values are deliberately left for `/speckit-plan` rather than fixed here: the chapter
ceiling's number (and whether it is board-dependent), and the mechanism that gives the cover
page a position without renumbering (FR-011 / FR-012 fix the constraints it must satisfy).
