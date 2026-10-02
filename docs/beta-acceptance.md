# macOS product candidate acceptance

> Current owner direction (13 September 2026): macOS Cloud CI is resumed;
> Windows execution remains paused. Candidates require exact-revision macOS
> Debug/Release CI and package/startup evidence, plus applicable local real-media
> and interactive acceptance. See [task delivery](development-workflow.md).


Updated 12 September 2026. Internal candidates are not approved for distribution
until the exact delivered archive passes acceptance. Version remains 0.2.0.
The old single-session-only product scope is superseded by
[the full product contract](product-vision.md) and [delivery ledger](product-delivery.md).

## Scope and evidence

The current candidate is one Mac application for whole-day event analysis and
video overlay editing/export. By owner decision (2 October 2026) the product is
being split into a desktop overlay editor (macOS and Windows) and Flapped Ear
Telemetry (macOS, Windows, iOS and Android); see [product split plan](product-split-plan.md). An event contains multiple runs with optional
run-local video/sync. A run's video can be a GoPro chapter group played as one
timeline (KAN-105); exporting a chaptered video is refused until KAN-106. First core and full-vision acceptance are distinct.

The delivery ledger's end-to-end checklist is mandatory for core completion.
The existing installation/export/recovery procedures below remain useful gates;
they do not by themselves prove A/B, corner analysis or reports are implemented.
Earlier Windows CI results are historical; current execution and product focus are Mac.

Advertise only OS/hardware combinations actually recorded for the candidate.
Supported SDR/color and RCZ restrictions remain in their implementation contracts.
HDR/Log and non-zero rotation/non-square SAR exports currently fail preflight;
no broader support or public release is claimed by this document.

## Candidate builds

