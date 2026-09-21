# Specification Quality Checklist: FB2 Next-Chapter Prefetch

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-09-21
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — see Notes
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] Every numeric limit or threshold cites the measurement that chose it (Constitution IV)
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

**Deliberate exception — "no implementation details".** FR-010 and FR-012 name
in-tree files, line ranges and constant values, and the Assumptions section names
`Fb2Section`'s one-shot layout and the `.crosspoint/fb2_<hash>/sections/` cache
path. This is not leakage: Constitution Principle IV requires proposed changes to
cite the specific files and lines that justify them, and requires any numeric
threshold to cite the measurement that chose it. The two constraints in question —
"do not add a second execution context" and "reuse the existing heap gate rather
than invent a number" — are unverifiable as requirements without those citations.
Every *behavioural* requirement (FR-001 to FR-009, FR-013 to FR-020) and every
success criterion is stated without reference to code.

**Numbers deliberately not fixed.** The settle interval before preparation starts
(FR-004) and the size of a preparation increment (FR-007) are named as
requirements but given no value, because none has been measured. Constitution IV
forbids writing down a round number with a plausible story attached; both MUST be
chosen from an on-device measurement during `/speckit-plan`. Whether FB2
preparation needs a heap gate above the reader's existing 32 KB / 16 KB is flagged
the same way in FR-012.

**Resolved open question from issue #4.** The issue asks "Should apply to EPUB too,
or FB2 only?" — answered FB2 only (FR-020, Out of Scope). EPUB already mitigates
boundary cost with resumable builds and partial section files; FB2 does not, and
FB2 is where the stall was measured.

**Open questions from issue #4 deferred to planning, not to clarification.** The
issue's remaining design questions — the single-task constraint, the second
in-flight layout's memory budget, and storage-mutex contention — are answered in
this spec as *constraints* (FR-010, FR-011, FR-012, FR-014) rather than as design
choices, so planning is bounded without the spec prescribing a mechanism.

Items marked incomplete require spec updates before `/speckit-clarify` or
`/speckit-plan`. None are outstanding.
