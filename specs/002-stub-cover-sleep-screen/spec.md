# Feature Specification: Stub Cover Sleep Screen

**Feature Branch**: `002-stub-cover-sleep-screen`

**Created**: 2026-09-19

**Status**: Draft

**Input**: User description: "Define the stub cover feature, retrospectively. But add one enhancement - wrap lines on the stub"

## Overview

When a reader chooses a cover-based sleep screen, the device shows the open book's cover art
while it sleeps. Many books carry no usable cover art: plain-text files with no sibling image,
FB2 files with no embedded binary, EPUBs whose cover entry is missing or unreadable. Until now
those books fell straight through to the generic sleep screen — the CrossPoint logo, or whichever
wallpaper happens to be next in the rotation — so a reader glancing at a sleeping device could
not tell which book was open, and a cover-mode setting silently did nothing for a large part of
their library.

The **stub cover** closes that gap: a framed card drawn from the book's own title and author,
shown in place of missing cover art. This specification defines that behaviour retrospectively
(it exists in the tree today) and adds one enhancement: the title and author **wrap across
multiple lines** instead of being cut off at one line with an ellipsis.

This feature amends the fall-back half of the baseline requirement FR-024 in
`specs/001-crosspoint-reader-baseline/spec.md`; every other part of cover-mode behaviour defined
there is unchanged.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Recognise the sleeping device's book without cover art (Priority: P1)

A reader keeps a library of plain-text and stripped-down EPUB files that carry no cover images.
They select a cover sleep screen because they like seeing what they are reading. They read for a
while, put the device down, and it sleeps. Instead of an anonymous logo screen, the device shows a
framed card naming the book and its author, so the reader can tell at a glance which book is
waiting for them.

**Why this priority**: This is the feature. Without it, the cover sleep screen is silently
inert for every book without cover art, which is the case this story exists to fix.

**Independent Test**: Put one EPUB with no cover image on the card, open it, select the Cover
sleep screen, and sleep the device. The sleeping screen names that book. Delivered value stands
alone: no other part of this spec is needed for it.

**Acceptance Scenarios**:

1. **Given** a recorded open book whose cover art is missing, **When** the device sleeps in Cover
   mode, **Then** the sleep screen shows a framed card with the book's title and author.
2. **Given** a recorded open book whose cover art is present and readable, **When** the device
   sleeps in Cover mode, **Then** the cover art is shown and no stub card appears.
3. **Given** a recorded open book whose cover file exists but is corrupt or not a readable image,
   **When** the device sleeps in Cover mode, **Then** the stub card is shown rather than the
   generic sleep screen.
4. **Given** no book is recorded as open, or the recorded book cannot be read at all, **When** the
   device sleeps in Cover mode, **Then** the previous fall-back behaviour is unchanged (the
   default sleep screen in Cover mode; custom wallpaper behaviour in Cover + Custom mode).
5. **Given** Cover + Custom mode and a book with no cover art, **When** the device sleeps **from
   the reader**, **Then** the stub card is shown; **when** it sleeps from anywhere else, **then**
   custom wallpaper behaviour applies and no stub card appears.

---

### User Story 2 - Read the whole title of a long-titled book (Priority: P2)

A reader's library is full of long titles — anthologies, translated works with subtitles, serial
volumes numbered in the title. On the stub card the title uses the full width of the card and
continues onto further lines when it does not fit, so the reader sees enough of it to tell two
volumes of the same series apart, rather than a title cut off after a few words.

**Why this priority**: The card is useless for disambiguation when every long title collapses to
the same truncated prefix. It is P2 because a single-line card already delivers Story 1's value
for short titles.

**Independent Test**: Open a book whose title is far wider than the screen, sleep in Cover mode,
and confirm the title continues onto further lines and that the visible text distinguishes it from
a book sharing its opening words.

**Acceptance Scenarios**:

1. **Given** a title too wide for one line, **When** the stub card is drawn, **Then** the title
   continues onto further lines, broken between words, up to the line budget.
2. **Given** a title that exceeds even the line budget, **When** the stub card is drawn, **Then**
   the last line ends with an ellipsis and no text is clipped by the frame or the screen edge.
