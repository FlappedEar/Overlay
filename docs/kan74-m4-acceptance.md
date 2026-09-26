# KAN-74 — M4 acceptance: report → loss → corner → evidence

Recorded 26 September 2026. Task 064 accepts the M4 increment, KAN-59 through
KAN-73: time losses, consistency and variability, G-G, recorded temperatures
and heart rate, the computed day report and its screen, and the areas to
inspect next. The final merge SHAs are in the Jira comments.

The synthetic evidence runs generated VBO fixtures through the real import,
lap derivation, segment review, persistence, report and comparison code. The
real-recording evidence (the owner's private Jastrząb day) is reported
separately below. **Physical Mac acceptance is not claimed.** The QML tests
open real windows (Cocoa QPA) on this Mac and on the hosted macOS runners,
but no one has used an installed candidate by hand. That belongs to M5.

## Acceptance criteria and evidence

| Criterion | Evidence |
| --- | --- |
| Full and limited fixtures | `acceptsM4ReportLossCornerEvidenceWorkflow`: two full recordings (heart rate, coolant, calculated longitudinal and lateral G, known-time warped laps) and one limited recording of the same route with GPS only |
| Absent heart rate, temperature and G | The limited run has no `heartRate` entry and no temperature channels in the report, so nothing is invented. Its lap has no G-G data (`valid: false` with a reason) and no heart rate (no mean) in the comparison, while the full lap beside it has both. Also `showsRecordedTemperaturesThroughTheDayInQml` and `showsHeartRateByRunAndSegmentInQml` ("Not recorded"), and `presentsDayReportWithEvidenceNavigation` ("No temperature recorded.") |
| Reported values agree with source data | Best lap 48.000 s, as every warped lap is generated. Theoretical total between the all-quick bound (43.2 s) and 47.5 s, with the difference equal to actual minus theoretical. Heart-rate means exactly 140 and 150 bpm. Coolant minimum at the generated start (80 or 85) and maximum within start + 0.01 per row. Lateral G peaks at the generated 1.0 and 0.9 |
| Exclusions | Excluding the best lap changes the decisions key, and the theoretical best shows no old value. After recalculation the best lap moves, consistency counts one lap fewer, and no theoretical-best evidence points to the excluded lap |
| Changed segmentation | Splitting an approved segment invalidates the theoretical best. The recalculated report has one more sector and a new decisions key |
| Navigation without video | The first ranked loss opens the comparison with the Corner Analyzer on its segment, with metrics. The first focus area opens its lap pair. The best lap opens its detail. None of the runs has video |
| Save and reopen | The saved project reopens with the same decisions key, best lap, theoretical total, sector count, focus areas and consistency count. The loss navigation works again after reopening |
| Report screen | `presentsDayReportWithEvidenceNavigation` (KAN-72) and the QML part of `selectsFocusAreasFromComputedObservations` (KAN-73), with production QML and no warnings |

## Defect found and fixed during this acceptance

**Save As discarded computed results.** The outing-lap key includes the
project path, so saving to a new path re-derives the laps. While they were
reloading, the theoretical-best invalidation computed its input key from the
transient state and reset the result to idle. The time losses, sections by
session and focus areas went with it. The channel summaries were reset on
any lap change. On the real day that meant about 20 s of recalculation after
the first save, although nothing had changed.

Both results now decide only after the laps have loaded:
- the theoretical best compares its decisions key;
- the channel summaries compare a key of each run's recording identity,
  which does not depend on the path.

A result from a worker that is really stale is still rejected by its request
number. The acceptance test saves to a new path and requires both results
to stay `ready` with the same decisions key. With the fix disabled, the test
fails (`idle`).

## Real-recording evidence (private, separate)

The Jastrząb day (6 VBO files, 37 recorded sections, 23 eligible laps; not
committed; see "Private real fixtures" in `docs/testing.md`), run through
`TelemetryTests::analyzesPrivateTrackDayCorners`:

- **Report:** all eight report results plus the focus areas are available.
  The report is 93 KB of JSON.
- **Best lap and theoretical best:**
  - best lap 1:49.898 (Session 5 · LAP 2);
  - theoretical best 1:47.905, 1.993 s available;
  - largest loss +11.290 s in Corners 9–16 (Session 2 · LAP 1).
- **Where to look next:**
  1. the best lap 0.619 s slower through Corners 9–16 than Session 6 ·
     LAP 3;
  2. Corners 2–3 lost in 5 of 5 session bests (median 2.035 s);
  3. the measured braking point for Corners 5–6 spread over 20.6 m across
     21 laps.
- **Car:**
  - peaks: coolant 103, oil 128, gearbox 113, intake 75;
  - cooling on the cool-down laps (oil about −10° over 2–4 min in
    Sessions 4–6).
- **Heart rate:**
  - session means 119–136 bpm, with complete coverage;
  - Corners 9–16 A/B 123 against 154 bpm.
- **Save and reopen:** saving the real day and reopening it in a new
  controller gives the same report on all 15 key values (decisions key,
  status and evidence count per result, best lap, theoretical total, focus
  observations). The live controller keeps its results across the save.
- **Screenshots:** reviewed locally for the Car & driver tab, the Corner
  Analyzer with heart rate, and the day report with its focus areas. They
  are not committed.

## Known limitations carried into M5

- **Report entry point.** The report opens from Day results → Day report…
  and not automatically after an import. Whether it should be the landing
  view is an owner decision.
- **Day layout.** Session trends are laid out in recording order without
  wall-clock spacing, so the length of the breaks between sessions is not
  drawn.
- **Units.** VBO files do not declare units (KAN-117). Temperatures and
  speeds are shown without an assumed unit.
- **Focus thresholds.** The focus-area thresholds (0.05 s, 10 m, 5%) are
  fixed, not tuned per track. Repeated losses are counted over session
  bests, not every lap.
- **Canonical run.** The M3 limitation remains: segments approved on one run
  are the canonical segmentation for the group.
- **Desktop UI.** The report is a desktop dialog. The phone and tablet
  screens are phase 4 of `docs/product-split-plan.md`.
- **Interactive use.** The harness drives the UI with the keyboard. The
  owner's interactive check at the track (27 September 2026) is outstanding
  and belongs in this record when done.
