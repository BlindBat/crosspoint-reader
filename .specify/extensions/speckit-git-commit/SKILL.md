---
name: "speckit-git-commit"
description: "Autocommit spec-kit workflow artifacts per the project constitution (semantic messages, allow-list staging, never on master/develop, no AI attribution)."
argument-hint: "<phase> — specify | clarify | plan | tasks | converge | checklist | constitution | implement"
compatibility: "Requires .specify/scripts/bash/speckit-commit.sh"
metadata:
  author: "BlindBat/crosspoint-reader fork"
  source: ".specify/extensions/speckit-git-commit/SKILL.md"
user-invocable: true
disable-model-invocation: false
---

## User Input

```text
$ARGUMENTS
```

Determine the workflow phase from the arguments (one of: `specify`, `clarify`,
`plan`, `tasks`, `converge`, `checklist`, `constitution`, `implement`). If the invoking hook
prompt names the phase, use that. If no phase can be determined, ask.

## What this command does

Commits the artifacts the just-finished spec-kit phase produced, under the
fork's commit rules (Constitution Principle VII, AGENTS.md Git rules). All git
mechanics live in a deterministic script — do not hand-roll `git add`/`commit`.

## Execution

### Artifact phases (`specify`, `clarify`, `plan`, `tasks`, `converge`, `checklist`, `constitution`)

Run from the repo root:

```bash
.specify/scripts/bash/speckit-commit.sh <phase>
```

That's it. The script resolves the feature's `specs/` directory the same way
the rest of the toolchain does (`SPECIFY_FEATURE_DIRECTORY` env var →
`.specify/feature.json`), stages only that phase's
allow-listed artifacts, uses a semantic `docs(...)` message, and is a safe
no-op when the phase produced no changes. Report the script's output (one
line) to the user.

RECOVERY — if the script refuses because you are on `master`/`develop`
(Principle VII): create the feature branch named after the spec directory
(read `feature_directory` from `.specify/feature.json`, use its basename:
`git checkout -b <basename>`), then re-run the same command. Never commit
directly on `master` or `develop`.

You MAY pass a better subject with `-m "docs(spec): <subject>"` when the
default (derived from the branch name) reads poorly — keep the same
`docs(<phase>)` prefix and NEVER include AI attribution or AI co-authors (the
script rejects them). A human `Co-Authored-By` trailer is fine when crediting
the author of an adapted PR.

### Implement phase (`implement`)

Code commits need human-grade granularity: one logical change per commit,
semantic type chosen from what the change IS (`feat`/`fix`/`test`/`refactor`/
`perf`/`chore`/`style`/`build`/`ci`). At each completed task-phase checkpoint in `tasks.md`:

1. Collect the exact file paths this checkpoint touched (never glob, never
   stage the whole tree).
2. Run:

```bash
.specify/scripts/bash/speckit-commit.sh implement -m "<type>: <subject>" <file>...
```

The script clang-formats staged C/C++ via the sanctioned wrapper, validates the
semantic prefix, stages only the listed files plus `tasks.md` progress, and
refuses AI-attribution text.

3. Before reporting the feature complete, remind the user of the merge gates
   (Constitution "Development Workflow & Quality Gates"): `bin/run-tests`,
   `bin/run-tests --asan`, `pio run -e default`, `pio check` — autocommit does
   NOT replace them, and this command never pushes.

## Guardrails (enforced by the script; do not bypass)

- Refuses direct commits on `master` (integration branch) and `develop`, and on
  detached HEAD.
- Allow-list staging only; unrelated working-tree changes are surfaced, never
  swept into the commit.
- No AI/tool attribution and no AI co-authors, ever; human `Co-Authored-By`
  trailers are allowed.
- Never pushes; pushing goes through the user and the pre-push hook.