Native CI runs Debug and Release tests on macOS; Windows execution remains paused. Only successful Release
jobs deploy Qt with CMake, run the installed application from a separate working
directory using the packaged Cocoa/Windows platform plugin with the build Qt SDK hidden, then upload `candidate-<runner>-<commit>`.
The deployment uses [Qt's script API](https://doc.qt.io/qt-6.8/qt-generate-deploy-script.html), QML import scanning and runtime dependency deployment. The explicit `qt6_generate_deploy_script` function retains install-time variables and plugin lists. Executable paths use the configured install directory and explicit quoting because Qt 6.8's convenience generator splits names containing spaces.
The archive includes `candidate-manifest.json` with the checkout commit, architecture,
build type and file hashes; a sidecar SHA-256 identifies the archive. On a PR run the
checkout commit can be GitHub's test merge commit. Record the manifest value, not just
the branch name. Artifacts expire after 14 days; retain an accepted candidate separately.

These are internal test packages, not published releases. macOS uses an ad-hoc signature;
Developer ID signing/notarization and Windows publisher signing remain pending. Qt
deployment can include multimedia codec libraries supplied by the Qt SDK; it does not
install the external `ffmpeg`/`ffprobe` command-line tools. Exact redistribution notices,
license/source obligations, interactive installer acceptance and signing must be completed before distribution
is approved. See [third-party inventory](../THIRD_PARTY_NOTICES.md).

To reproduce the candidate build with the same platform/compiler and Qt 6.8.3 SDK:

```sh
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DFLAPPEDEAR_DEPLOY_QT=ON -DCMAKE_PREFIX_PATH=<qt-sdk>
cmake --build build-native --parallel
ctest --test-dir build-native --build-config Release --output-on-failure
cmake --install build-native --prefix <absolute-staging-directory> --config Release
```

The build process records dependency versions. FFmpeg is currently resolved through
the platform package manager, so this is a reproducible procedure, not a promise of
byte-identical rebuilds. Run `scripts/package_candidate.py` only in the isolated build
environment: its `--qt-root` directory is temporarily renamed during the startup check.

## Installation and prerequisites for acceptance

1. Download a candidate from a successful workflow and verify its SHA-256 sidecar.
2. Windows (execution paused since 13 September 2026): run the `*-setup.exe` candidate installer; see [Windows installer](windows-installer.md).
   Alternatively extract the portable archive and keep its complete tree, including
   `stage/bin/Flapped Ear Overlays.exe`. macOS: extract `stage/Flapped Ear Overlays.app`.
3. Install compatible external FFmpeg and ffprobe. Both must resolve on PATH; macOS
   also searches `/opt/homebrew/bin` and `/usr/local/bin`. Record `ffmpeg -version` and
   `ffprobe -version`. Export checks a working HEVC encoder and the actual composition
   filters, including explicit `setparams` alpha mode. FFmpeg 6.1.1 fails that filter
   check. A successful `ffmpeg -version` alone does not establish compatibility.
4. Launch from Finder/Explorer, then run the tests below on a machine without the
   development Qt SDK. Record any Gatekeeper/SmartScreen prompt exactly; do not disable
   operating-system protection as an installation procedure.

## Acceptance record

Copy this table into the candidate's acceptance issue and attach evidence. Blank or
pending entries do not pass. A scope reduction must be explicit; do not silently skip
an advertised platform or capability.

| Gate | Required evidence | Current status |
|---|---|---|
| Identity | Archive SHA-256, manifest commit, OS build, CPU/GPU, driver, FFmpeg/ffprobe versions | Pending exact candidate |
| Automated tests | Exact-revision macOS Debug/Release CI, RCZ suite, startup, deployed startup; applicable local checks | Required on candidate |
| Clean installation | Finder/Explorer launch without Qt SDK; media playback; missing/incompatible FFmpeg message | Pending physical machines |
| Real media | Supplied RCZ/VBO plus matching real video through the full workflow below | Telemetry pair passed locally; matching video unavailable in this workspace |
| Hardware export | Actual selected HEVC encoder, output metadata, decoded frames and audio alignment | Pending candidate run |
| Persistence/recovery | Save, reopen, move/relink, mismatch confirmation, unsaved recovery/discard | Synthetic coverage exists; candidate UI check pending |
| Cancellation/output safety | Cancel both stages; existing output bytes preserved on failure/cancel; overwrite requires consent | Synthetic coverage exists; candidate UI check pending |
| Distribution | Exact dependency notices/source access, package/install review, signing decision, retained candidate | Pending |
| Approval | All required evidence linked and beta scope accepted by Arek | Pending |

## Real-media walkthrough

1. Import the video and VBO; repeat using the matching RCZ. Check duration, channel
   availability, map orientation, lap count and lap times. The supplied pair derives
   five complete laps; its parser comparison is documented in `rcz-format.md`.
2. Run auto-sync when the GoPro has usable GPS. Review weak candidates. While it runs,
   edit offset and scale: those edits must remain after completion. For video without
   GPS, check ordinary import and manual synchronization.
3. Compare a visible driving event with telemetry near the beginning, middle and end.
   Record the chosen offset/scale and observed timing error; assess drift explicitly.
4. Edit widgets, open/close Analysis, navigate laps and check all sidebar controls at
   1180×720. Verify missing channels stay missing and ordinary playback remains usable.
5. Save, close and reopen. Move each source and relink it. Try a different same-name
   source and require explicit mismatch confirmation. Exercise recovery and discard.
6. Export a short range, a complete lap with handles, then the full recording. Verify
   first/last frame, CFR count, HUD alignment, 8/10-bit color, sound and delayed/short
   audio where present. Compare preview with decoded output at matching timestamps.
7. Cancel during rendering and composition. Repeat against an existing destination;
   check its SHA-256 before/after cancellation or failure. Successful overwrite must
   require confirmation. A `SuccessWithWarning` result needs review, not an automatic pass.

## Reporting a bug

Include candidate archive hash/commit, OS/GPU, FFmpeg version, exact steps, expected and
actual result, and whether VBO or RCZ was used. Export support logs are retained under
the application-data `exports` directory; use the path shown in diagnostics. Include
the terminal error and relevant log, with personal paths redacted. Share private media
only by explicit agreement. A small synthetic reproducer is preferable when available.

Open hardening items are tracked in the [delivery ledger](product-delivery.md#open-technical-items) and Jira epic KAN-144.
No data-loss, incorrect timing or unsafe-output defect is acceptable merely because
the build has a beta label.
