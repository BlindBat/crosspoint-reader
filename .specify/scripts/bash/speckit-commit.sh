#!/usr/bin/env bash
# speckit-commit.sh — deterministic autocommit for spec-kit workflow artifacts.
#
# Invoked by the speckit-git-commit skill from .specify/extensions.yml hooks.
# Encodes the fork's commit rules (Constitution Principle VII + AGENTS.md):
#   - never commits directly on master (integration branch), develop, or detached HEAD
#   - stages an explicit per-phase allow-list, never `git add -A`
#   - semantic commit messages, no Co-Authored-By / AI-attribution trailers
#   - no-op success when the phase produced nothing to commit (idempotent)
#
# Usage:
#   speckit-commit.sh <phase> [-m "subject"] [--dry-run] [file...]
#     phase: specify | clarify | plan | tasks | converge | checklist | constitution | implement
#   For implement, -m "type: subject" is REQUIRED (type in feat|fix|test|docs|
#   refactor|perf|chore|style|build|ci) and [file...] lists the implementation files to stage
#   (tasks.md is staged automatically); C/C++ changes are clang-formatted first.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

REPO_ROOT="$(get_repo_root)"
cd "$REPO_ROOT"

PHASE="${1:-}"
shift || true

die() { echo "speckit-commit: $*" >&2; exit 1; }

MSG=""
DRY_RUN=0
FILES=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -m) [[ $# -ge 2 ]] || die "-m requires a value"; MSG="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    *) FILES+=("$1"); shift ;;
  esac
done

case "$PHASE" in
  specify|clarify|plan|tasks|converge|checklist|constitution|implement) ;;
  *) die "unknown or missing phase '$PHASE' (expected specify|clarify|plan|tasks|converge|checklist|constitution|implement)" ;;
esac

# --- Branch guard: Principle VII — work lands on master only via short-lived branches.
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
[[ "$BRANCH" == "HEAD" ]] && die "detached HEAD — check out a branch first"
if [[ "$BRANCH" == "master" || "$BRANCH" == "develop" ]]; then
  die "refusing to commit directly on '$BRANCH' (Constitution Principle VII). Create a feature branch first."
fi

# --- Message hygiene: semantic prefix, no AI attribution (repo hard rule).
if [[ -n "$MSG" ]]; then
  # Always-forbidden attribution forms.
  if echo "$MSG" | grep -qiE "co-authored-by|generated (with|by)"; then
    die "commit message must not carry AI attribution or co-author trailers (repo rule)"
  fi
  # Tool names are forbidden except as file/path references (CLAUDE.md, .claude/,
  # CLAUDE.local.md are legitimate repo paths).
  STRIPPED="$(echo "$MSG" | tr 'A-Z' 'a-z' | sed -E 's/claude(\.local)?\.md//g; s/\.claude(\/[^ ]*)?//g')"
  if echo "$STRIPPED" | grep -qE "(^|[^[:alnum:]_])(claude|copilot|chatgpt|anthropic)([^[:alnum:]_]|$)|[[:space:]]ai[[:space:]-]"; then
    die "commit message must not name AI tools outside file references (repo rule)"
  fi
fi

# Resolve the feature dir exactly as the rest of the toolchain does
# (get_feature_paths: SPECIFY_FEATURE_DIRECTORY env -> .specify/feature.json).
# Spec dir and branch name are independent in spec-kit. --no-persist keeps this
# read-only: it must not rewrite a pinned .specify/feature.json.
FEATURE_DIR=""
if [[ "$PHASE" != "constitution" ]]; then
  PATHS_OUT="$(get_feature_paths --no-persist)" || die "cannot resolve the feature specs/ directory (set SPECIFY_FEATURE_DIRECTORY or run /speckit-specify to write .specify/feature.json)"
  eval "$PATHS_OUT"
  [[ -d "$FEATURE_DIR" ]] || die "resolved feature dir does not exist: $FEATURE_DIR"
