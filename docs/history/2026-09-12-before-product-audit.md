# Flapped Ear Telemetry — current state

Updated 12 September 2026. Application version: **0.2.0**. Status: **internal candidate preparation; beta approval pending; macOS-first event analysis development**.

The September 1 handoff is preserved in [the historical checkpoint](2026-09-01-currentstate.md). Its open/closed statements describe that older baseline.

## Implemented

- Qt 6/C++20/QML editor with one video and one telemetry session, projects, recovery, templates, Analysis and shared preview/export rendering.
- Native uninterrupted version-1 RCZ import throughout import/reopen/relink/recovery/export, alongside VBO. Native clocks and recorded channels are preserved; unsupported variants fail explicitly.
- Shipping-review R1–R8 closed in PRs [#2](https://github.com/arekkozuch/VBOOverlay/pull/2) and [#4](https://github.com/arekkozuch/VBOOverlay/pull/4): checked frame arithmetic, conservative auto-sync confidence, bounded template persistence, GUI recovery ownership, complete source-load request restarts, original-PTS seek/trim, delayed-audio preservation and production FFmpeg filter preflight.
- Pending auto-sync results now require the current timing-edit revision. Manual offset/scale edits cancel work and clear old candidates; an edit followed by restoration still invalidates the pending result.
- Verified RaceChrono Pro 10.2.4 VBO timing gates convert centre/direction vectors into perpendicular finite gates. Generic VBO endpoints remain unchanged; unverified identified RaceChrono versions omit gates with a warning.
- Debug/Release CI and internal Release deployment are defined for macOS ARM64 and Windows x64 with Qt 6.8.3. Successful Release jobs attach candidate archives, hashes and build manifests after installed startup with the build SDK hidden.

- Windows Release CI builds an unsigned NSIS 3.12 installer, with per-user shortcuts/registration, installed startup, uninstall/reinstall and preservation checks. See [installer contract](../windows-installer.md).

## Evidence

The new event-import slice adds a core-only batch preparation API and a separate
Qt Test target. It does not yet change the single-recording UI or project format.
Local native compilation is unavailable in this Work environment. Native
verification for this slice is tracked against the exact head of
[PR #7](https://github.com/arekkozuch/VBOOverlay/pull/7); consult its checks and
validation record rather than the historical results below. See the
[working delivery plan](../event-analysis-plan.md).

| Evidence | Result / boundary |
|---|---|
| PR #4 CI, run 34589013875 | macOS application 203 passed / 0 failed / 6 skipped; Windows 198 / 0 / 11; RCZ 26 / 0 / 1 and startup passed on each platform |
| New local parser comparison | Qt 6.8.3, 30 passed / 0 failed, including the supplied private RCZ/VBO pair |
| Supplied pair laps | Five complete laps through both parsers; maximum duration differences from recorded metadata: RCZ 0.0076 s, VBO 0.0104 s |
| Previous beta-readiness change | [PR #5](https://github.com/arekkozuch/VBOOverlay/pull/5); historical candidate evidence, not validation of the new import slice |
| Real-video/hardware evidence | Earlier development checks remain historical. No matching GoPro video is available in this workspace for fresh candidate acceptance |

CI skips for private media and hardware remain visible. A hosted renderer or installed startup check does not establish interactive or clean-machine acceptance.

## Remaining before beta approval

Follow [beta acceptance](../beta-acceptance.md) and record results against the exact archive hash. Required remaining work includes real-video synchronization/export, physical hardware-encoder checks, clean installation, dependency notices/signing decisions, and an accepted supported-platform scope.

Additional audit hardening remains open: lap-reference GPS gaps, VBO pre-allocation/derived-time bounds, Unix descendants after leader exit, production UUID log retention, coordinate-unit ambiguity, and slow/filling export destinations. [ROADMAP.md](2026-09-12-roadmap.md) tracks these explicitly; they are not closed by the two latest fixes.

Multi-run event work has begun as a separate development slice within this same
application. It does not broaden the existing single-session beta acceptance
claim. Multi-video, HDR/Log and display-transform export support, 8K hardware
acceptance, sectors and theoretical best laps remain outside that beta scope.

## Documentation map

- [README](../../README.md): capabilities, build prerequisites and limitations.
- [Architecture](../architecture.md), [project format](../project-format.md), [telemetry semantics](../telemetry-semantics.md), [RCZ format](../rcz-format.md): implementation contracts.
- [Export pipeline](../export-pipeline.md), [color policy](../media-color-policy.md), [output safety](../export-output-safety.md): rendering, timing and destination guarantees.
- [Testing](../testing.md): automated and private evidence; [beta acceptance](../beta-acceptance.md): candidate walkthrough and approval record.
- [Third-party notices](../../THIRD_PARTY_NOTICES.md): dependency inventory and remaining distribution work.
