# Automated ROM builds and downloads

`.github/workflows/pages.yml` builds Runner on pushes to `main` and manual
workflow dispatches. Build checks can run on another branch, but publication
and Pages deployment only run for `main`.

The build installs the PSn00bSDK 0.24 Linux release, which includes GCC 12.3.0.
The archive URL and SHA-256 are pinned in `scripts/ci/runner-toolchain.json`.
The installer verifies that checksum, restores executable permissions, and
caches the installed SDK. The Ubuntu 24.04 job installs FFmpeg, CMake, Ninja
and the host compiler, then performs a fresh normal build. This regenerates
the sprites, font, sound bank, track and world bakes; no local build directory
is required. It runs the existing bake and music-control checks before
packaging the disc.

## Published files

Every successful main build publishes these assets:

- `runner.zip`: matching `runner.bin` and `runner.cue`. Extract both and open
  the CUE in DuckStation, so the CD audio tracks are recognized.
- `runner.zip.sha256`: checksum of the ZIP.
- `runner-build.json`: full source commit, Action run URL, disc/ZIP sizes,
  track count, checksum and pinned toolchain identity.

The package step checks the disc's sector alignment, ISO9660 descriptor,
CUE filename, track order and track offsets, and verifies the ZIP's CRCs.
The publication step verifies the downloaded artifacts again and checks the
uploaded asset names/sizes and GitHub's ZIP digest when available.

The permanent latest-download address is:

https://github.com/crosshj/parascene-games/releases/latest/download/runner.zip

This address is used by `web/manifest.json` and both download links in the
player. The page navigates directly to release assets; it does not require a
cross-origin HEAD request that GitHub's CORS rules can block. The old site
address `/build/runner.zip` leads browser visitors to a generated redirect
page at `/build/runner.zip/`, which sends them to the same latest download.
This compatibility page is HTML, not a ZIP for command-line download tools.

## Publication and retention

The workflow is serialized. Each release has a unique tag:
`runner-build-<run-number>-<attempt>-<commit-prefix>`. The publisher checks
that the build still matches main HEAD, creates a draft, uploads all three
assets, and only then publishes it as latest. Failed builds/uploads do not
promote an incomplete release or prune previous downloads.

After successful publication, it retains the current and previous successful
automated Runner releases. Older managed releases, assets and their build
tags are removed. Abandoned managed drafts are also removed after a later
successful publication. It identifies its releases using both the tag format
and an explicit marker in their notes; unrelated/manual releases are retained.
Pruning failure fails the job and can be corrected on the next successful run.

Publication uses the job's `GITHUB_TOKEN` with `contents: write`; no personal
token is needed. Assets are uploaded while the release is a draft, so the
process also supports repositories with immutable releases enabled.

Pages deploys only after publication, and uses the web ROM artifact from the
same build. It no longer packages the large native ROM into the site.
Short-lived Action artifacts transfer files between jobs and expire after
three days; release assets supply the lasting player downloads.

## Local development

`build/runner.bin` is ignored and is no longer tracked in Git. A local CMake
build still writes it normally; the small web/no-music discs keep their
existing behavior. `web/server.js` still offers `/build/runner.zip` as a ZIP
of the local BIN/CUE when testing an unpublished build. The player itself
links to the latest published release even when served locally.

This change does not rewrite or remove old binaries from existing Git history.

Run publication checks without accessing or changing GitHub:

```sh
python3 -m unittest discover -s scripts/ci -p 'test_*.py'
node --test web/test-downloads.cjs
```

The package script can also be run locally against a completed build:

```sh
python3 scripts/ci/package_runner.py --commit "$(git rev-parse HEAD)"
```

The first release appears after these changes are committed and pushed to
main, or the updated workflow is manually dispatched on main. A cloud run is
required to confirm the Linux-hosted build; local tests do not publish a ROM.
