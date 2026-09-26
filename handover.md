# Session handover — 26 September 2026

Written for: the next Claude Code session continuing this work. Read this
first, then `AGENTS.md` (engineering rules and safety invariants).

**Several sessions work this repo, sometimes at the same time** (a
coordinator workspace without a Qt toolchain did KAN-42–54; another session
merged PR #56). Before starting, run `git fetch origin && git log --oneline
origin/main -20` and check Jira for tickets you assume are still open. Trust
that check over this file.

## Project

VBOOverlay / "Flapped Ear Telemetry": a Qt 6 / C++20 / QML macOS desktop app
combining a video overlay editor with track-day telemetry analysis (VBO/RCZ,
GoPro). Repo `arekkozuch/VBOOverlay`. Jira project KAN, cloud ID
`315ac5b8-6fd1-4518-8b5f-4433bcc33447`.

## Where M3/M4 stand (end of this session)

Merged to `main` today, all with hosted macOS Debug + Release green:

| Ticket | What | PR |
| --- | --- | --- |
| KAN-56 | Sector theoretical best across a compatible population, donor lap per sector | #57 |
| KAN-57 | Theoretical-best view, donor → Corner Analyzer | #58 |
| KAN-58 | M3 acceptance record `docs/kan58-m3-acceptance.md` (synthetic) | #59 |
| KAN-59 | Non-overlapping time-loss windows (`TimeLoss.h`) | #60 |
| KAN-60 | Ranked time losses (Day results → Time losses…) | #61 |
| KAN-61 | Loss → Corner Analyzer navigation, lap A/B "here" with video | #62 |
| KAN-116 | Connected corners proposed as one corner chain (proposal algorithm v2) | #63 |
| KAN-117 | Corner Analyzer usable on real recordings (side column, speeds, fixes) | #64 |
| KAN-118 | Accelerator pedal, not throttle plate, is the `throttle` alias | #65 |
| KAN-119 | Imported runs named "Session N" in recording order | #66 |
| KAN-120 | "Where your best lap can improve" map; gate-crossing segments timed | #67 |

Merged 26 September 2026:

| Ticket | What | PR |
| --- | --- | --- |
| KAN-62 | Lap and sector timing consistency (median, IQR, minimum 3) | #69 |
| KAN-63 | Braking/apex/exit/pickup and racing-line variability; braking approach stops at the previous corner (`braking-metrics-v2`) | #70 |
| KAN-64 | Progression → **By section** (typical time and spread per session) | #71 |
| KAN-65 | Timed G-G sample pairs (`GgPairs.h`) | #72 |
| KAN-66 | A/B G-G scatter with peaks in the comparison view | #74 |
| KAN-121 plan | `docs/product-split-plan.md` | #73 |

Merged later on 26 September 2026, which completes **M4**:

| Ticket | What | PR |
| --- | --- | --- |
| KAN-123 | Split phase 1: `flappedear_telemetry_core` (Qt Core + zlib) vs `flappedear_overlay_core` | #75 |
| KAN-67 | Recorded temperature summaries (`ChannelSummary.h`) | #76 |
| KAN-69 | Heart-rate summaries per run, section and A/B interval | #77 |
| KAN-68 | Progression → **Car & driver**: temperature trends per session, recorded cooling | #79 |
| KAN-70 | Heart rate in Car & driver and in the Corner Analyzer (A/B, across start/finish) | #80 |
| KAN-71 | Computed day-report model with provenance (`DayReport.h`) | #81 |
| KAN-72 | Day results → **Day report…** with evidence navigation | #82 |
| KAN-73 | "Where to look next": observations apart from hypotheses (`FocusAreas.h`) | #83 |
| KAN-74 | M4 acceptance `docs/kan74-m4-acceptance.md`, plus a fix: Save As no longer discards computed results | #84 |

In flight (check `gh pr list`): #85, KAN-124 step 1. The analysis side reaches
video only through `VideoLink`, and its guards use `documentBusy()`.

KAN-116–120 came from the **owner testing the real Jastrząb day**
(`jastrzab/`, git-ignored). That testing exposed problems synthetic fixtures
never showed, so develop against real data. See "Real data" below.

**Owner track day, 27 September 2026.** Things to ask about afterwards:
- the Day report's "Where to look next", and whether the three areas match
  the driver's own view;
- whether the Day report should open by itself after an import (KAN-72 left
  this to the owner);
- the real-track sections of `docs/kan58-m3-acceptance.md` and
  `docs/kan74-m4-acceptance.md`, which are still to be filled in.

Next: continue KAN-124 (split phase 2) in behaviour-preserving steps.
- Extract `AnalysisController`, which owns outing laps, comparison, segment
  review, theoretical best, report and channel summaries and holds a
  nullable `VideoLink *`.
- Extract `DocumentController`.
- Extract `OverlayController`.
- Split `TelemetryTests.cpp` to match.

Before and after each step, run the private real-day check. Its report
key values must stay identical.

## The product split (owner direction, 26 September 2026)

The owner wants **two products in one repository**:

- **Flapped Ear Telemetry:** a quick iPhone, iPad and Android app to view
  the day's telemetry at the track. Nobody carries a laptop to the track.
- **Flapped Ear Overlays:** the desktop video-overlay editor. Planned
  additions are tyre pressure and temperature, and a helmet camera without
  GPS, which needs manual sync of multiple video sources.

Read `docs/product-split-plan.md` (epic KAN-121, tickets KAN-122–132) before
any structural work. Phase 0 (KAN-122) lists owner decisions that are still
open: the Qt store licence, updating the direction documents
(`product-vision.md` and `AGENTS.md` still say one macOS app), bundle
identities, and the tyre data source. Phases 1–3 need none of them except
identities (phase 3).

Split rules already in force after KAN-123:
- `src/telemetry` and `src/project` form `flappedear_telemetry_core`, which
  links **Qt Core and zlib only**.
- `flappedear_telemetry_core_boundary` fails the build if they include
  overlay, app or Gui headers.
- New analysis code goes there, and its pure tests link only that library.
- Keep analysis features independent of video.

## Owner direction and preferences (this session)

- **Merging:** the owner authorised merging for this session ("you can
  merge — keep working until I say stop"). Without that, never merge; post
  green CI and wait. See memory `feedback-merge-and-ship-loop`.
- **The owner reads the app as a driver.** Tables of numbers are not enough.
  Lead with the best lap and where time is, on the track map. Say "Session 3 ·
  LAP 2", not filenames. Times of a minute or more are `m:ss.mmm`
  (`AppController::formatElapsedTime`).
- **Two apps:** decided. See "The product split" above (epic KAN-121).
- One focused PR per ticket; the owner creates or asks for Jira tickets for
  feedback-driven work (KAN-116–120 were created this way).

## Real data

```bash
FLAPPEDEAR_REAL_DAY="$PWD/jastrzab" FLAPPEDEAR_CORNER_REVIEW_DIR=/some/scratch/dir \
  ./build-native/native/tests/flappedear_native_tests analyzesPrivateTrackDayCorners
```

It imports the six recordings, approves every proposal on the best lap's
run (without a manual split, as a driver would), and prints:
- proposals, theoretical best, ranked losses and per-segment metrics;
- temperatures with cooling, and heart rate;
- the day report with its focus areas.

It also saves and reopens the day and requires an identical report. With
the review directory set, it saves screenshots of:
- the Analysis window and the Corner Analyzer;
- G-G and the theoretical best;
- the By section and Car & driver tabs;
- the day report.

**Look at the screenshots** before claiming UI work is done. Last result:
- 1:49.898 best against 1:47.905 theoretical, 1.993 s available;
- focus areas: Corners 9–16 (+0.619 s against Session 6 · LAP 3), Corners
  2–3 lost in 5 of 5 session bests, and the braking point for Corners 5–6
  spread over 20.6 m;
- oil up to 128;
- heart rate 119–136 bpm per session.

Facts learned from the owner's car (RaceChrono Pro VBO/RCZ with OBD):
`accelerator_pos-obd` is the pedal (0–100 %); `throttle_pos-obd` is the
plate (13.3 % idle, 80.4 % fully open, fully open from about 70 % pedal,
rev-match blips on downshifts with the pedal at 0). Speed is `velocity`
(km/h), brake is `brake_pos-obd`.

## Known gaps and traps

- **VBO channel units are not recorded.** RaceChrono declares them in
  `[header]`, but `channel.unit` is part of the recording fingerprint
  (`ProjectSourceReference.cpp`). Adding units would make every saved project
  demand a relink, so it needs a fingerprint migration first. Speeds therefore
  show without a unit.
- **Canonical segmentation:** segments are approved per run. The theoretical
  best, the loss ranking and the Corner Analyzer, when opened from them, use
  the lowest-run-ID eligible run with approved segments. The axis is built
  from that run's fastest lap.
- Corner/braking/exit metrics for a gate-crossing segment stay unavailable;
  only its sector time is computed.
- **Stacked PRs:** `gh pr merge --delete-branch` on a base branch
  **auto-closes** PRs stacked on it (GitHub did not retarget them). Point
  every PR at `main` before merging, and delete branches only at the end.
- QML: under `pragma ComponentBehavior: Bound`, qualify child reads through
  an explicit `id`. `Dialog.result` is a FINAL property, so don't name a
  property `result`. `slots` is a Qt macro in C++ tests. ListView delegates
  are not reachable with `findChild`; use `itemAtIndex`. Repeater delegates
  inside Flickables and Popups are not either; search the visual tree
  (`childItems()` from `window->contentItem()`). `focus` is also FINAL (use
  `focusResult`), and so is `Item.data`. An `ItemDelegate`'s `text` is not
  its content; read the property you set. Register every new QML file in
  `native/CMakeLists.txt`'s `QML_FILES`, **and every new `AppController*.cpp`
  in `native/tests/CMakeLists.txt`'s source list**, or startup smoke or the
  test link fails.
- Hosted runners lay out later than this Mac. Measure QML geometry with
  `QTRY_VERIFY`, not `QVERIFY` (a KAN-68 test flaked on CI this way).
- Save As changes the project path and re-derives the laps. Results
  invalidated on `outingLapsChanged` must wait until `!outingLapsLoading()`
  and then compare their input key (KAN-74 fix).

## Working conventions (still in force)

- Jira: "W toku" (transition `21`) when starting; comment with branch, PR,
  what landed, evidence and gaps; "Gotowe" (`41`) after merge with the final
  SHA and green main CI. All Jira content in English.
- Build/test gate every time: `cmake --build build-native --parallel` and
  `ctest --test-dir build-native --output-on-failure` (27 suites). Reconfigure
  with `cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt` after adding source files.
- Cloud CI: macOS Debug + Release on every PR push and on push to `main`
  (the main concurrency group cancels superseded runs, so verify the latest
  main SHA). macOS only; no Windows work.
- Documentation ships in the same PR (`docs/testing.md`,
  `docs/telemetry-semantics.md`, the implementation column of
  `docs/product-delivery.md`). `docs/product-vision.md` and delivery forecasts
  are owner-authored: flag staleness rather than rewriting them.
- Never kill an app process you didn't start; the owner may be using it.

## Credentials/access

- Jira via the `plugin:atlassian:atlassian` MCP connector; expect to
  re-authorize with `/mcp` in a new session.
- GitHub: `gh` is authenticated for `arekkozuch/VBOOverlay`.
