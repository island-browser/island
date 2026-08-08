# Release process

Island currently produces unsigned internal candidates only. Public stable release remains blocked
until signing and notarization verification are implemented.

## Build and package locally

```bash
./scripts/setup_deps.sh
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
python3 scripts/package.py --target macosarm64 --build-dir build --version 0.1.0 --output-dir dist
```

Package layout:

- macOS/Windows: `island_browser-<version>-<target>.zip`.
- Linux: `island_browser-<version>-<target>.tar.gz`.
- `SHA256SUMS.txt` contains the package digest and file name.
- Each package includes `build-metadata.json` and `THIRD_PARTY_NOTICES.txt`.
- Current metadata is `signed=false`, `notarized=false`, and `publicReleaseEligible=false`.

Only macOS arm64 has local Phase 1 build/run/test/package evidence: dependency verify, clean build,
18/18 CTest, deterministic data-only smoke runtime, popup blocking, history back/forward, native
close/menu Quit cleanup, 11 package tests, and Phase 0 package compatibility. Do not promote macOS
x64, Windows, or Linux packages without native GitHub Actions evidence for those targets.

## GitHub workflows

- **Native build** runs on pull requests, pushes to `main`, and manual dispatch for native build
  paths. It verifies the six-target interface set, restores dependency cache entries, runs setup and
  verification, configures CMake, builds, and runs CTest for each matrix target. Windows build jobs
  cover both sandbox-off and sandbox-on configurations.
- **Package unsigned candidates** runs on protected `main` pushes for package-relevant paths and on
  manual dispatch. It chooses a SemVer-like candidate version, builds/tests each target, packages the
  unsigned candidate, verifies package checksum and metadata, and uploads short-retention artifacts.
- **Dependency check** runs weekly and manually. It reports current/changed dependency artifacts per
  target and uploads reports; it does not edit the lock file, open/merge PRs, or release anything.
- **Deploy site to GitHub Pages** deploys the static `site/` directory on `main` pushes that change
  `site/**`. Browser visual QA for the site is still pending because Chrome was unavailable.
- **Release gate** runs for `v*` tags but intentionally fails after recognizing the unsigned package
  contract. It requires protected tags and blocks stable publication until signed/notarized release
  verification exists.

No workflow currently auto-merges dependency updates or creates a public stable release.
