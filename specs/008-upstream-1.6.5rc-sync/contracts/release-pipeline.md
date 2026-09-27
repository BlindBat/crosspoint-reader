# Contract: fork release pipeline on upstream's workflow

Upstream `1.6.5rc` replaced the tag-push artifact workflow with `.github/workflows/release.yml`
triggered on `release: published`. The fork adopts it with **one** edit. This file records the
contract so the next sync re-applies the edit deliberately (Constitution VII).

## Inputs

| Input | Fork value |
|---|---|
| `platformio.ini` `version` | `1.6.5rc-bb.1` (RC-based) — later `<upstream>-bb.N` on an upstream final |
| Git tag | identical to the version, no `v` prefix |
| GitHub release | published; **pre-release** flag set for RC-based versions, clear otherwise |

## Upstream's check (unchanged for upstream)

```bash
version="${RELEASE_TAG#v}"
configured_version="$(sed -n 's/^version = //p' platformio.ini | head -n1)"
if [ "${{ github.event.release.prerelease }}" = "true" ]; then
  test "$version" = "${configured_version}rc"     # ← fails for 1.6.5rc-bb.1
else
  test "$version" = "$configured_version"
fi
```

## The fork's edit (the only one)

```bash
# Fork tags carry their rc marker inside the version (1.6.5rc-bb.1), so the tag
# must equal the configured version whether or not the release is a pre-release.
test "$version" = "$configured_version"
```

Everything else stays upstream's: `prerelease` selects `*-gh_release_rc` environments (else
`*-gh_release`), assets are `crosspoint-${version}-${device}.bin` for `x3-x4 sticky x4pro x4c
papermono`, the publish job requires exactly five and uploads with `--clobber`.

## Outputs and checks

| Check | Expected |
|---|---|
| Assets on the release | exactly 5, named `crosspoint-1.6.5rc-bb.1-<device>.bin` |
| Manual uploads | 0 |
| `strings <asset> \| grep -o '1\.6\.5rc-bb\.1[^ ]*'` | present in each (RC envs stamp `1.6.5rc-bb.1-rc+<sha>`) |
| `OtaUpdater::isUpdateNewer` on the device | `sscanf("%d.%d.%d")` reads `1.6.5`; the `-rc` substring means an upstream final `1.6.5` is offered as an update — intended |

## Retired

- The fork's tag-push `release.yml` that only uploaded artifacts.
- The hand-attached `firmware.bin`, `firmware-<board>.bin` assets (memory: fork-release-process →
  update after the first release under this contract).