3. **Given** an author name too wide for one line, **When** the stub card is drawn, **Then** the
   author also wraps within its own smaller line budget.
4. **Given** titles of any length, **When** the stub card is drawn, **Then** the title-and-author
   block stays visually balanced on the card and never overlaps the frame.

---

### User Story 3 - The stub card obeys the reader's sleep-screen choices (Priority: P3)

A reader who has set the sleep screen to an inverted (dark) cover presentation expects the stub
card to match the rest of their sleep screens rather than flashing a bright white page at them in
a dark room, and expects sleeping with a stub card to be as quick and as flicker-free as sleeping
with real cover art.

**Why this priority**: Consistency and battery/refresh behaviour matter, but a reader still gets
the core value with default settings.

**Independent Test**: Set the cover filter to Inverted, sleep with a coverless book, and confirm
the card is light-on-dark and that the screen updates exactly once.

**Acceptance Scenarios**:

1. **Given** the Inverted cover filter, **When** the stub card is drawn, **Then** it appears as
   light text on a dark card.
2. **Given** any cover filter, **When** the stub card is drawn, **Then** the screen performs the
   same single update used by the other static sleep screens, with no extra flash.
3. **Given** the device sleeps from a reader in a rotated orientation, **When** the stub card is
   drawn, **Then** it is laid out upright for the device's normal sleep presentation, like cover
   art is.

---

### Edge Cases

- **No title recorded**: a book that loads but exposes no title produces no card — the previous
  fall-back (default or custom sleep screen) applies, because a frame with nothing in it is worse
  than a wallpaper.
- **No author recorded** (common for plain-text files): the card shows the title alone, with the
  block re-balanced so it does not look bottom-heavy.
- **Unbreakable text**: a title that is one word longer than a line is broken within the word
  rather than allowed to run past the frame.
- **Non-Latin scripts**: titles in scripts the built-in interface font does not cover are drawn
  with whatever substitution the rest of the interface already uses; right-to-left titles keep
  their reading direction.
- **Whitespace-only or control-character title**: treated as no title.
- **Cover art appears later**: once real cover art becomes available for that book, the card stops
  appearing, with no stored state to clear.
- **Book fails to load at all** (missing file, unreadable container): no metadata is available, so
  the previous fall-back applies — the card never invents a placeholder title.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: When a cover-based sleep screen is selected and the recorded open book has no usable
  cover art, the System MUST render a stub cover card instead of falling through to the default or
  custom sleep screen.
- **FR-002**: The System MUST treat all of these as "no usable cover art": the book format exposes
  no cover, cover extraction fails, the cover file is absent, and the cover file is present but not
  a readable image.
- **FR-003**: The stub cover MUST be reachable for every book format the cover sleep screen already
  supports (EPUB, FB2, plain text and Markdown, XTC/XTCH page containers).
- **FR-004**: The stub cover MUST show the book's title, and MUST show the book's author beneath it
  when the format records one.
- **FR-005**: The System MUST NOT render a stub cover when no title is available; in that case the
  pre-existing fall-back for the selected mode MUST apply unchanged (default sleep screen in Cover
  mode, custom wallpaper behaviour in Cover + Custom mode).
- **FR-006**: The System MUST apply the stub cover under exactly the same mode conditions as real
  cover art: always in Cover mode when a book is recorded, and in Cover + Custom mode only when
  sleep was entered from a reader.
- **FR-007**: The stub cover MUST be visually distinguishable from a real cover and from the
  default sleep screen: it MUST present the text inside a double-line rectangular frame inset from
  the screen edges.
- **FR-008**: The title MUST be drawn in the larger, bolder of the two text sizes used on the card,
  and the author in the smaller one, so the two are distinguishable at a glance.
- **FR-009**: The title MUST wrap across multiple lines, broken at word boundaries, up to a bounded
  line budget; a title that exceeds the budget MUST end with an ellipsis.
- **FR-010**: The author MUST wrap the same way within its own, smaller line budget.
- **FR-011**: Wrapped text MUST stay within the inner frame's horizontal bounds, and a word longer
  than one line MUST be broken rather than overflow.
