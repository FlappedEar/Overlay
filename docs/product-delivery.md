# FlappedEar Telemetry — audit and delivery ledger

Audit date: 13 September 2026; M1 acceptance and reforecast for [KAN-28].
Original KAN-12 audit: 12 September 2026 (retained in Git history).
Product scope: [product contract](product-vision.md).
This is the current delivery authority; old checkpoints and narrow beta documents
must not override it. Feature implementation is not the same as runtime acceptance.

## Status on 4 October 2026

This section supersedes the dated baseline below where they differ. The root
`currentstate.md` and `ROADMAP.md` were archived to `docs/history/` on 2 October
2026 (KAN-159); this ledger and Jira are the only status records.

| Milestone | Tasks done | State |
| --- | --- | --- |
| M0 reliable baseline ([KAN-4]) | 8 of 8 | Done |
| M1 day results ([KAN-6]) | 10 of 10 | Done |
| M2 A/B comparison ([KAN-7]) | 14 of 14 | Done; epic closed 2 October 2026 |
| M3 sectors and corners ([KAN-8]) | 20 of 20 | Done; epic closed 2 October 2026 |
| M4 losses and report ([KAN-9]) | 16 of 16 | Done; epic closed 2 October 2026 |
| M5 core acceptance ([KAN-10]) | 6 of 12 | Open: KAN-80 and KAN-81 (matching video), KAN-83 (clean machine), KAN-84 (Windows installer, paused), KAN-85 (notices), KAN-86 (closure) |
| M6 full vision ([KAN-11]) | 15 of 24 | Open: KAN-89, KAN-94–KAN-96, KAN-98, KAN-99, KAN-108–KAN-110 |
| Product split (KAN-121, superseded by KAN-165) | Phases 1–2 and KAN-132 done; KAN-121 closed; KAN-125 rename merged | Separate repositories (2 October 2026). Stage 1: FlappedEar Overlays standalone in this repository (KAN-125; decision 3 in KAN-122). Stage 2: FlappedEar Telemetry, a new Flutter app from a blank page in its own repository and Jira project (decision KAN-167; architect handover KAN-168; KAN-126–KAN-130 move to the new project), with one `.fetproject` format compatible between both apps (KAN-170; since 6 October 2026, Overlays keeps newer versions of known fields and a test shows it keeps every analysis field through an overlay edit). Stage 3, done on 5 October 2026: the owner approved removing the analysis from Overlays that day (KAN-169 done, recording the last full-analysis commit `7eae6cd`; KAN-166 and KAN-186 done). Done: plan steps 1 and 2 (PRs #153 and #156), the automatic best lap of KAN-185 (PR #158), and step 3 (PR #160), which removes the View, welcome and transport entry points to the Lap Analysis window, adds a run picker to the editor header and drops the Analysis pages from the user guide, step 4 (PR #161), which deletes the 24 analysis QML files and their 38 UI tests (the real-day grouping and best-lap check, the day-decision recovery test and the chapter review test stay), step 5 (PR #162), which removes `AnalysisController` from `AppController` (Overlays' day-decision tests now make their decisions in `TelemetryController`), step 6 (PR #163), which removes **File › Import telemetry runs…** and its dialog, so Overlays opens days saved by Telemetry (KAN-186), and step 7 (PR #164), which removes leftover includes, the unused loaded chart-channel list and stale notes, and brings the handover current. Platforms: Overlays macOS and Windows; Telemetry macOS, Windows, iOS and Android |
| Widget list (KAN-192) | Done (PR #171) | Owner decision, 5 October 2026: keep the widgets of the owner's track video plus Tyres and designed widgets; 18 types and 9 built-in templates removed; saved documents with removed types open without them |
| Tech widget style (KAN-193) | Done (PR #176) | Owner choice, 5 October 2026: design D (Chakra Petch, cut-corner plates, amber tab, slanted pedal bars) with design C's thin RPM arc; per-widget Style setting (Classic default) and the Tech HUD built-in template |
| Widget settings clean-up (KAN-139) | Done (PR #179) | The WIDGET tab offers only settings that change the selected widget in its style; four dead defaults dropped (saved documents keep them); Retro Custom icon and stacking, Retro RPM red zone and colours, tyre pressure decimals, lap-tile label and Tech radar ring label exposed; Retro RPM canvas font fixed for multi-word families |
| App icon (KAN-202) | In this change | The icon redrawn in the Telemetry design language: amber ears in video-frame corners above a lower-third data plate on a charcoal tile (owner choice of design D); SVG master `native/resources/branding/app-icon.svg`, platform files from `scripts/make_app_icons.py` |
| Chapter export (KAN-106) | Done in this change | A GoPro recording opened as chapters exports as one source across chapter joins: exact frame count and frames, audio kept on the chapter timeline, every chapter protected, changed or mismatched chapters refused ([export-pipeline.md](export-pipeline.md#chaptered-sources-kan-106)); validated on generated chapters, real chapters wait for the owner's footage (KAN-81) |
| User-guide screenshots (KAN-189) | Interim done (PR #186) | Retaken on 6 October 2026 in the current look over a neutral review backdrop (no GoPro footage available); the Auto Sync and Video chapters pictures and the final retake over the onboard video wait for the footage |
| Tyres data (KAN-203) | Done (PR #192) | Owner report, 6 October 2026: Tyres showed only dashes. The RCZ import dropped RaceChrono's CAN-bus tyre channels; it now reads them, verified sample for sample against the VBO of the owner's Silesia Ring session (27 September 2026). The inspector also offers a channel choice per corner and says when no tyre channel is found (the Jastrząb day of 29 August has none). Follow-up KAN-204 (done): RCZ laps on library tracks |
| RCZ laps on library tracks (KAN-204) | Done (PR #196) | Newer RaceChrono sessions on a library track carry only the track id in the RCZ, so no start/finish line and no laps. The line is now rebuilt from RaceChrono's own lap times; the owner's Silesia Ring session (27 September 2026) gives 7 laps within 0.02 s of RaceChrono's |
| Start-line crossings (KAN-205) | Done 7 October 2026 (#202) | Port of Telemetry FET-198: a pass counts only when it really crosses the line, starting on one side and ending on the other within the gate span widened by 5 m at each end. Same-side near misses (pit lane, a car turning back) no longer make a lap. The owner's Jastrząb day still gives 25 laps, best 1:49.898 |
| Lap plausibility (KAN-225) | In this change | Port of Telemetry FET-199: a lap under 3 s or over 1 h, under 200 m, faster than 100 m/s on average, or under 80 % of the recording's median lap path is listed but never ranked; across the day, a lap under 80 % of its circuit group's median path (from at least two recordings) is left out too. New lap issue `implausible-lap`. The owner's Jastrząb day still gives 25 timed laps, none implausible, best 1:49.898 |
| Architecture review (KAN-206) | In progress | Owner review, 6 October 2026, one Jira item per finding (KAN-207 to KAN-222). Done: KAN-207 (#199) a VBO without a recognised time column is rejected instead of loading row numbers as seconds. KAN-208 (#200) a telemetry file's whole content is hashed, not three sampled windows; videos keep the sampled check (owner decision, 6 October 2026). KAN-209 part 1 (#201) the per-channel cadence cache is thread-safe and recomputed after an edit. KAN-209 part 2 (#204), a channel's samples change only through setters that keep timestamps finite and strictly increasing, one value each. KAN-210, chart downsampling keeps every separate run or reports truncation. KAN-211, GoPro GPS without fix information is not trusted, GPS9 is preferred per packet, and duplicates keep the better fix. KAN-213, the lap direction is the one most crossings share. KAN-214, auto-sync counts a separate peak 2 to 4 s from the best as a competitor. KAN-212, a start line more than 15 m from the lap's path gives no progress axis. KAN-220 (#204), lap traces and progress projection spread dense GPS fixes evenly by time. KAN-221 (#204), a gap is also judged against the local cadence, so a slower-logged stretch is not a run of gaps. KAN-222 (#204), the heart-rate difference of 133 was RaceChrono's 0 bpm in a VBO's final row; heart rate outside 30 to 230 bpm is now no data. KAN-215 step 1, the unused batch-import calls are gone from `AppController`. KAN-215 step 2, the export run moved into `ExportController` (`appController.exporter`). KAN-215 step 3, synchronization moved into `SyncController` (`appController.sync`). KAN-215 step 4, the template picker moved into `TemplatePicker` (`appController.templatePicker`). KAN-216 step 1, the export dialog moved out of `Main.qml` into `ExportDialog.qml`. KAN-216 step 2, the export progress popup moved into `ExportProgressPopup.qml`. KAN-219, CI adds a ThreadSanitizer job and an editor-suite ASan+UBSan job, clang-tidy checks changed headers, and the fuzzers accept 1 MiB inputs. KAN-218, shared `.fetproject` round-trip vectors pin how a day is re-saved; Telemetry is to run them too. KAN-217 first step, a widget of a type this build does not know is kept unchanged instead of making the project invalid. Open: KAN-215 (rest of the AppController split), KAN-216 (Main.qml split), KAN-217 (one descriptor per widget type), and the FlappedEar/Telemetry ports |
| Product review (KAN-141) | Done 6 October 2026: 18 findings fixed, 2 kept by the owner | [Record](kan141-code-review-2026-10-06.md): 20 findings on `main` at `32ba970`. Fixed: KAN-194 (#180), KAN-195 (#181), KAN-197 (#182), KAN-196 (#183), KAN-198 (#184), KAN-199 gauges and editor panel (#185) and Theme tokens (#189), KAN-201 (#188), KAN-188 fusion gap markers (#190). Owner kept the overlay value hold and path-only legacy source matching (KAN-200), recorded in AGENTS.md |
| Stabilisation (KAN-144) | 21 of 25 | From the independent audit of 2 October 2026 and the owner's Windows validation; it gates M5. Done: KAN-145 to KAN-151, KAN-154 to KAN-157, KAN-159, KAN-161, KAN-164, KAN-172 to KAN-176, KAN-178, KAN-153. Open: KAN-152, KAN-158, KAN-160, KAN-179 (KAN-152: since 6 October progress 0 is where the lap crosses the gate, a lap's progress is unwrapped at the gate and the comparison delta runs from each lap's timed start; recording which axis approved segments were measured on remains; KAN-154: `main` protection enabled 4 October, Linux sanitizer job added 5 October; since 5 October the boundary checks follow includes from each library's sources and the macOS FFmpeg is range-tested (8.1 to 9.x), and pull requests run clang-tidy on changed files; KAN-161: the editor's GUI tests run as five domain suites and `AppController` is the `flappedear_editor` library since 5 October; KAN-157: value lookup and auto-sync stop at time gaps since 5 October; KAN-155: libFuzzer harnesses for VBO, RCZ and GPMF, 10 minutes each without a finding, and a 60-second pass per parser in CI) |

- **CI:** Native CI builds macOS arm64 Debug and Release with Qt 6.8.3 and runs 40
  CTest registrations (37 Qt Test executables, the startup smoke and two library
  boundary checks; the storage-migration suite was added by KAN-125 and the
  content-id vector suite by KAN-180). `main` at `ca66169` failed in Debug on a post-import test race
  (KAN-143, KAN-150); Release passed. Windows CI remains paused; the owner
  validated Windows locally on 2 October 2026 ([record](windows-validation-2026-10-02.md)),
  and Windows code changes for its findings are resumed (KAN-172 to KAN-176).
- **Evidence:** synthetic CI plus one private real day without video (KAN-77,
  KAN-79). The owner's real-video acceptance (M5) has not been executed.
- **Open technical items** outside Jira tasks are listed in
  [open technical items](#open-technical-items).

## Verified baseline

This reconciliation uses `main` at
[`5d71a1a5e5d31579102e93b6912838a122baa763`](https://github.com/arekkozuch/VBOOverlay/commit/5d71a1a5e5d31579102e93b6912838a122baa763).
It is a dated source/evidence snapshot. Later task PRs and exact validation
results belong in their Jira completion records.

| Integrated change | Source state | Verification |
| --- | --- | --- |
| M0 steps 001–008: GPS eligibility, log retention, parser/process/sync hardening and product naming | PRs #12–#16 and #18–#20; retained in this baseline | Exact per-task integration evidence in [KAN-5], [KAN-12]–[KAN-18]; full baseline suite reruns the regressions |
| M1 steps 009–016: identity, exclusions, rankings, metadata, progression, persistent decisions and automatic GPS grouping | PRs #23–#29 plus owner-local commit `ddb095a514c06cd7ad090257b9a74108e85d810d` | [Local private-recording evidence](kan26-local-validation.md), separately from hosted synthetic coverage |
| Step 017: complete video-free result states and bounded retry | [PR #31](https://github.com/arekkozuch/VBOOverlay/pull/31), merge `f2f1af0262e6ed20d2426aece241e10e77b33912` | [PR CI](https://github.com/arekkozuch/VBOOverlay/actions/runs/34773978691) and [main CI](https://github.com/arekkozuch/VBOOverlay/actions/runs/34774340031), macOS Debug and Release |
| Readable channel selectors | [PR #32](https://github.com/arekkozuch/VBOOverlay/pull/32), merge `5d71a1a5e5d31579102e93b6912838a122baa763` | [PR CI](https://github.com/arekkozuch/VBOOverlay/actions/runs/34775248771); [main CI](https://github.com/arekkozuch/VBOOverlay/actions/runs/34775598217); exact acceptance matrix in [KAN-28 record](kan28-m1-acceptance.md) |
| M2 steps 019–031: independent A/B, shared track-progress axis, delta/channel/map comparison, optional video linkage, missing-data context and persistence | PRs #37–#41 plus owner-local commits `080d4ea`, `143303c` | Exact acceptance matrix, new fixture coverage and measured-cycle reforecast in [KAN-42 record](kan42-m2-acceptance.md); PR CI links recorded per-task in Jira |

## Evidence levels

- **Implementation** describes code present at the baseline. Partial, raw-channel
  and visualization foundations do not satisfy an entire F00–F20 capability.
- **CI verification** means the configured synthetic coverage passed on that
  source state. Current Native CI builds macOS arm64 Debug and Release with Qt
  6.8.3: 40 CTest registrations on 4 October 2026 (the seven-registration,
  501-pass figure describes the 13 September baseline). Release also exercises
  deployment and SDK-isolated package startup. Windows execution remains paused.
- **Physical acceptance** requires an identified candidate, environment and
  executed scenario. KAN-26 includes local Mac/Qt 6.11 execution and six private
  VBO recordings; it is not a new owner-operated walkthrough of this baseline.
  Private GoPro validation and M5/M6 physical acceptance remain outstanding.

This coordinator environment has no CMake/Qt. Native execution evidence comes
from CI; the owner's local Codex follows the [build/test handoff](development-workflow.md).
Private telemetry files must not be committed.

## Actual capability audit

Every row refers to the verified baseline above. Existing foundations have the
configured CI coverage; missing analytical outcomes have neither implementation
nor execution acceptance. Jira links identify concrete implementation/acceptance
work, not evidence that a future capability already works. Ranges are inclusive.

| ID | Implementation at `5d71a1a` | Remaining outcome and Jira ownership |
| --- | --- | --- |
| F00 | Partial: transactional multi-file import, portable event projects, day metadata, automatic track identity and persistent decisions; folder and drag-and-drop import (KAN-87, KAN-88); attaching recordings to existing runs after evidence review (KAN-90) | Reusable vehicle and track profiles: [KAN-89] (needs a product decision); whole-product acceptance remains in M5/M6 |
| F01 | Partial: inspectable OUT/LAP/IN, stable references, exclusions and compatible best-run/day rankings; automatic GPS layout/direction grouping; independent A/B on a shared track-progress axis with delta, paired traces, map and optional video ([KAN-29]–[KAN-42]) | M2 delivered; physical acceptance in M5 ([KAN-86]); audit defects in track progress are tracked in KAN-152 (gate anchoring, unwrapping and the timed delta fixed 6 October; the segment axis remains) |
| F02 | Partial: versioned, validated segment data model (KAN-43); smoothed heading/curvature feature derivation on the shared axis (KAN-44); automatic straight/corner proposals with boundary uncertainty (KAN-45); geometric entry/apex/exit proposals (KAN-46); measured/inferred braking-onset candidates (KAN-47); proposal review with approve/reject/edit, map overlay and an identified approved revision (KAN-48); approved-segment editing with split/merge, map picking and session undo/redo (KAN-49); persisted review decisions and calculation-revision stamps (KAN-50) | Synthetic M3 acceptance recorded in `docs/kan58-m3-acceptance.md` ([KAN-58]); real-track feedback pending |
| F03 | Partial: non-overlapping loss windows per approved segment for an A/B pair, with corner continuation onto the following straight and signed increments kept separate from the running delta (KAN-59); day ranking of every eligible lap's largest observed losses against the group's best lap, with segment, compared laps, coverage and method (KAN-60); a loss opens its A/B pair in the Corner Analyzer on that window, lap A or B opens at the window with the run's video when available, and returning keeps the ranking (KAN-61) | Non-overlapping ranked losses and evidence navigation: [KAN-59]–[KAN-61]; acceptance: [KAN-74] |
| F04 | Partial: per-sector theoretical best across a compatible population, with the donor lap for each sector and the total withheld when any sector is uncovered (KAN-56); actual best, theoretical and difference with per-sector losses, opening the donor lap in the Corner Analyzer (KAN-57) | Synthetic acceptance recorded ([KAN-58]); real-track feedback pending; separately validated realistic potential: [KAN-94]–[KAN-96] |
| F05 | Partial: single-lap trace and independent cursor, readable channel selector; shared progress and paired traces/cursor ([KAN-31], [KAN-33], [KAN-37]–[KAN-42]); A/B map layers for speed, delta, lateral/longitudinal G, throttle, measured brake and recorded temperatures with legends, gaps kept open (KAN-97) | Interval/map UX: [KAN-114] |
| F06 | Partial: per-lap minimum-speed location kept separate from the geometric apex (KAN-46); braking-onset candidates with measured/inferred provenance (KAN-47); per-lap sector times with coverage and revision (KAN-51); separate entry/apex/minimum/exit speeds with provenance (KAN-52); braking point/time/distance and deceleration with an explicit interval (KAN-53); throttle pickup and following-straight exit effects (KAN-54); Corner Analyzer view with A/B segment metrics (KAN-55) | Synthetic M3 acceptance recorded ([KAN-58]); real-track feedback pending |
| F07 | Partial: coasting episodes with duration, distance and lap position, per-segment and lap totals, provenance from recorded pedals or inferred from G, on the lap map (KAN-92) | Real-track feedback pending |
| F08 | Partial: braking, accelerating, cornering and coasting as overlapping states with explicit measured/calculated/inferred provenance, known spans and no bridging across gaps (KAN-91) | Real-track feedback pending |
| F09 | Partial: braking-while-cornering time and distance per A/B lap over a Corner Analyzer segment, with braking and cornering provenance and A/B strips (KAN-93) | Real-track feedback pending |
| F10 | Partial: lap and per-sector timing consistency (median and interquartile range with sample count, minimum 3, ranking eligibility), day and per run (KAN-62); per-corner braking/apex/exit/pickup and racing-line variability, measured vs inferred kept apart, line spread against GPS accuracy (KAN-63); section-by-session progression with typical time, spread, conditions/setup and the laps behind each figure (KAN-64) | Eligible populations, timing/braking/exit/line variability and presentation: [KAN-62]–[KAN-64]; acceptance: [KAN-74] |
| F11 | Partial: within-day progression, notes, conditions and setup edits; scoped exclusions and persisted choices | Variability: [KAN-64]; reusable profiles and comparable visits: [KAN-89], [KAN-98], [KAN-99] |
| F12 | Partial: recorded temperatures summarized per run and per recorded section (time-weighted mean, extrema, coverage, placeholder/implausible samples excluded and counted, absent sensors not invented) (KAN-67); day trends per session on a shared scale with recording gaps left open, and continuously recorded cooling intervals, in Progression → Car & driver (KAN-68) | Covered extrema, thermal trends and recorded recovery: [KAN-67], [KAN-68]; acceptance: [KAN-74] |
| F13 | Partial: each recorded temperature against lap time and strong acceleration over eligible laps: Spearman rank correlation with a minimum of 8 laps and 80 % sensor coverage, counts, per-lap observations and scatter, and a time-of-day confound warning; no causal claims (KAN-100) | Real-track feedback pending |
| F14 | Partial visualization: G ball/radar/bar and calculated-G aliases; timed longitudinal/lateral pairs with clock alignment, units, provenance and outlier/gap handling (KAN-65); A/B G-G scatter over the selected range with lateral/braking/combined peaks from every sample and valid counts (KAN-66) | Timed G-G pairs, scatter and observed peaks: [KAN-65], [KAN-66]; acceptance: [KAN-74] |
| F15 | Partial: central single-video sync and independent video-free detail; GoPro chapter groups proposed from names and checked against probed metadata (missing, duplicate, unreadable, incompatible, timing), reviewable and reorderable (KAN-104); chapters played, saved and reopened as one continuous timeline with missing chapters as explicit gaps, exported as one source across chapter joins (KAN-105, KAN-106); side-by-side A/B video from each run's own verified footage at a shared track point, played by lap A with lap B kept at the same place (KAN-107) | Analysis/video navigation: [KAN-39], [KAN-42]; chapter export (generated chapters): [KAN-106]; private-video acceptance: [KAN-80] |
| F16 | Partial foundation: GPS/OBD/HR coexist; alternative files persist; clock alignment of an alternative recording to the primary described with declared and measured offset, drift, uncertainty and evidence, ambiguity and conflicts never approved (KAN-101); core fusion policy with per-sample source, clock, unit and rule, conflicts requiring an explicit rule, no resampling (KAN-102); fusion reviewed in Run details (alignment, resulting channels, coverage, conflicts), approved into the project bound to both recordings' content, reopened, and not applied when a recording changes (KAN-103); all synthetic validation only | Real paired-logger acceptance pending |
| F17 | Partial: heart-rate summaries per run, per recorded section and for a selected A/B interval (time-weighted mean, extrema, coverage, artifacts excluded and counted) (KAN-69); shown per session and per lap in Progression → Car & driver and as A/B for the selected Corner Analyzer segment (also across start/finish), each opening the recorded heart-rate channel (KAN-70) | Run/lap/sector summaries and comparisons: [KAN-69], [KAN-70]; acceptance: [KAN-74] |
| F18 | Partial: best-run/day results with lap click-through; a computed day-report model (best lap, progression, consistency, theoretical best, time losses, sections by session, temperatures, heart rate), each result with algorithm/revision, range, data status and evidence references, stale values dropped when analysis decisions change (KAN-71); Day report screen from Day results with a path from each result to its lap, loss comparison and Corner Analyzer, theoretical-best map or progression tab, and explicit reasons for missing results (KAN-72) | Computed report and evidence observations: [KAN-71]–[KAN-73]; M4 acceptance recorded in `docs/kan74-m4-acceptance.md` ([KAN-74]), synthetic plus private real-day evidence; physical Mac acceptance not claimed |
| F19 | Partial: up to three areas to inspect next (best lap against the fastest sector, a loss repeated across session bests, measured braking-point or lowest-speed spread), each with its metric, sample count, the pair of laps to compare, and a hypothesis kept apart from the observation, never causal and never recommending later braking (KAN-73) | Computed observation guidance: [KAN-73]; evidence package and Explain this lap: [KAN-108], [KAN-109] |
| F20 | Substantial implementation: shared preview/export scene, transactions, recovery, log retention, descendant shutdown, checked time bounds and consistent naming | Export/installed-candidate acceptance: [KAN-75]–[KAN-85]; chapter export: [KAN-106] |

Core physical acceptance closes in [KAN-86]; full F00–F20 acceptance closes in
[KAN-110]. Neither is complete at this baseline. Reuse PR #11's independent
verified-source loader, bounded row service and detail view for subsequent work.

## Correctness blockers and ownership

| ID | Finding | Required closure / current action |
| --- | --- | --- |
| C01 | PR #11 Qt 6.8 Mac QML crash | Closed by native-QPA fix `8aeb572`, integrated at `a0122ab`; PR and main Native CI passed with rendering/input coverage retained |
| C02 | Flat best-lap trace bridges missing GPS | Closed in [KAN-5] / PR #12 at `7138fbd`: timings retained, invalid references excluded before trace/ranking, reason/no-delta exposed; focused regressions and PR/main CI passed |
| C03 | Log retention ignores canonical UUIDs | Closed in [KAN-5] / PR #12 at `7138fbd`: actual IDs matched; unrelated/active files and symlinks protected; retention regressions and PR/main CI passed |
| C04 | Unix descendants outlive group leader | Implemented in [KAN-13] / PR #14: retain group ownership after leader exit, bound escalation, gate controller commit/cleanup and stale recovery on group termination; exact CI/integration evidence is recorded in KAN-13 |
| C05 | VBO bulk split/derived time budgets | [KAN-14] / PR #15 implements bounded line/field scanning (integration evidence in Jira); [KAN-15] implements derived-time/conversion checks (integration evidence in Jira); [KAN-17] adds checked bidirectional transforms and bounded auto-sync search (integration evidence in Jira) |
| C06 | Coordinate interpretation ambiguity | [KAN-16] implements explicit exporter/header evidence shared by samples and gates; unresolved units withhold GPS with a warning. Quadrant, zero-crossing and conflict regressions; exact integration evidence in Jira |
| C07 | Slow/full export destination behavior | [KAN-75] exercises a real filling volume (a disk image): preflight refusal, out of space in Stage A and Stage B, cancellation, and publication on a full volume. The target and unrelated files stay intact, cleanup touches only owned artifacts, and the user is told the disk is full. Remaining: a throttled (slow) destination, which needs tooling macOS does not offer without admin rights |
| C08 | User-visible name/package drift | [KAN-18] standardizes FlappedEar Telemetry display/About/bundle/package names with settings/recovery preservation checks; macOS integration evidence in Jira; Windows execution paused |

M1 implements compatibility and exclusion rules beyond C02. Route evidence,
direction and timing-definition identity govern grouping; the same date alone
does not establish compatibility. OUT/IN and route outliers remain inspectable.

## Dependency-ordered delivery

> Status note (2 October 2026): steps 001–064 (M0–M4) are done; see
> [Status on 2 October 2026](#status-on-2-october-2026). The step counts below
> describe the 13 September reconciliation.

The backlog contains **100 separate numbered Tasks plus seven milestone Epics**.
Steps 001–017 are complete; this reconciliation is step 018. After its closure,
82 numbered tasks (019–100) remain. Jira holds live status and acceptance criteria.
Additional analysis UX items [KAN-113]–[KAN-115] remain in the backlog outside the
100 numbered tasks; their scope is included in the deadline capacity discussion.

| Milestone epic | Steps | Task keys | Count |
| --- | --- | --- | --- |
| [M0 — reliable baseline][KAN-4] | 001–008 | [KAN-5], [KAN-12]–[KAN-18] | 8 |
| [M1 — day results][KAN-6] | 009–018 | [KAN-19]–[KAN-28] | 10 |
| [M2 — A/B comparison][KAN-7] | 019–032 | [KAN-29]–[KAN-42] | 14 |
| [M3 — sectors and corners][KAN-8] | 033–048 | [KAN-43]–[KAN-58] | 16 |
| [M4 — losses and report][KAN-9] | 049–064 | [KAN-59]–[KAN-74] | 16 |
| [M5 — core acceptance][KAN-10] | 065–076 | [KAN-75]–[KAN-86] | 12 |
| [M6 — full vision][KAN-11] | 077–100 | [KAN-87]–[KAN-110] | 24 |

The effort ranges below are **historical estimates from the original audit**, not
remaining-work estimates or delivery dates. M0's Jira scope includes additional
hardening, so its original range must not be treated as a forecast for all eight
tasks. Ranges include implementation, regressions and integration; they are not
elapsed-time guarantees and are not divided by agent count.

| Milestone | User-visible completion | Acceptance / dependencies | Original effort estimate |
| --- | --- | --- | --- |
| M0 — regain a reliable baseline | Existing outing workflow integrated; master vision/status truthful | KAN-5 and KAN-12–KAN-18: C01–C03 verified, descendant/parser/coordinate fixes, sync-bound verification and naming; C07 retained in M5 | 1–3 working days for the original narrower audit slice |
| M1 — day results | Best eligible run/day, run progression, notes/conditions and exclusions | Reuse outing service; compatible layout/direction/gate groups; missing files and GPS incomplete states; persistence/recovery | 2–4 days |
| M2 — comparison evidence | Independent A/B, distance delta, speed/available channels, two traces, common cursor | M1; deterministic shared track-progress alignment including crossings/gaps; no editor mutation | 6–10 days (original estimate); **measured ~11.4 elapsed days, see [KAN-42 reforecast](kan42-m2-acceptance.md#measured-m2-cycle-time-and-reforecast)** |
| M3 — corner analysis | Automatic sector/corner proposals with review/editing, entry/apex/exit/braking metrics, sector theoretical | M2; stable editable boundaries, metric prerequisites/provenance and downstream straight effects | 7–12 days |
| M4 — useful conclusions | Ranked losses, consistency, G-G, available thermal/HR summary and clickable report | M3; non-overlapping losses, sample counts, missing-data semantics, measured/inferred distinction | 5–9 days |
| M5 — complete core acceptance | A tested Mac app covering the core product journey and overlay export | M4 + C04–C08; full-day/private-video walkthrough, reopen/recovery, installed candidate, short/lap/full exports | 4–7 days plus external access |
| M6 — remaining original vision | Realistic potential, expanded state/coasting/trail analysis, thermal correlation, multi-event history, fusion, chapter/comparison video and explanations; remaining F00 folder/drop, reusable vehicle/track references and alternatives on existing runs; remaining F05 channel map layers | Individually validated algorithms, source/clock provenance; explicit acceptance per F00–F20; earlier core remains usable | Additional 25–45 days, low confidence |

The original core workflow estimate was **25–45 focused working days** from
the `d7e195e` audit; the full-vision estimate was **50–90 working days total**.
These are retained historical estimates, superseded for near-term planning by
the [measured M1 reforecast](kan28-m1-acceptance.md#measured-cycle-time-and-reforecast).

### Two-week owner target — 27 September 2026

> Historical (flagged 2 October 2026): the 27 September target has passed. The
> forecast below is owner-authored and is retained as recorded.

The owner set a two-week deadline on 13 September, before the next track visit.
**27 September is the planning target**, derived from that instruction, not a
separately confirmed event date. Preserve all F00–F20 scope. Reserve 26–27
September for candidate regression, the owner's Mac/private GoPro walkthrough
and track preparation; fix issues discovered earlier as each increment lands.

M0's eight recorded Jira cycles had a median of 25.4 minutes; M1 implementation
had a median of 23.3 minutes across eight measurable cycles. KAN-26 has no
recorded start and is excluded from duration statistics. These are workflow
status intervals, including CI and administration, not measured engineering hours
or a sustained daily delivery rate. Future alignment, corner and inference work
is not demonstrated by this small sample.

After 018, the full target contains 82 numbered tasks plus three UX tasks.
With 12 delivery days and two acceptance days, it requires about **7.1 completed
items per delivery day**. Capacity scenarios of 4/6/8 items per day imply
24/17/13 calendar days including that buffer; they are arithmetic sensitivity
checks, not confidence bounds or commitments. Full-scope completion in two weeks
remains low-confidence until M2 demonstrates the harder analysis work and the
private acceptance window is exercised. The target does not authorize dropping
features or counting skipped physical checks as passed.

M2 (steps 019–031, [KAN-29]–[KAN-41]) is complete and merged. Its measured
cycle time and reforecast are recorded in the
[KAN-42 acceptance record](kan42-m2-acceptance.md#measured-m2-cycle-time-and-reforecast):
M2 took roughly 11.4 elapsed days for 13 tickets, an order of magnitude
slower than the capacity scenarios below (themselves modeled from M0/M1's
much faster synthetic-only cycles), and close to M2's own original 6–10-day
human estimate. At that observed rate, the 68 numbered tasks remaining after
step 032 (033–100, M3 through M6) would take on the order of 60 elapsed
days — the 27 September planning target is not achievable for full F00–F20
scope on this evidence. Whether to start M3 (step 033, [KAN-43]), narrow
scope, or hold at the current baseline is the owner's decision with this
evidence in hand; this ledger does not make that call. The recorded UX
tickets remain backlog work, as requested. No unattended execution between
turns is implied.

The owner's near-term benefit arrives incrementally: integrated PR #11 gives day/lap inspection;
M1 gives day results; M2 gives actionable comparison; M4 gives the original
loss-to-corner-to-evidence experience. None is called full completion prematurely.

## Open technical items

Moved from the archived [roadmap of 12 September 2026](history/2026-09-12-roadmap.md)
on 2 October 2026; only items that are still open are listed. Audit defects are
tracked separately in KAN-144.

- **Export destinations:** a slow (throttled) destination remains unvalidated (C07);
  the fixed final-validation timeout is KAN-148.
- **Real media:** broader final-media and preview/export validation on real
  recordings, including the owner's matching GoPro video for the full day (KAN-80,
  KAN-81).
- **Colour and transforms:** colour-managed HDR/HLG/PQ/Log preservation (rejected
  today, never silently converted); rotation and sample-aspect-ratio display
  transforms end to end (export fails fast today).
- **Resolution:** production 8K validation on representative renderer and encoder
  hardware.
- **Video:** multiple video sources with manual sync (KAN-131); export across
  real GoPro chapters checked with the owner's footage (KAN-106 is validated on
  generated chapters).
- **Telemetry sources:** RaceChrono VBO exporter versions other than Pro 10.2.4;
  VBO channel units are not stored (a fingerprint migration is needed first).
- **Lap timing:** a manual Start/Finish override.
- **Analysis UX:** more than four channels (KAN-113), interval selection with map
  highlighting (KAN-114), readable axes, units and grids (KAN-115), chart
  annotations.
- **Map:** interactive map tiles and offline-safe map export behaviour.
- **Distribution:** Developer ID signing and notarization, distribution notices
  and source access (KAN-85, KAN-160), clean-machine acceptance (KAN-83), retained
  artifacts, installation UX and an update strategy.
- **Windows (CI and packaging paused):** broader GPU/encoder and installed-dependency coverage,
  heavy 4K GUI responsiveness, ACL-denied filesystem coverage, multi-instance
  export-log safety and Windows code signing (KAN-84).

## Whole-product acceptance record

For each milestone record commit, PR, exact CI head/test-merge SHA, executed test
counts and limitations. For M5 also record candidate archive hash and Mac/Qt/OS.

- Import all provided runs, paired exports, malformed/duplicate files and cancel.
- Save/reopen/move/relink the whole event; preserve IDs, notes, choices and sync.
- Select laps from different compatible runs without video. Show unavailable
  metrics honestly and exclude incomplete data from best/potential calculations.
- Follow a report loss into its corner, delta, map and channel evidence.
- Attach matching video; verify sync and editor/analysis independence; render
  short/lap/full supported SDR outputs; test cancel/failure with existing targets.
- Inspect minimum-size Mac UI, keyboard navigation, no-data/loading/error states
  and installed startup. Preserve explicit hardware/private fixture skips.
- Public distribution/signing is a separate authorization and acceptance record;
  existing candidate smoke tests are not a release approval.

## Coordination and handoff

The [task delivery workflow](development-workflow.md) requires one active Jira
task, an implementation PR, passing PR and integrated-main CI, and an exact-SHA
handoff for local Codex compilation. All Jira content is English. KAN-5 starts
the numbered backlog; milestone containers do not count towards its 100 tasks.

Primary agent owns this ledger, integration, PR verification and reporting. Check
current main/open PRs before work and keep implementation commits focused. Each handoff must state completed evidence,
remaining blockers, current milestone and next acceptance outcome. Do not hand
the owner a fresh list of prompts in place of executing authorized work.

[KAN-4]: https://kozucharkadiusz.atlassian.net/browse/KAN-4
[KAN-5]: https://kozucharkadiusz.atlassian.net/browse/KAN-5
[KAN-6]: https://kozucharkadiusz.atlassian.net/browse/KAN-6
[KAN-7]: https://kozucharkadiusz.atlassian.net/browse/KAN-7
[KAN-8]: https://kozucharkadiusz.atlassian.net/browse/KAN-8
[KAN-9]: https://kozucharkadiusz.atlassian.net/browse/KAN-9
[KAN-10]: https://kozucharkadiusz.atlassian.net/browse/KAN-10
[KAN-11]: https://kozucharkadiusz.atlassian.net/browse/KAN-11
[KAN-12]: https://kozucharkadiusz.atlassian.net/browse/KAN-12
[KAN-13]: https://kozucharkadiusz.atlassian.net/browse/KAN-13
[KAN-14]: https://kozucharkadiusz.atlassian.net/browse/KAN-14
[KAN-15]: https://kozucharkadiusz.atlassian.net/browse/KAN-15
[KAN-16]: https://kozucharkadiusz.atlassian.net/browse/KAN-16
[KAN-17]: https://kozucharkadiusz.atlassian.net/browse/KAN-17
[KAN-18]: https://kozucharkadiusz.atlassian.net/browse/KAN-18
[KAN-19]: https://kozucharkadiusz.atlassian.net/browse/KAN-19
[KAN-21]: https://kozucharkadiusz.atlassian.net/browse/KAN-21
[KAN-23]: https://kozucharkadiusz.atlassian.net/browse/KAN-23
[KAN-24]: https://kozucharkadiusz.atlassian.net/browse/KAN-24
[KAN-25]: https://kozucharkadiusz.atlassian.net/browse/KAN-25
[KAN-26]: https://kozucharkadiusz.atlassian.net/browse/KAN-26
[KAN-28]: https://kozucharkadiusz.atlassian.net/browse/KAN-28
[KAN-29]: https://kozucharkadiusz.atlassian.net/browse/KAN-29
[KAN-31]: https://kozucharkadiusz.atlassian.net/browse/KAN-31
[KAN-33]: https://kozucharkadiusz.atlassian.net/browse/KAN-33
[KAN-35]: https://kozucharkadiusz.atlassian.net/browse/KAN-35
[KAN-37]: https://kozucharkadiusz.atlassian.net/browse/KAN-37
[KAN-39]: https://kozucharkadiusz.atlassian.net/browse/KAN-39
[KAN-42]: https://kozucharkadiusz.atlassian.net/browse/KAN-42
[KAN-43]: https://kozucharkadiusz.atlassian.net/browse/KAN-43
[KAN-44]: https://kozucharkadiusz.atlassian.net/browse/KAN-44
[KAN-45]: https://kozucharkadiusz.atlassian.net/browse/KAN-45
[KAN-46]: https://kozucharkadiusz.atlassian.net/browse/KAN-46
[KAN-47]: https://kozucharkadiusz.atlassian.net/browse/KAN-47
[KAN-50]: https://kozucharkadiusz.atlassian.net/browse/KAN-50
[KAN-51]: https://kozucharkadiusz.atlassian.net/browse/KAN-51
[KAN-55]: https://kozucharkadiusz.atlassian.net/browse/KAN-55
[KAN-56]: https://kozucharkadiusz.atlassian.net/browse/KAN-56
[KAN-58]: https://kozucharkadiusz.atlassian.net/browse/KAN-58
[KAN-59]: https://kozucharkadiusz.atlassian.net/browse/KAN-59
[KAN-61]: https://kozucharkadiusz.atlassian.net/browse/KAN-61
[KAN-62]: https://kozucharkadiusz.atlassian.net/browse/KAN-62
[KAN-64]: https://kozucharkadiusz.atlassian.net/browse/KAN-64
[KAN-65]: https://kozucharkadiusz.atlassian.net/browse/KAN-65
[KAN-66]: https://kozucharkadiusz.atlassian.net/browse/KAN-66
[KAN-67]: https://kozucharkadiusz.atlassian.net/browse/KAN-67
[KAN-68]: https://kozucharkadiusz.atlassian.net/browse/KAN-68
[KAN-69]: https://kozucharkadiusz.atlassian.net/browse/KAN-69
[KAN-70]: https://kozucharkadiusz.atlassian.net/browse/KAN-70
[KAN-71]: https://kozucharkadiusz.atlassian.net/browse/KAN-71
[KAN-73]: https://kozucharkadiusz.atlassian.net/browse/KAN-73
[KAN-74]: https://kozucharkadiusz.atlassian.net/browse/KAN-74
[KAN-75]: https://kozucharkadiusz.atlassian.net/browse/KAN-75
[KAN-80]: https://kozucharkadiusz.atlassian.net/browse/KAN-80
[KAN-85]: https://kozucharkadiusz.atlassian.net/browse/KAN-85
[KAN-86]: https://kozucharkadiusz.atlassian.net/browse/KAN-86
[KAN-87]: https://kozucharkadiusz.atlassian.net/browse/KAN-87
[KAN-89]: https://kozucharkadiusz.atlassian.net/browse/KAN-89
[KAN-90]: https://kozucharkadiusz.atlassian.net/browse/KAN-90
[KAN-91]: https://kozucharkadiusz.atlassian.net/browse/KAN-91
[KAN-92]: https://kozucharkadiusz.atlassian.net/browse/KAN-92
[KAN-93]: https://kozucharkadiusz.atlassian.net/browse/KAN-93
[KAN-94]: https://kozucharkadiusz.atlassian.net/browse/KAN-94
[KAN-96]: https://kozucharkadiusz.atlassian.net/browse/KAN-96
[KAN-97]: https://kozucharkadiusz.atlassian.net/browse/KAN-97
[KAN-98]: https://kozucharkadiusz.atlassian.net/browse/KAN-98
[KAN-99]: https://kozucharkadiusz.atlassian.net/browse/KAN-99
[KAN-100]: https://kozucharkadiusz.atlassian.net/browse/KAN-100
[KAN-101]: https://kozucharkadiusz.atlassian.net/browse/KAN-101
[KAN-102]: https://kozucharkadiusz.atlassian.net/browse/KAN-102
[KAN-103]: https://kozucharkadiusz.atlassian.net/browse/KAN-103
[KAN-104]: https://kozucharkadiusz.atlassian.net/browse/KAN-104
[KAN-105]: https://kozucharkadiusz.atlassian.net/browse/KAN-105
[KAN-106]: https://kozucharkadiusz.atlassian.net/browse/KAN-106
[KAN-107]: https://kozucharkadiusz.atlassian.net/browse/KAN-107
[KAN-108]: https://kozucharkadiusz.atlassian.net/browse/KAN-108
[KAN-109]: https://kozucharkadiusz.atlassian.net/browse/KAN-109
[KAN-110]: https://kozucharkadiusz.atlassian.net/browse/KAN-110

[KAN-113]: https://kozucharkadiusz.atlassian.net/browse/KAN-113

[KAN-114]: https://kozucharkadiusz.atlassian.net/browse/KAN-114

[KAN-115]: https://kozucharkadiusz.atlassian.net/browse/KAN-115
