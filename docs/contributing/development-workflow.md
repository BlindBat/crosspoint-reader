# Development Workflow

This page defines the expected local workflow before opening a pull request.

## 1) Fork and create a focused branch

- Fork the repository to your own GitHub account
- Clone your fork locally and add the upstream repository if needed
- Enable repo hooks once per clone: `./bin/install-hooks` *(fork-only)*

- Branch from the integration branch (see [Branch model](#branch-model))
- Keep each PR focused on one fix or feature area

## 2) Implement with scope in mind

- Confirm your idea is in project scope: [SCOPE.md](../../SCOPE.md)
- Prefer incremental changes over broad refactors

## 3) Run local checks

```sh
./bin/clang-format-fix
./bin/run-tests
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

CI enforces formatting, static analysis, the host test suites, and a build of every board
environment. The second, AddressSanitizer leg of the test job is *(fork-only)*.
Use clang-format 21+ locally to match CI.
If `clang-format` is missing or too old locally, see [Getting Started](./getting-started.md).

For the full test-program options — `--asan`, `--filter`, `--quick`, `--full` — and for
`pio run -t unit-tests`, see [Testing and Debugging](./testing-debugging.md).

## Branch model

This fork uses a single integration branch: `master`. It carries fork-only work (the host test
suites, QA tooling, spec-kit scaffolding, fork features and fixes) on top of upstream.

- Do not commit directly on `master`. Work lands only by fast-forwarding or merging a short-lived
  `feature/…` or `fix/…` branch that passed the gates below, and that branch is deleted afterwards.
- Upstream syncs merge `origin/develop` (or an upstream `master` release) into `master` through a
  dedicated sync branch.
- Fixes are one-defect-one-commit with semantic messages, so each is individually cherry-pickable.
- An **upstream** PR branch is cut from `origin/develop` and targets `develop` — upstream's
  integration branch, which is *not* the remote's symbolic default (that is `master`). Fork-only
  tooling must not be bundled into an upstream PR: `bin/run-tests`, `bin/install-hooks`,
  `.githooks/pre-push`, `test/corpus/`, `test/support/`, and the `CROSSPOINT_SANITIZE` /
  `CROSSPOINT_SUITE_DIRS` CMake options. `bin/clang-format-fix`, `.githooks/pre-commit`,
  `test/CMakeLists.txt` and `pio run -t unit-tests` exist upstream and are fine to touch.

### Merge gates

Every merge into `master` — feature, fix, or upstream sync — must pass, cheapest first:

1. `./bin/clang-format-fix -c` — formatting check (no rewrite)
2. `./bin/run-tests` — host suites green
3. `./bin/run-tests --asan` — host suites green under ASan + UBSan
4. `pio run -e default` — device firmware builds
5. `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
6. Changes to rendering, input, or activities: verify in the simulator. Claims about e-ink timing,
   sleep, or memory pressure: verify on hardware with serial output.

`./bin/install-hooks` points `core.hooksPath` at `.githooks/` and enables both hooks. `pre-commit`
runs `./bin/clang-format-fix` and re-stages the files that were already staged. `pre-push` runs
`./bin/clang-format-fix -c` when the outgoing commits touch C/C++ sources, then
`./bin/run-tests --quick`, which skips the two slowest suites. The hooks narrow the loop; they do
not replace gates 2–3. Bypass once with `git commit --no-verify` / `git push --no-verify`, and
uninstall with `git config --unset core.hooksPath`.

## 4) Open the PR

- Target the integration branch described above
- Use a semantic title (example: `fix: avoid crash when opening malformed epub`)
- Fill out `.github/PULL_REQUEST_TEMPLATE.md`
- Describe the problem, approach, and any tradeoffs
- Include reproduction and verification steps for bug fixes
- Keep it small — aim under 200 lines of non-test diff

## 5) Review etiquette

- Be explicit and concise in responses
- Keep discussions technical and respectful
- Assume good intent and focus on code-level feedback
- Write maintainer-facing replies yourself

For community expectations, see [GOVERNANCE.md](../../GOVERNANCE.md).