- **FR-012**: The title-and-author block MUST remain vertically balanced on the card as the number
  of lines changes, and MUST NOT overlap or cross the frame at any line count.
- **FR-013**: The stub cover MUST honour the sleep-screen cover filter setting, at minimum
  rendering light-on-dark under the Inverted filter.
- **FR-014**: The stub cover MUST complete in a single screen update, matching the other static
  sleep screens, and MUST NOT add a second flash or a visible delay to sleep entry.
- **FR-015**: The stub cover MUST be laid out for the device's normal sleep presentation regardless
  of the orientation the reader was using, consistent with how cover art is presented.
- **FR-016**: The stub cover MUST derive its text only from metadata already read while looking for
  the cover; it MUST NOT trigger an additional book parse, and MUST NOT write anything to storage.
- **FR-017**: All text on the card MUST come from the book's own metadata; no fixed user-facing
  wording is introduced by this feature (so the card needs no translation).
- **FR-018**: Metadata strings MUST be treated as untrusted input: any length, any encoding and any
  control characters MUST produce a bounded, non-crashing card.

### Key Entities

- **Book metadata**: the title and author recorded by the format reader for the book currently
  open. Read-only here; this feature adds no new stored data.
- **Sleep screen selection**: the reader's chosen sleep screen mode, cover fit/crop mode and cover
  filter. Read-only here; this feature adds no new setting.
- **Stub cover card**: the rendered result — a framed, centred presentation of title and author,
  produced on demand and never persisted.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For 100% of books that have readable metadata but no cover art, a sleeping device in
  a cover sleep mode names the book on screen, where previously it named none of them.
- **SC-002**: A reader can identify which of two books with the same first three title words is
  open, from the sleeping screen alone, without waking the device.
- **SC-003**: Titles up to 100 characters are shown in full on a standard panel; longer titles end
  in an ellipsis with no clipped or overflowing text at any length.
- **SC-004**: Sleeping with a stub cover takes the same number of screen updates (one) as sleeping
  with real cover art, with no added flash.
- **SC-005**: Sleep entry time with a stub cover is indistinguishable to the user from sleep entry
  with the generic sleep screen (no perceptible added delay).
- **SC-006**: No book, however malformed its metadata, causes the device to fail to sleep or to
  restart while drawing the card.
- **SC-007**: Readers who previously saw a wallpaper or logo for coverless books see zero change in
  behaviour for books that do have cover art, and zero change in any non-cover sleep mode.

## Assumptions

- **Deltas from what ships today**: this spec is retrospective except for two points — the
  multi-line wrapping of FR-009 to FR-012 (the requested enhancement, which replaces
  single-line truncation), and the whitespace-only-title edge case of FR-005, which today
  would draw an empty frame. Everything else describes behaviour already in the tree.
- **Line budgets**: the title wraps to at most 3 lines and the author to at most 2. These are
  chosen so the card stays legible and balanced on the smallest supported panel; the description
  asked for wrapping without naming a limit.
- **Ellipsis over shrinking**: text that exceeds its line budget is ellipsised rather than rendered
  at a smaller size, keeping the card's two-size hierarchy predictable.
- **Cover + Custom parity**: the stub cover inherits the existing "only from the reader" rule of
  Cover + Custom mode rather than introducing its own rule, so the setting's meaning does not
  change.
- **Filter semantics**: the Contrast cover filter has no visible effect on a card that is already
  pure black and white; only the Inverted filter changes its appearance. This is treated as correct
  rather than a gap.
- **No new setting**: the stub cover is not independently switchable. A reader who does not want it
  chooses a non-cover sleep screen, as they would to avoid cover art.
- **No new storage**: the card is drawn from metadata already in hand; nothing is cached, so no
  cache version changes and no card can go stale.
- **Existing font substitution applies**: scripts outside the built-in interface font rely on the
  substitution behaviour the rest of the interface already has; this feature adds none of its own.
- **Baseline dependency**: cover-mode selection, cover extraction per format, the cover filter and
  fit/crop settings, and the single-refresh sleep presentation all exist already and are specified
  in `specs/001-crosspoint-reader-baseline/spec.md` (FR-022, FR-024). This feature changes only
  what happens when that cover lookup comes back empty.