fi
REL_FEATURE="${FEATURE_DIR#"$REPO_ROOT"/}"
FEATURE_LABEL="$(basename "${FEATURE_DIR:-$BRANCH}")"

# --- Per-phase staging allow-list. Paths that don't exist are skipped silently.
STAGE=()
add_if_exists() { [[ -e "$1" ]] && STAGE+=("$1"); return 0; }
DEFAULT_MSG=""

case "$PHASE" in
  specify)
    add_if_exists "$REL_FEATURE/spec.md"
    add_if_exists "$REL_FEATURE/checklists"
    DEFAULT_MSG="docs(spec): add specification for $FEATURE_LABEL"
    ;;
  clarify)
    add_if_exists "$REL_FEATURE/spec.md"
    DEFAULT_MSG="docs(spec): record clarifications for $FEATURE_LABEL"
    ;;
  plan)
    add_if_exists "$REL_FEATURE/plan.md"
    add_if_exists "$REL_FEATURE/research.md"
    add_if_exists "$REL_FEATURE/data-model.md"
    add_if_exists "$REL_FEATURE/quickstart.md"
    add_if_exists "$REL_FEATURE/contracts"
    DEFAULT_MSG="docs(plan): add implementation plan for $FEATURE_LABEL"
    ;;
  tasks)
    add_if_exists "$REL_FEATURE/tasks.md"
    DEFAULT_MSG="docs(tasks): add task breakdown for $FEATURE_LABEL"
    ;;
  converge)
    add_if_exists "$REL_FEATURE/tasks.md"
    DEFAULT_MSG="docs(tasks): append convergence tasks for $FEATURE_LABEL"
    ;;
  checklist)
    add_if_exists "$REL_FEATURE/checklists"
    DEFAULT_MSG="docs(checklist): add quality checklists for $FEATURE_LABEL"
    ;;
  constitution)
    add_if_exists ".specify/memory/constitution.md"
    add_if_exists ".specify/templates"
    DEFAULT_MSG="docs: amend project constitution"
    ;;
  implement)
    [[ -n "$MSG" ]] || die "implement phase requires -m \"type: subject\""
    echo "$MSG" | grep -qE "^(feat|fix|test|docs|refactor|perf|chore|style|build|ci)(\([a-z0-9_-]+\))?: " \
      || die "implement message must use a semantic prefix (feat|fix|test|docs|refactor|perf|chore|style|build|ci)"
    [[ ${#FILES[@]} -gt 0 ]] || die "implement phase requires the file paths to stage"
    # Format C/C++ before staging, via the sanctioned wrapper (AGENTS.md rule).
    if printf '%s\n' "${FILES[@]}" | grep -qE '\.(c|cc|cpp|h|hpp)$'; then
      ./bin/clang-format-fix -g >/dev/null 2>&1 || true
    fi
    for f in "${FILES[@]}"; do
      [[ -e "$f" ]] || die "file to stage does not exist: $f"
      STAGE+=("$f")
    done
    add_if_exists "$REL_FEATURE/tasks.md"
    ;;
esac

[[ ${#STAGE[@]} -gt 0 ]] || { echo "speckit-commit: nothing to stage for phase '$PHASE' — no-op."; exit 0; }
MSG="${MSG:-$DEFAULT_MSG}"

if [[ $DRY_RUN -eq 1 ]]; then
  echo "speckit-commit (dry-run) on branch '$BRANCH':"
  echo "  message: $MSG"
  printf '  stage: %s\n' "${STAGE[@]}"
  exit 0
fi

git add -- "${STAGE[@]}"
if git diff --cached --quiet; then
  echo "speckit-commit: no staged changes for phase '$PHASE' — no-op."
  exit 0
fi
git commit -m "$MSG"
echo "speckit-commit: committed on '$BRANCH': $MSG"

# Informational: surface unrelated dirt so it is never silently swept in later.
OTHER="$(git status --porcelain | grep -v '^??' || true)"
if [[ -n "$OTHER" ]]; then
  echo "speckit-commit: note — unstaged changes remain outside this phase's scope:"
  echo "$OTHER" | head -10
fi
