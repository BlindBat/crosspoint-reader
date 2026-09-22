# Specification Quality Checklist: FB2 Body-Level Front Matter Carries Progress Weight

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-09-22
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

- **Every number is measured, not estimated.** The 2,899-book FB2 corpus was scanned on
  2026-09-22 with an expat scan mirroring `Fb2MetadataParser`'s own byte-offset arithmetic
  (section span minus child spans). It produced: 380 books (13.1%) with more than 200 bytes
  of body front matter; 165 (5.7%) worth at least 0.5 of a displayed percentage point; 55
  (1.9%) worth at least a full point; worst case 256,971 bytes of front matter against
  403,143 bytes of sections (38.9%); body-level text *after* the first section capped at
  1,779 bytes across 90 books; 10 books with no section in their reading body.
- **The measurement overturned the issue's own conclusion.** Issue #9 closes with "probably
  never worth fixing" on a sample of one reference book. Principle IV is why the corpus was
  scanned before the spec was written rather than after.
- **Structural terms in the spec (`<body>`, `<section>`, `name` attribute) are the file
  format's vocabulary, not implementation detail** — they name what is in the reader's book
  file, and no requirement can be stated without them.
- **FR-011 (pin test) is a Constitution V requirement, not a user requirement**, and is kept
  in the spec because the constitution makes mutation-verified coverage a merge gate.
