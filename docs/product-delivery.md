# Flapped Ear Telemetry — audit and delivery ledger

Audit date: 13 September 2026; M1 acceptance and reforecast for [KAN-28].
Original KAN-12 audit: 12 September 2026 (retained in Git history).
Product scope: [product contract](product-vision.md).
This is the current delivery authority; old checkpoints and narrow beta documents
must not override it. Feature implementation is not the same as runtime acceptance.

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
  6.8.3: seven CTest registrations, 501 Qt Test passes, zero failures and eight
  explicit private/hardware skips per configuration. Release also exercises
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
| F00 | Partial: transactional multi-file import, portable event projects, day metadata, automatic track identity and persistent decisions | Folder/drop, reusable profiles and existing-run alternatives: [KAN-87]–[KAN-90]; whole-product acceptance remains in M5/M6 |
| F01 | Partial: inspectable OUT/LAP/IN, stable references, exclusions and compatible best-run/day rankings; automatic GPS layout/direction grouping | Independent A/B, shared progress and delta: [KAN-29]–[KAN-42] |
| F02 | Partial: versioned, validated segment data model (KAN-43); smoothed heading/curvature feature derivation on the shared axis (KAN-44); automatic straight/corner proposals with boundary uncertainty (KAN-45); geometric entry/apex/exit proposals (KAN-46); measured/inferred braking-onset candidates (KAN-47); proposal review with approve/reject/edit, map overlay and an identified approved revision (KAN-48); approved-segment editing with split/merge, map picking and session undo/redo (KAN-49); persisted review decisions and calculation-revision stamps (KAN-50) | Synthetic M3 acceptance recorded in `docs/kan58-m3-acceptance.md` ([KAN-58]); real-track feedback pending |
| F03 | Partial: non-overlapping loss windows per approved segment for an A/B pair, with corner continuation onto the following straight and signed increments kept separate from the running delta (KAN-59); day ranking of every eligible lap's largest observed losses against the group's best lap, with segment, compared laps, coverage and method (KAN-60); a loss opens its A/B pair in the Corner Analyzer on that window, lap A or B opens at the window with the run's video when available, and returning keeps the ranking (KAN-61) | Non-overlapping ranked losses and evidence navigation: [KAN-59]–[KAN-61]; acceptance: [KAN-74] |
| F04 | Partial: per-sector theoretical best across a compatible population, with the donor lap for each sector and the total withheld when any sector is uncovered (KAN-56); actual best, theoretical and difference with per-sector losses, opening the donor lap in the Corner Analyzer (KAN-57) | Synthetic acceptance recorded ([KAN-58]); real-track feedback pending; separately validated realistic potential: [KAN-94]–[KAN-96] |
| F05 | Partial: single-lap trace and independent cursor, readable channel selector | Shared progress, paired traces/cursor: [KAN-31], [KAN-33], [KAN-37]–[KAN-42]; available-channel map layers: [KAN-97]; interval/map UX: [KAN-114] |
| F06 | Partial: per-lap minimum-speed location kept separate from the geometric apex (KAN-46); braking-onset candidates with measured/inferred provenance (KAN-47); per-lap sector times with coverage and revision (KAN-51); separate entry/apex/minimum/exit speeds with provenance (KAN-52); braking point/time/distance and deceleration with an explicit interval (KAN-53); throttle pickup and following-straight exit effects (KAN-54); Corner Analyzer view with A/B segment metrics (KAN-55) | Synthetic M3 acceptance recorded ([KAN-58]); real-track feedback pending |
| F07 | Missing | Measured/inferred coasting duration, distance and locations: [KAN-92] |
| F08 | Missing | Overlapping driving states with prerequisites and provenance: [KAN-91] |
| F09 | Missing | Braking/cornering overlap, with measured/inferred distinction: [KAN-93] |
| F10 | Partial: lap and per-sector timing consistency (median and interquartile range with sample count, minimum 3, ranking eligibility), day and per run (KAN-62); per-corner braking/apex/exit/pickup and racing-line variability, measured vs inferred kept apart, line spread against GPS accuracy (KAN-63); section-by-session progression with typical time, spread, conditions/setup and the laps behind each figure (KAN-64) | Eligible populations, timing/braking/exit/line variability and presentation: [KAN-62]–[KAN-64]; acceptance: [KAN-74] |
| F11 | Partial: within-day progression, notes, conditions and setup edits; scoped exclusions and persisted choices | Variability: [KAN-64]; reusable profiles and comparable visits: [KAN-89], [KAN-98], [KAN-99] |
| F12 | Raw channels only: recorded temperatures can be plotted | Covered extrema, thermal trends and recorded recovery: [KAN-67], [KAN-68]; acceptance: [KAN-74] |
| F13 | Raw channels only: recorded temperature/performance inputs | Sample-backed associations, without causal claims: [KAN-100] |
| F14 | Partial visualization: G ball/radar/bar and calculated-G aliases; timed longitudinal/lateral pairs with clock alignment, units, provenance and outlier/gap handling (KAN-65) | Timed G-G pairs, scatter and observed peaks: [KAN-65], [KAN-66]; acceptance: [KAN-74] |
| F15 | Partial: central single-video sync and independent video-free detail | Analysis/video navigation: [KAN-39], [KAN-42]; chapter review/timeline/export and side-by-side video: [KAN-104]–[KAN-107]; private-video acceptance: [KAN-80] |
| F16 | Partial foundation: GPS/OBD/HR coexist; alternative files persist | Channel provenance in A/B: [KAN-35]; existing-run alternatives: [KAN-90]; actual cross-file clock alignment/fusion/conflict review: [KAN-101]–[KAN-103] |
| F17 | Raw channels only: imported/plotted HR | Run/lap/sector summaries and comparisons: [KAN-69], [KAN-70]; acceptance: [KAN-74] |
| F18 | Partial: best-run/day results with lap click-through; full report missing | Computed report and evidence observations: [KAN-71]–[KAN-74] |
| F19 | Missing | Computed observation guidance: [KAN-73]; evidence package and Explain this lap: [KAN-108], [KAN-109] |
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
| C07 | Slow/full export destination behavior | Open: [KAN-75] exercises cancellation, scan and transaction cleanup under slow/filling volume |
| C08 | User-visible name/package drift | [KAN-18] standardizes Flapped Ear Telemetry display/About/bundle/package names with settings/recovery preservation checks; macOS integration evidence in Jira; Windows execution paused |

M1 implements compatibility and exclusion rules beyond C02. Route evidence,
direction and timing-definition identity govern grouping; the same date alone
does not establish compatibility. OUT/IN and route outliers remain inspectable.

## Dependency-ordered delivery

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
[KAN-103]: https://kozucharkadiusz.atlassian.net/browse/KAN-103
[KAN-104]: https://kozucharkadiusz.atlassian.net/browse/KAN-104
[KAN-106]: https://kozucharkadiusz.atlassian.net/browse/KAN-106
[KAN-107]: https://kozucharkadiusz.atlassian.net/browse/KAN-107
[KAN-108]: https://kozucharkadiusz.atlassian.net/browse/KAN-108
[KAN-109]: https://kozucharkadiusz.atlassian.net/browse/KAN-109
[KAN-110]: https://kozucharkadiusz.atlassian.net/browse/KAN-110

[KAN-113]: https://kozucharkadiusz.atlassian.net/browse/KAN-113

[KAN-114]: https://kozucharkadiusz.atlassian.net/browse/KAN-114

[KAN-115]: https://kozucharkadiusz.atlassian.net/browse/KAN-115
