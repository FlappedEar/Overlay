# Testing

## Normal local gate

```bash
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
```

Lap Analysis UI tests named in this document (for example `analyzesPrivateTrackDayCorners`,
`selectsIndependentComparisonLapsThroughQml` and the other QML-driven analysis tests) were removed
with the analysis QML by KAN-166 step 4 (5 October 2026). They remain at commit `7eae6cd`
(recorded on KAN-169); their sections are kept as the record of what was verified.
Step 5 removed `AnalysisController` from `AppController` and with it the `TelemetryTests` that
drove the analysis through the editor's controller (run notes, segments, comparison, the day
report). Tests that check what Overlays does with a day's decisions now make those decisions in
`TelemetryController`, which stands in for FlappedEar Telemetry, save the day and open it in
`AppController`: `findsTheDayBestLapWithoutTheAnalysis`,
`restoresDayDecisionsAfterMoveMissingRelinkAndRecovery`,
`lapExclusionsSurviveSaveRecoveryAndInvalidateSafely`, `keepsAnalysisStateThroughOverlayEdits`,
`persistsAndInvalidatesRunTrackConfiguration` and the real-day `automaticallyGroupsPrivateTrackDay`.
Step 6 removed the import menu and `BatchImportDialog.qml` (and `reviewsBatchThroughProductionQml`).
The import tests (`importsSixRunsAndAppendsWithoutDuplicates` and the others) still drive
`DocumentController` directly, through `AppController::m_document`, because its import serves the
Telemetry reference; the user-guide capture builds its day in `TelemetryController`.

KAN-161 (5 October 2026) split the editor's GUI suite, `TelemetryTests.cpp` and its single CTest
entry `flappedear_native_tests`, into five executables, each its own CTest entry, so one failure
stays within its domain and CTest can run them in parallel:

| CTest entry | Source | Covers |
| --- | --- | --- |
| `flappedear_native_tests_editor` | `NativeEditorTests.cpp` | controller presentation, lap state, preview, user-guide capture |
| `flappedear_native_tests_sources` | `NativeSourceTests.cpp` | events and day import, video chapters, media probing, GoPro GPMF, synchronisation |
| `flappedear_native_tests_project` | `NativeProjectTests.cpp` | save, reopen, relink, recovery, the single-instance guard |
| `flappedear_native_tests_export` | `NativeExportTests.cpp` | export targets and storage, process supervision, FFmpeg composition, diagnostics |
| `flappedear_native_tests_widgets` | `NativeWidgetTests.cpp` | widget scenes, templates, the widget library, widget rendering |

Helpers more than one suite uses, the settings isolation and the export-worker `main()` are in
`NativeTestSupport.h`. The 216 test functions are unchanged; a test this document names as
`TelemetryTests::name` is now in the suite for its domain (`-functions` lists a suite's tests).
Run one suite, or one test in it, directly:

```bash
./build-native/native/tests/flappedear_native_tests_sources automaticallyGroupsPrivateTrackDay
```

Run the local gate appropriate to the change before claiming a behavior works. Cloud CI is enabled for macOS Debug and Release by owner direction; Windows CI remains paused, and the owner validates Windows locally ([2 October 2026](windows-validation-2026-10-02.md)). Follow the current [local task workflow](development-workflow.md).

Claude Code cloud sessions (Linux) get Qt 6.8.3 from conda-forge through the SessionStart hook in `.claude/hooks/session-start.sh`, which sets `CMAKE_PREFIX_PATH` and `QT_QPA_PLATFORM=offscreen`. Linux is not a CI platform: report Linux results separately from macOS CI. Export tests need FFmpeg 8.1 or newer on `PATH`; the Ubuntu package (6.1.1) fails the composition-filter preflight.

The native suite assigns a unique test application identity and checks a default `QSettings` round trip before controller tests run. It retains the platform's native settings backend, including the Windows registry, and clears that test namespace afterward. Recovery cleanup failures use the existing injected deletion operation so stale-snapshot and Save As assertions run on every platform; these checks do not replace native Windows ACL-denial coverage. File-content checks close their read handles before attempting atomic replacement.

## M1 acceptance (KAN-28)

The [M1 acceptance record](kan28-m1-acceptance.md) maps the executed multi-run,
configuration, OUT/IN, exclusion, missing-source and reopen scenarios to their
regressions and exact source/CI revisions. It also separates the existing
KAN-26 local private-VBO evidence from hosted synthetic tests and remaining
owner/private-GoPro acceptance. The record contains the measured M0/M1 cycle
times and the 27 September planning target; the current capability/scope ledger
is [product-delivery.md](product-delivery.md).

## Independent A/B selection (KAN-29)

`selectsIndependentComparisonLapsThroughQml` imports matching and reversed routes,
uses the production A/B selectors and keyboard controls, rejects incompatible
pairs, swaps verified sessions, selects best run/group as B and inspects a lap.
It checks that editor run/synchronization and the saved document remain unchanged,
and that exclusions invalidate the affected selection.

`preservesComparisonSlotAcrossFailuresAndReplacement` removes/restores B's source,
checks that A retains its session and map, holds an async completion across a
swap, replaces one editor source and rejects a late completion after New project.
The pair and single-lap inspector share the existing bounded full-content loader;
its fingerprint, hash, range, GPS-gap and cancellation regressions remain enabled.

Interactive acceptance: import a day, open **Compare laps…**, choose compatible
laps from different runs, swap, choose best run/group as B and inspect each. Check
readability and keyboard navigation on the owner's Mac. These checks do not yet
accept shared-progress delta or paired chart/map presentation.

## Shared comparison source budget (KAN-30)

`flappedear_source_cache_tests` exercises reuse with validation on every hit,
fingerprint/revision separation, immutable cadence statistics, lease accounting
after pinned-entry eviction, idle eviction, failed validation, oversized decode,
cancellation after decoding and cancellation while waiting for the decode lock.
VBO tests compare bounded/default output with malformed rows, reject a wide source
before sample allocation and retain malformed-section/cancellation behavior. RCZ
tests retain gap semantics and reject compressed expansion against the allowance;
the existing malformed-archive and parser-limit suites remain mandatory.

Controller regressions verify that A/B and the inspector share a source session,
different derivations get separate entries, cache reuse rejects missing/changed
files, and a source that fits alone is rejected against the pair's remaining
budget without losing A. Clearing A permits B to load. A blocked decoder makes
rapid reselection deterministic: the old request is cancelled and only the final
lap reaches the slot. Existing QML selection, source replacement, document-change
and stale-completion regressions remain enabled.

The coordinator lacks CMake/CTest/Qt. Exact macOS Debug/Release PR and main build,
test and installed-startup evidence is recorded in
[KAN-30](https://kozucharkadiusz.atlassian.net/browse/KAN-30). Synthetic allocation
and lifecycle tests do not establish process RSS, private-recording throughput,
physical Mac acceptance or private GoPro validation. Windows remains paused.

## M2 acceptance and reforecast (KAN-42)

The [KAN-42 M2 acceptance record](kan42-m2-acceptance.md) maps crossings, gaps,
different-lines and known-delta fixture coverage (already established by
KAN-31 through KAN-34) against KAN-42's acceptance criteria, adds the one
disclosed gap (a comparison pair where one recording lacks a channel the
other has) and a two-run-comparison/optional-video editor-independence
exercise, and reforecasts remaining backlog using measured M2 cycle time. It
also documents that dual, side-by-side comparison video is not implemented
by this task (see KAN-104 through KAN-107).

`excludesChannelMissingFromOneComparisonSlot` appends a synthetic channel
column to one recording's VBO text and confirms `comparisonAvailableChannels()`
excludes it while the recording that has it still serves real data, and the
recording that lacks it reports `channelMissing` rather than fabricated or
borrowed values.

`comparesKnownDeltaThroughFullComparisonPipeline` elevates the existing
`TrackProgressTests::knownDelayHasCorrectSignAndFinishLineMagnitude` unit-level
guarantee to the full AppController import → comparison-slot → shared-axis
pipeline, using two separately imported runs of the identical physical path
with a known, uniform 10% time rescale.

`keepsComparisonAndOutingLapVideoIndependent` proves the single central video
slot (gated to the active run's open lap, KAN-39) and the two comparison
slots cannot disturb each other's state while both are populated/open at once.

## Versioned sector and corner model (KAN-43)

`flappedear_track_segments_tests` is a new standalone CTest registration for
`native/src/telemetry/TrackSegments.h/.cpp` (M3's first ticket): a plain-JSON
segment model, following this codebase's established pattern of validating
project-document data as `QJsonObject`/`QJsonArray` rather than persisting a
deserialized C++ struct (mirroring `OutingLaps`' lap references and
`EventProjectCodec`'s `comparisonRange`/`comparisonChannels`).

Each segment carries a stable `QUuid`-minted `id`, a `type` (`"sector"` or
`"corner"`), a `name`, `startProgressMeters`/`endProgressMeters` against the
existing shared `ProgressAxis` (KAN-31), and a `trackConfigurationReference`
in the exact `lapCompatibilityGroupId()` format -- validated for shape only,
not liveness, so a stale reference is a later runtime concern rather than a
load-time document rejection (the same principle stale lap references
already follow). No `ProgressAxis` sample data is ever persisted.

A run's segment array (`run.trackSegments`) must be listed in non-decreasing
start-progress order, bounded to `maximumTrackSegments` (64) entries, with
unique IDs; only the final (highest-start) segment may wrap across the
start/finish line (`endProgressMeters < startProgressMeters`), the one
physically meaningful case -- a segment covering the timing gate itself.
`trackSegmentSetRevision()` is a pure, never-persisted content hash a future
consumer recomputes and compares to detect any add/remove/reorder/edit --
"versioning" without a separate counter to keep in sync, the same
content-addressed approach `gateRevision`/`lapCompatibilityGroupId`/
`lapDerivationKey` already use.

`EventProjectCodec::validate()` now rejects a document whose `trackSegments`
array is malformed, unordered, or exceeds the bound. Automatic corner
detection (KAN-44), review/editing UI (KAN-45+) and Corner Analyzer metrics
(KAN-46, KAN-51-55) are separate, later tickets -- this task is the data
model and its validation only.

## Track progress at the gate (KAN-152)

`TrackProgressTests` covers the start-gate findings of the 2 October audit
with the route fixture's gate turned 45 degrees, its midpoint about 10 m off
the line. `anchorsProgressZeroWhereTheGateCrossesTheAxis` checks progress 0 is
where the reference lap crosses the gate (it was 6 m away, at the point nearest
the midpoint). `obliqueOffLineGateKeepsEveryLapsSectors` checks every lap's
progress never falls and all four quarter sectors are timed, summing to the
lap time. `unwrapsAFirstFixJustBeforeTheGate` checks a lap's first fix just
before progress 0 is stored as a small negative value, so the gate crossing is
interpolated. `deltaAtTheFinishEqualsTheLapTimeDifference` checks the timed
delta is 0 at the start and the lap-time difference at the finish for a lap
10% slower with a shifted clock. Each fails without the change.

## Smoothed heading and curvature on track progress (KAN-44)

`computeTrackFeatures` (`native/src/telemetry/TrackProgress.h/.cpp`) is a
pure function of the existing shared `ProgressAxis` (KAN-31): it never reads
or produces a per-lap projection. Investigation before implementing this
confirmed `buildProgressAxis`'s only ever accepts a `referenceEligible`
(gap-free, continuous) lap trace -- eligibility screening already happens a
layer earlier, in lap-timing -- so there is no per-lap gap concept for this
computation to preserve. "Preserved gaps" is satisfied structurally: the
function only ever reads `axis.points`/`axis.cumulative`/`axis.spacingMeters`
and never writes back to them or to any telemetry channel.

Heading at each axis point is the circular mean (mean of unit tangent
vectors, extending the existing `axisTangent` helper used by lap-trace
projection) of the axis's direction of travel within an explicit
`smoothingMeters` radius on each side, wrapping around the closed loop --
never a naive mean of raw angles, which breaks across the +-pi wrap.
Curvature is the signed angular change between adjacent smoothed headings
divided by the axis's uniform point spacing; positive means a left turn.

Three new `TrackProgressTests` regressions cover the acceptance criterion
directly: the hairpin fixture's two exactly-colinear straights read as
(numerically) exactly zero curvature while its two ~180-degree connectors
spike well above a generous threshold regardless of exact resampling-index
drift; a real closed-circuit fixture's convex ellipse reads a consistently
signed (never sign-flipping) curvature at ten points spread around the whole
loop; and invalid inputs (an invalid axis, non-positive, infinite or NaN
smoothing scale) are rejected without touching the axis geometry the
features were derived from.

Automatic corner/sector *proposals* built from this feature data, and any
review/editing UI, remain separate, later M3 tickets (KAN-45 onward).

## Automatic straight and corner proposals (KAN-45)

`proposeTrackSegments` (`native/src/telemetry/TrackSegmentProposals.h/.cpp`,
algorithm tag `track-segment-proposal-v2`) classifies each axis sample of the
KAN-44 smoothed curvature as straight or left/right turning (explicit
`cornerCurvaturePerMeter` threshold, default 1/250 m), folds turning runs below
`minimumCornerTurnRadians` (default 0.35 rad) back into the straight as kinks,
and emits alternating corner/straight proposals named `Corner N`/`Straight N`
in progress order from the gate.

Corners with no proposed straight between them form one **corner chain**
proposal (KAN-116, tag `v2`). This covers opposite-direction corners with no
straight (an S-bend) and corners separated by a straight shorter than
`connectedStraightMeters` (default 20 m). A chain is named after the corners
it contains (`Corners 2–3`), and `chainedCorners` records how many there are.
Its `turnRadians` is the net heading change, close to zero for an S-bend.
Only a proposed straight separates corners, and a loop with no proposed
straight stays unresolved (`continuousCorner`). Within a chain, a single
geometric apex is reported as `multipleApexes`. Entry, exit, minimum,
braking, sector time and loss windows cover the whole chain. To separate a
chain, split the approved segment. The version bump means review rejections
persisted for v1 proposals are not applied to v2 proposals; approved
segments are unchanged.

Each boundary carries a tolerance (smoothing radius plus axis spacing) and
zero or more uncertainty reasons:

- `shortStraight`: both ends of a straight shorter than
  `certainStraightMeters` (default 40 m).
- `gpsGap`: within tolerance of a caller-supplied progress range lacking GPS
  coverage. The axis itself is built from a gap-free reference lap, so these
  ranges describe the analysed lap(s), not the axis geometry.

A loop with no boundary (e.g. a constant-radius circle) returns no proposals
with `unresolvedReason` set rather than inventing one. Proposals carry no IDs
or approval state; `proposalsToTrackSegments` converts them into ordinary
editable track segments with fresh IDs. The segment model (`track-segment-v2`)
now accepts a `straight` type. Uncertainty is not persisted in segments;
review/approval (KAN-48), editing (KAN-49) and persistence (KAN-50) are
separate tickets.

`TrackSegmentProposalTests` builds exact line/arc loops through the real
`buildProgressAxis` → `computeTrackFeatures` → `proposeTrackSegments` pipeline:
a stadium (4 certain proposals, ~pi corner turns, wrap at the gate), the same
stadium with a GPS-gap range, left-right chicanes (each one corner chain,
`Corners 2–3` numbering, near-zero net turn), 90-degree corners 16 m apart
(one same-direction chain), 44 m straights between 90-degree corners (short-straight
boundaries), 10-degree kinks (folded into straights), a circle (unresolved),
conversion/editing of proposals as segments, and invalid inputs. Expected
values were checked beforehand with a line-for-line Python model of the same
algorithm. These are synthetic fixtures only; no real VBO recording has been
segmented yet.

## Corner entry, apex, exit and minimum speed (KAN-46)

`native/src/telemetry/CornerPhases.h/.cpp` (algorithm tag `corner-phase-v1`)
keeps geometry and driving apart:

- `proposeCornerGeometryPhases` (per axis, shared by every lap): entry
  (`curvatureOnset`) and exit (`curvatureRelease`) reuse the KAN-45 corner
  boundaries with their tolerance and uncertainty. The apex
  (`peakCurvatureRegion`) is the midpoint of the corner's high-curvature
  region: curvature at or above 80% of the corner peak opens a region, which
  closes only once curvature falls below 60% (hysteresis, so ripple on a
  constant-radius arc is not split into false apexes). More than one region
  leaves the apex unresolved (`multipleApexes`) and lists every candidate; a
  region wider than the smoothing tolerance is located at its midpoint with a
  widened tolerance and `broadPeak`.
- `locateMinimumSpeed` (per lap): samples the lap's `speed` channel every
  `stepMeters` across the corner through its projected trace. It is never
  derived from the apex. Missing speed channel, any sample without projected
  GPS coverage or a speed value, a flat speed, or a corner crossing the gate
  (the two halves are at opposite ends of a gate-to-gate lap) leave it
  unresolved rather than searching around the hole. A minimum touching the
  corner boundary is flagged `atCornerBoundary`.

Every phase exposes its method, tolerance and an evidence object (peak
curvature and region, or channel, unit, measured minimum, telemetry time and
sample counts). `CornerPhaseTests` uses shared line/arc loops
(`native/tests/SyntheticLoopFixture.h`, also used by KAN-45 tests) and a
synthetic 20 Hz GPS/speed lap: a single tight arc (apex at the arc, minimum
speed placed ~28 m later), two tight arcs joined by a gentle one (unresolved,
two candidates), a constant-radius semicircle (broad peak), and GPS gaps, NaN
speed, missing speed channel, flat speed, a gate-crossing corner and invalid
inputs. Expected apex positions were checked beforehand with a Python model.
Synthetic fixtures only; braking onset (KAN-47) and review UI (KAN-48) are
separate.

## Braking-onset candidates with explicit provenance (KAN-47)

`detectBrakingOnsets` (`native/src/telemetry/BrakingOnset.h/.cpp`, algorithm
tag `braking-onset-v1`) scans raw samples in a time window, never
interpolating across gaps:

- A session with a `brake` channel always uses it (`measuredBrake`,
  provenance `measured`) with explicit hysteresis thresholds (default on 10 /
  off 5, unit `%`). Deceleration is never substituted, even where the brake
  channel has no data in the window (`noSamplesInWindow`).
- Only without a brake channel does it use `longitudinalAcceleration`
  (`inferredDeceleration`, provenance `inferred`; default on 0.30 / off 0.15
  g, braking = negative G as in the analysis charts). Inference can be
  disabled. With neither channel the result is `noBrakeOrDecelerationChannel`.
  No brake value is manufactured.
- Thresholds carry a unit. A channel declaring a different unit is
  unresolved (`unitMismatch`); VBO channels currently declare no unit, so
  their candidates carry `channelUnitUndeclared`.
- Onset is the interpolated on-threshold crossing between two contiguous
  samples (tolerance: that sample interval). Episodes shorter than
  `minimumDurationSeconds` (0.2 s) are counted as rejected spikes. A gap
  (non-finite value or a timestamp jump over three median intervals) ends an
  episode (`interruptedByGap`); an onset right after a gap is `followsGap`;
  window edges give `alreadyBrakingAtWindowStart` / `truncatedAtWindowEnd`.
- The result reports method, provenance, channel, declared unit, thresholds,
  minimum duration, rejected spikes and gap count. An optional projected lap
  trace maps each onset to track progress.

`BrakingOnsetTests` uses synthetic 20 Hz channels: a known 10% crossing at
5.025 s (with a competing deceleration event ignored), a one-sample spike,
chatter between the thresholds, missing and non-finite samples, window
edges, inferred deceleration at 6.075 s, disabled inference, no substitution
for an empty brake channel, `bar` vs `%` units, undeclared units, progress
mapping and invalid inputs. Synthetic only; no real brake-sensor recording
has been validated.

## Segment-proposal review and approved revision (KAN-48)

`native/src/telemetry/TrackSegmentReview.h/.cpp` (tag
`track-segment-review-v1`) holds the pure review rules; the controller glue is
`native/src/app/AnalysisControllerSegmentReview.cpp` and the UI is
`SegmentReviewPanel.qml` with a static segment layer in `TrackMapPanel.qml`.

- Opening *Review segments* on a clean timed lap builds a progress axis from
  that lap and runs `proposeTrackSegments` (smoothing 6 m) and
  `proposeCornerGeometryPhases` off the UI thread. The worker is cancellable and
  its result is dropped unless its request matches the current review, so
  closing or switching the lap cannot apply a stale proposal set. OUT/IN
  sections, laps with GPS or layout issues, and laps without a resolved track
  configuration are reported as unavailable instead of guessing.
- Coverage holes of 15 m or more in the lap's own projected trace are passed
  as GPS gaps, so nearby boundaries are marked uncertain.
- A proposal is `proposed`, `approved` (an approved segment has exactly its
  bounds and type), `rejected` (review session only), or `superseded` (it
  overlaps an approved segment from elsewhere and cannot be approved).
- Approving writes an ordinary segment into the run's `trackSegments`. It is
  refused if it overlaps an approved segment (including across the
  start/finish line), if segments approved for another track configuration are
  still stored (they can only be discarded explicitly), or above the 64-segment
  bound. *Approve all certain* skips any proposal with an uncertain boundary.
- Edits (name, type, numeric bounds) are validated (finite, within the axis,
  non-empty, 1–160 character name). A moved boundary drops the automatic
  uncertainty, and the geometric apex is hidden for an edited proposal.
  Approved segments are edited in the approved-segment editor (KAN-49).
- `approvedSegmentation` gives the segments for one configuration and their
  `trackSegmentSetRevision`. Results record a `SegmentationResultStamp` and are
  current only while `segmentationResultCurrent` holds. Approval, revocation,
  rename or a configuration change all produce a different revision.
- The map draws approved segments (green), proposals (blue corners, grey
  straights) and uncertainty windows (orange) along the lap's own GPS trace,
  breaking at coverage holes. This static layer is repainted only when the
  review changes, never on playback.

`TrackSegmentReviewTests` covers state derivation, overlap including
wrap-around, ordering, configuration mixing, revocation and discard, revision
stamps, malformed edits and stored data, the segment bound, and approving
every stadium proposal. `TelemetryTests::reviewsSegmentProposalsForTheOpenLap`
drives the controller on the synthetic elliptical route. It covers an
unavailable non-lap section, stale-result rejection after closing the lap,
finite map layers, QML panel/map loading, approve/reject/edit/supersede/revoke,
dirty state, revision changes, and save/reopen matching of the approved segment.
Synthetic only; no real track has been reviewed. Proposal edits are not
persisted; rejections are persisted since KAN-50.

## Editing approved segments (KAN-49)

`native/src/telemetry/TrackSegmentEditing.h/.cpp` (tag
`track-segment-editing-v1`) holds the pure editing rules; the controller glue
is in `AnalysisControllerSegmentReview.cpp` and the editor is the approved-segment
list in `SegmentReviewPanel.qml`.

- Every operation takes the run's stored `trackSegments` and returns a
  complete, ordered replacement (the one gate-crossing segment last) or a
  reason. Overlap, empty segments, bounds outside the axis, invalid names or
  types, the 64-segment bound and stored segments from another track
  configuration are refused.
- Stable identity: edit, move and rename keep the segment's ID; a split keeps
  the ID on the first part and gives the second part a fresh ID and the name
  "<name> (2)"; a merge keeps the earlier segment's ID and name, and becomes a
  `sector` when the two types differ. Only segments that share a boundary
  merge, and a merge that would span the whole lap is refused. Progress 0 and
  the lap length are the same boundary.
- *Move adjoining segments with shared boundaries* (on by default) moves a
  neighbour whose boundary coincided with the moved one; otherwise a move into
  a neighbour is refused as overlap. Gaps between segments are allowed.
- Boundaries are entered numerically or picked on the map. A tap maps to the
  nearest lap-trace sample within 3% of the map; it is refused when another
  part of the track at least 30 m away along the lap is within 1% of that
  distance (crossings and close parallel sections), so a pick is never guessed.
- History policy: there was no document undo before this ticket. Segment
  changes follow the existing document policy (dirty state, atomic save,
  recovery) and add a bounded (50-step) undo/redo for the open review
  session, covering approvals, revocations, edits, splits and merges. A step
  is applied only while the run still holds exactly the state it left; any
  other change clears the history instead of being overwritten. History is
  not saved and is cleared when the review is reset or recomputed.
- Any change produces a new `trackSegmentSetRevision`, so results stamped with
  the old revision become stale (`segmentationResultCurrent` fails).

`TrackSegmentEditingTests` covers identity, revision change, joined/unjoined
moves, gaps, invalid edits, mixed configurations, split inside/at/across the
gate, adjacent-only merge including across the gate and the whole-lap refusal,
the segment bound, history order/redo clearing/bound, and picking on a
figure-eight crossing (nearest, ambiguous, far, empty).
`TelemetryTests::editsApprovedSegmentsWithUndo` drives the controller on the
synthetic route: map pick inside an approved segment, rename, joined move,
refused overlap and empty edits, split and merge with stable IDs, the editor
list in QML, six-step undo and redo, and refusal to undo over a change made
outside the history. Synthetic only; the map pick has not been exercised
interactively, and no real track has been edited.

## Persisted segmentation and calculation revisions (KAN-50)

Approved segments already lived in the run's `trackSegments`, which the
existing atomic save and recovery snapshot carry unchanged. KAN-50 adds:

- `trackSegmentReview` on the run (validated by `EventProjectCodec`): the
  review's rejections, stored by segment type and exact bounds together with
  the track configuration reference and the proposal algorithm tag. On review,
  rejections are restored only when both still match; a recomputed proposal
  with different bounds is not treated as the rejected one. At most 64
  decisions; an empty set removes the key. Approval itself remains the
  presence of a segment in `trackSegments`; proposal edits are session-only.
- `SegmentationResultStamp` now carries the result's `calculationAlgorithm`
  and has a strict JSON round trip (`segmentationResultStampToJson` /
  `segmentationResultStampFromJson`). A result is current only while the
  configuration reference (layout, direction and timing gate), the approved
  segment revision and the calculation algorithm all match; with no approved
  revision it is never current. No sector, theoretical-lap or report result
  is persisted yet; later tickets must store and check this stamp.

`TrackSegmentReviewTests` covers rejection round trips per configuration and
algorithm, moved bounds, malformed and oversized decisions, stamp JSON round
trip and rejection of malformed stamps, and staleness on algorithm,
configuration and segment changes.
`TelemetryTests::persistsSegmentationAcrossSaveRecoveryAndReopen` drives the
controller through save and reopen (IDs, names, bounds, a rejected proposal and
a still-current stamp), an unsaved approval restored from the recovery
snapshot (earlier stamp now stale), and a layout change that keeps the stored
segments and their IDs but applies none of them, so no stamp stays current.
The timing gate cannot be edited directly; it changes the same configuration
reference when telemetry is replaced. Synthetic only.

## Sector times and coverage per lap (KAN-51)

`computeLapSectorTimes` (`native/src/telemetry/SectorTiming.h/.cpp`, tag
`sector-timing-v1`) times each approved segment on one lap's projection onto
the shared progress axis:

- Boundary crossing times are interpolated between projected samples
  (`timeAtProgress`). Boundaries at progress 0 and the axis length use the
  lap's own timed start and end, so a complete partition telescopes to the lap
  time; `sectorSumToleranceSeconds` (1 ms) bounds the difference.
- Coverage: the lap's projected ranges are merged; an end within 15 m of the
  gate counts as reaching it (projection starts a sample or two past the gate).
  Each sector reports `coveredMeters`. A sector has a numeric time only when
  one covered range spans it and both crossings exist; otherwise it is
  `incompleteCoverage` with no time, never bridged across a gap.
- A gate-crossing sector is timed within the lap as its part after the start
  crossing plus its part before the end crossing (KAN-120; formerly
  `crossesGate`). `completePartition` is true only when approved segments
  tile [0, axis length] without gaps or a gate-crossing segment; only then,
  with every sector timed, are `sumSeconds` and `partitionErrorSeconds` set.
- Each result carries the lap reference, segment IDs and a
  `SegmentationResultStamp` tagged `sector-timing-v1`.
- `AppController::outingLapSectorTimes()` exposes the reviewed lap's sector
  times; there is no UI for them yet.

Axis limitation: segment bounds are distances along the axis built when the
segments were approved (the reviewed lap's own axis). Timing a different lap
uses that lap's axis, so boundaries can shift by the difference in lap
lengths along the racing line (typically metres). A canonical axis per track
configuration is not implemented yet.

`SectorTimingTests` uses a constant-speed projected lap: interpolated
crossings between samples, gate boundaries from the lap timing, a complete
partition summing within tolerance, a coverage hole leaving only the affected
sector untimed (and a boundary inside the hole without a crossing time),
gate-crossing and gapped partitions, no approved segments, and invalid inputs.
`TelemetryTests::timesApprovedSectorsForTheOpenLap` approves every proposal on
the synthetic route, splits the gate-crossing one at the gate and checks that
every sector is timed and the sum matches the lap time within tolerance.
Synthetic only.

## Corner entry, minimum, apex and exit speeds (KAN-52)

`computeCornerSpeeds` (`native/src/telemetry/CornerSpeeds.h/.cpp`, tag
`corner-speeds-v1`) reports four separate values for one approved segment on
one lap, read from the recorded `speed` channel (provenance `measured`):

- entry and exit: speed where the lap crosses the segment's start and end;
- apex: speed at the geometric apex (`proposeCornerGeometryPhases` on the
  segment via `cornerFromSegment`); unavailable when the apex is unresolved
  (`multipleApexes`) or the segment is not a corner (`notACorner`), and
  limited by `broadPeak` when the apex region is wide;
- minimum: `locateMinimumSpeed` inside the segment, never the apex.

No value is derived from GPS positions: without a speed channel every value
is `speedChannelMissing` and provenance is `unavailable`. A value at a point
without projected coverage or speed data is `incompleteCoverage`; a
gate-crossing segment is `crossesGate`. When recorded speed samples inside the
segment are more than 10 m apart on average, every present value carries
`sparseSamples`. Results carry covered metres, mean sample spacing and a
`SegmentationResultStamp` tagged `corner-speeds-v1`.
`AppController::outingLapCornerSpeeds()` returns them for the reviewed lap's
approved corners; there is no UI for them yet.

`CornerPhaseTests` covers four separate values on the single-apex fixture
(apex speed read ~28 m before the slowest point), a GPS gap removing only the
minimum, sparse samples marked as limited, a missing speed channel, two
apexes, a straight segment and a gate-crossing segment.
`TelemetryTests::timesApprovedSectorsForTheOpenLap` checks that the synthetic
route, which has no speed channel, yields explicitly unavailable corner
speeds. The axis limitation of KAN-51 applies. Synthetic only.

## Braking point, distance and deceleration (KAN-53)

`computeBrakingMetrics` (`native/src/telemetry/BrakingMetrics.h/.cpp`, tag
`braking-metrics-v1`) defines its reference explicitly: shared-axis progress,
with a search interval from 200 m (`approachMeters`) before the segment's
start boundary to its end boundary, clipped at the gate
(`approachClippedAtGate`). The braking point is the first KAN-47 onset
candidate in that interval, keeping its method (`measuredBrake` /
`inferredDeceleration`), provenance, channel and unit-tagged threshold. It
reports the distance before the entry boundary (negative inside the segment),
the episode's time, and its distance only when one continuous projection spans
onset to episode end. Peak and mean deceleration (positive magnitudes of
negative longitudinal G, in the channel's unit) are read only from the
recorded `longitudinalAcceleration` channel over the same episode, and only
when its samples cover the episode without a gap. Missing coverage at the
interval start, a gate-crossing segment, no channel and no onset each give an
explicit reason instead of a number.

`compareBrakingMetrics` returns A minus B for the same segment and revision
(braking point positive when A brakes later) and refuses to compare different
methods, provenances or channels (`mixedProvenance`).
`AppController::outingLapBrakingMetrics()` returns single-lap metrics for the
reviewed lap's approved corners; A/B wiring and UI land with the Corner
Analyzer.

`BrakingMetricsTests` uses a constant-speed projected lap and 20 Hz channels:
an interpolated measured onset at 22.025 s (440.5 m) with distance before entry,
time and distance, peak/mean deceleration, the inferred path, a missing
acceleration channel, projection holes inside the episode and at the interval
start, a missing acceleration sample, no onset in the interval, no channels, a
gate-crossing segment, a clipped approach, and A/B comparison including mixed
provenance and different revisions. `TelemetryTests` checks that the synthetic
route, with no brake or acceleration channel, yields no braking values. The
KAN-51 axis limitation applies. Synthetic only; no real brake sensor.

## Throttle pickup and downstream exit effects (KAN-54)

`computeExitMetrics` (`native/src/telemetry/ExitMetrics.h/.cpp`, tag
`exit-metrics-v1`) defines its intervals explicitly:

- Pickup is searched inside the segment ([start, end] on shared-axis
  progress): the first rise to the on-threshold after the channel was at or
  below the off-threshold, held at or above the off-threshold for 0.2 s. It is
  measured from the recorded `throttle` channel (`measuredThrottle`, 20 / 10 %)
  whenever one exists; only without it is a positive longitudinal-acceleration
  onset reported (`inferredAcceleration`, 0.10 / 0.05 g), labelled inferred.
  A throttle never lifted is `noLift`; lifted but never reapplied is
  `noPickupDetected`; brief blips are ignored; a rise right after missing
  samples is reported where data resumes with `followsGap`; declared units
  must match (`unitMismatch`), undeclared ones are flagged.
- The downstream interval starts at the segment's end boundary and ends at the
  end of the adjoining approved straight (`followingStraight`) or 200 m later
  (`fixedDistance`). Exit speed (at the end boundary) and speed at the
  interval end come only from the recorded speed channel; elapsed time needs
  continuous projected coverage of the whole interval, and an interval ending
  at the gate uses the lap's timed end. An interval continuing past the gate
  is `crossesGate`.
- `compareExitMetrics` gives A minus B (pickup positive when A picks up later)
  for the same segment, revision and interval. Pickups from different methods
  are not compared; speeds and elapsed time are compared as numbers only. No
  cause is attributed to any difference.
- `AppController::outingLapExitMetrics()` returns single-lap values for the
  reviewed lap's approved corners; A/B wiring and UI land with the Corner
  Analyzer.

`ExitMetricsTests` uses a constant-speed projected lap and 20 Hz channels:
interpolated measured pickup at 26.9875 s (539.75 m), the following straight
(exit 80 km/h, end 90 km/h, 10 s), the inferred path, no channels, no speed
channel, no lift, no pickup, a blip, missing samples, unit mismatch and
undeclared units, fixed-distance and gate-ending intervals, a gate-crossing
interval and segment, a projection hole, invalid options, and A/B comparison
including mixed provenance and a different revision. `TelemetryTests` checks
that the synthetic route yields no pickup and no speeds, while following
intervals are still timed. The KAN-51 axis limitation applies. Synthetic only.

## Sector theoretical best across a population (KAN-56)

`computeTheoreticalBest` (`native/src/telemetry/TheoreticalBest.h/.cpp`, tag
`theoretical-best-v1`) takes one `LapSectorTimes` per lap and, for each
approved segment, keeps the fastest numeric time together with the lap
reference that produced it (the donor lap). Results stamped with another
revision or configuration reference are ignored. A segment with no timed lap
reports `incompleteCoverage`; the other segments stay visible, but the total
is withheld. With no approved segmentation nothing is computed
(`noApprovedSegmentation`).

The population is `eligibleOutingLaps` (`OutingLaps.h`), which applies the
same per-lap reasons as `rankOutingLaps` (compatibility group, exclusions,
GPS issues, stale sources, invalid references). `rankOutingLaps` now calls the
same helper, so the two cannot disagree.

`AppController::requestOutingTheoreticalBest()` runs in the background for the
current comparison group. Segments are approved per run, so it uses one
canonical run: the lowest run ID among eligible runs that has approved
segments for the group. One shared progress axis is built from a lap of that
run, and every eligible lap, from any run, is projected onto it before its
sector times are computed. This avoids the per-lap axis limitation of KAN-51
for this result. Laps are processed grouped by run, so each recording is
decoded once, and a lap whose recording cannot be decoded is skipped. The
result, `outingTheoreticalBest`, is invalidated whenever outing laps or the
document change. Choosing the lowest run ID is deterministic but arbitrary
when more than one run has approved segments.

`TheoreticalBestTests` covers per-segment winners from different laps, a
segment no lap covers (total withheld, other segments kept), a faster lap
from another revision being ignored, and no approved segmentation.
`LapEligibilityTests::exposesEligiblePopulationMatchingRanking` checks that
the population matches ranking, including exclusions, stale runs, other groups
and ineligible laps. `TelemetryTests::calculatesOutingTheoreticalBestAcrossPopulation`
checks that the result is unavailable before approval, then approves one
run's segments and checks each timed segment's donor label and whether a
total is present. Synthetic only.

## Theoretical best view and donor navigation (KAN-57)

Day results → **Theoretical best…** (`TheoreticalBestDialog.qml`) requests
the KAN-56 calculation for the current comparison group. It shows:

- **Actual best**: the group's best-of-day lap. The worker also times this
  lap on the canonical axis. If the approved sectors cover the whole lap
  (`completePartition`), its lap time is shown. Otherwise, the sum of its own
  times over the same sectors is shown, and the dialog says so.
- **Sector theoretical**: the KAN-56 total. **Difference** is actual best
  minus theoretical over the same sectors. It is shown only when both are
  complete.
- The algorithm tag (`theoretical-best-v1`) and a statement that the sum
  combines fragments of different laps and does not show that the whole lap
  can be driven that fast.
- One row per sector: best time, donor lap, the actual best lap's time and
  the loss (actual minus best). The actual best is part of the population,
  so a loss is never negative.

Selecting a timed sector calls `openTheoreticalBestSector(segmentId)`. It
loads the donor lap as comparison A and the actual best as B, opens the
comparison view, and sets `comparisonFocusSegmentId`. The Corner Analyzer
opens and selects that segment once the pair's segments load, then clears the
request. Closing the comparison view also clears it.

The Corner Analyzer normally requires both laps' own runs to have the same
approved revision (KAN-55). Segments are often approved on only one run, so a
donor and the actual best can both come from a run without approved segments.
When the view is opened from a theoretical-best sector, and both laps are in
the canonical segmentation's group, the Corner Analyzer uses the canonical
run's approved segments. These are the same segments the theoretical best
used. `comparisonSegmentationNote()` names that run and notes that boundaries
are distances along its axis. This fallback is removed when the view closes.

`TelemetryTests::opensTheoreticalBestDonorFromAnotherRun` imports the route
twice, with the second recording uniformly 10% faster, and approves segments
only on the slower run. Every donor and the actual best then come from the
faster run. The test checks that losses and the difference are non-negative,
that an unknown sector does not open, that the Corner Analyzer shows the
canonical segments with the note and timed A/B sector values, and that closing
the view removes the fallback. `TelemetryTests::opensTheoreticalBestSectorThroughQml`
opens the dialog, checks the actual-best label and the algorithm and
achievability text, activates a sector row with the keyboard and checks that
the dialog closes, the Corner Analyzer selects that segment and no QML warnings
are logged. Synthetic only.

## M3 acceptance workflow (KAN-58)

`TelemetryTests::acceptsM3SegmentationCornerAndTheoreticalBestWorkflow` runs
proposal, review, correction, save, reopen, corner comparison and donor-sector
navigation on a known-time fixture (`warpedRouteVbo`: two runs lapping in
exactly 48 s with opposite quick halves). It also checks cache invalidation
after a segment split, a missing speed sensor and a layout change. The
acceptance matrix, measured values and outstanding real-track feedback are in
`docs/kan58-m3-acceptance.md`. Synthetic only.

## Time-loss observations (KAN-59)

`computeTimeLossObservations` (`native/src/telemetry/TimeLoss.h/.cpp`, tag
`time-loss-windows-v1`) makes one loss window per approved segment for an
A/B pair, from both laps' `LapSectorTimes` on one shared axis:

- **Increment**: A's time through the window minus B's. This is exactly how
  much the A-minus-B delta changes across the window. Positive means A lost
  time there, negative means A gained.
- **Cumulative**: the running A-minus-B delta at the window's entry and exit
  (crossing time minus each lap's timed start), reported separately. A
  window can have a negative increment while the cumulative delta is still
  positive.
- **No double counting**: approved segments never overlap, so a stretch of
  track belongs to at most one window. Unsegmented stretches belong to none.
  With a complete partition and every window timed, the increments sum to
  the lap-time difference.
- **Corner vs continuation**: a straight that starts where a corner ends
  (within 0.5 m, including a corner ending at the gate) has the role
  `continuation` and names the corner. A loss carried onto the straight
  stays out of the corner's window. Other straights are `straight`, sectors
  are `sector`.
- A window where either lap has no complete time (coverage gap,
  gate-crossing segment) is `untimed` and is never bridged. Laps timed
  against another revision or configuration are rejected.

`AppController::comparisonTimeLossObservations()` returns these for the
current comparison pair. It uses the Corner Analyzer's segmentation,
including the canonical fallback when the pair was opened from a
theoretical-best sector (KAN-57). Ranking and UI are KAN-60 and KAN-61.

`TimeLossTests` builds laps from explicit time-at-progress functions. It
checks exact increments, continuity of the running delta, a sum equal to the
lap delta, continuation roles (including at the gate), a lap that gains in a
window while still behind overall, a gap leaving one window untimed, and
rejection of another revision.
`TelemetryTests::derivesTimeLossObservationsForComparisonPair` compares laps
from the two opposite-quick-half runs of the KAN-58 fixture. It checks that
windows both lose and gain more than 0.5 s, that increments sum to the lap
delta within 10 ms, and that windows are in order, do not overlap and have a
continuous running delta. Synthetic only.

## Ranked time losses (KAN-60)

`rankTimeLosses` (`TimeLoss.h/.cpp`) compares every lap with a reference lap,
one KAN-59 window per approved segment. Each positive increment is one
observed loss. Gains, zero increments and the reference lap itself are left
out. Windows without full coverage on either lap are counted
(`untimedWindowCount`) but not ranked. Losses are sorted largest first. Ties
are broken by track position, then by lap start. At most 50 are returned,
and `observationCount` is the total before truncation.

Day results → **Time losses…** (`TimeLossDialog.qml`) shows
`AppController::outingTimeLossRanking`. It uses the same background
calculation as the theoretical best: every eligible lap of the comparison
group is timed on the canonical axis against the group's best lap. Each row
shows the loss, the segment and its role (a continuation names its corner),
the compared lap and the window's coverage on both laps. The header names
the reference lap and the counts. The method text says that an observed
loss is not a guaranteed or necessarily safe gain. Exclusions, a new best
lap, edited segments or document changes invalidate the result, and an open
dialog (either one) recalculates.

`TimeLossTests::ranksLossesAcrossLapsAgainstReference` checks the order, ties,
the omission of gains and of the reference lap, untimed windows, truncation
and a missing reference. `TelemetryTests::ranksTimeLossesAndRecalculatesOnExclusion`
opens the dialog on the KAN-58 two-run fixture and checks the ranked rows,
the reference label and the disclaimer. It then excludes the top loss's lap
and checks that the open dialog recalculates without it, with no QML
warnings. Synthetic only.

## From a ranked loss to corner evidence (KAN-61)

Selecting a row in **Time losses…** calls `openTimeLoss(loss)`. The loss's
lap becomes comparison A and the ranking's reference lap becomes B. The
comparison view opens with the canonical segments the ranking used, focused
on the loss window. The Corner Analyzer selects that segment and sets the
shared zoom to the window, so the delta, map and channel charts show the same
range when switching to **← Channels**.

In the Corner Analyzer, **Lap A here…** and **Lap B here…**
(`openComparisonLapAtProgress`) open that lap in the lap view. The cursor is
placed where the lap reaches the segment start on the comparison axis. The
lap view's video follows the cursor through the run's existing
synchronization (KAN-39) when the lap belongs to the loaded run. Otherwise
the video pane shows that no video is available and the lap still opens. If
the lap has no projected time at that point, it opens at its start. Missing
channels only affect the metrics shown and never block navigation.

Returning keeps the ranking context. **← All laps** in the lap view goes
back to the comparison. Closing the comparison reopens **Time losses…** with
the same loss selected, matched by lap and segment because a recalculation
can reorder the list.

`TelemetryTests::navigatesFromRankedLossToCornerEvidence` hosts
`OutingLapPanel` and `ComparisonDetailPanel` and drives the flow with the
keyboard: open the ranking, select a loss, check A, B, the selected segment,
the zoom range and the cleared focus request, then open lap A at the window
(no video in the fixture, which does not block it) with the cursor inside
the lap. Finally it goes back twice and checks that the ranking reopens with
the same row selected, with no QML warnings. Synthetic only; no video-bearing
fixture exercises the seek.

## Corner Analyzer on real recordings (KAN-117)

Changes made after the first owner test with the real Jastrząb day:

- A theoretical-best sector whose donor lap is also the actual best no
  longer compares that lap with itself. B is the next-fastest lap through
  the sector.
- The Corner Analyzer is a column beside the track map and the delta/channel
  charts, sharing their zoom. The map highlights the zoomed stretch on lap
  B's trace, and the segment list scrolls to the selected segment.
- Every segment shows the measured entry, top, lowest and exit speed from the
  recorded speed channel. They are never derived from GPS, and are
  unavailable without a speed channel or when the segment crosses the gate.
  Corners keep their KAN-52 entry/apex/minimum/exit rows.
- Default comparison channels resolve the speed/throttle/brake aliases to
  the recording's own names (`velocity`, `throttle_pos-obd`,
  `brake_pos-obd`) through `comparisonPreferredChannels()`.
- The theoretical best and the loss ranking are invalidated only when their
  inputs change (group, eligible laps, exclusions, segments, track
  configuration, best lap). Opening evidence, which persists the comparison
  pair, no longer discards them.
- The loss ranking compares each run's best lap with the day's best by
  default. **Include every eligible lap** restores the full list, where
  warm-up laps otherwise dominated the top of the ranking.
- The canonical axis is built from the canonical run's fastest lap (the lap
  usually reviewed). Before this change, the best lap on Jastrząb had no
  projected time through one corner.
- Lap and segment times of a minute or more display as `m:ss.mmm`
  (`FlappedEar::formatElapsedTime` in `telemetry/LapTiming`, shared by the editor and the analysis).

The VBO parser still records no channel units: RaceChrono declares them in
`[header]`, but channel units are part of the recording fingerprint, so
adding them would ask every saved project to relink its recordings. Speeds
therefore show without a unit.

Opt-in real-day check, up to commit `7eae6cd` (recordings stay out of Git;
screenshots go to a local directory):

```bash
FLAPPEDEAR_REAL_DAY="$PWD/jastrzab" FLAPPEDEAR_CORNER_REVIEW_DIR=/tmp/review \
  ./build-native/native/tests/flappedear_native_tests analyzesPrivateTrackDayCorners
```

It reviews the best lap's run, prints the proposals, theoretical best,
ranked losses and per-segment metrics, and checks that no sector opens a lap
against itself. On the 29 August 2026 Jastrząb day it reported 15 proposals
(including `Corners 9–16`, 504 m), a theoretical best of 1:47.900 against a
1:49.898 actual best covering the whole lap, and speeds for every segment.
`TelemetryTests::formatsElapsedTimes` covers the time formatter.

## Pedal as the throttle input (KAN-118)

`preferAcceleratorPedalForThrottle` (`TelemetrySession.h`) runs after
both the VBO and the RCZ parser. `RczTests::prefersAcceleratorPedalForThrottle`
covers: a VBO with plate and pedal, where `throttle` reads the pedal (0 %
during a 72 % plate blip) and the plate stays readable by name; a
plate-only VBO, which is unchanged; a pedal column without numeric values,
which is ignored; an RCZ with OBD channels 10025 (plate) and 10071 (pedal);
and a plate-only RCZ. On the private Jastrząb day
(`analyzesPrivateTrackDayCorners`), throttle pickup went from undetected on
every corner (the plate never falls below the 10 % off threshold) to
measured on all of them.

## Where the best lap can improve (KAN-120)

Two changes after owner testing on the real Jastrząb day:

- **Gate-crossing segments are timed.** A segment whose end lies past the
  start/finish line (the last proposal of a lap usually does) used to be
  `crossesGate` and untimed, so the theoretical best, the actual best's sum
  and the difference were all withheld. `computeLapSectorTimes` now times it
  within the lap as (lap end − its start crossing) + (its end crossing − lap
  start). Both parts must be fully covered, and a gap is never bridged. The
  two parts count toward `completePartition`. In a time-loss window that
  crosses the gate, the exit running delta is the entry value plus the
  increment. Corner speed and braking metrics for such a segment remain
  unavailable (`crossesGate`).
- **The window leads with the best lap.** **Theoretical best…** shows your
  best lap → theoretical best → time available, then a track map. The map is
  the canonical axis, north up; each approved segment is drawn in one
  sequential hue by the time your best lap loses there, with a legend. Next
  to it is a list sorted by gain that names the fastest lap in each segment.
  Hovering a row or clicking the map selects a segment. Clicking a row or
  double-clicking the map opens that lap against your best lap in the
  Corner Analyzer. `outingTheoreticalBest` adds `gains` (sorted) and `map`
  (normalised segment polylines).

`SectorTimingTests::gateCrossingAndGappedPartitionsAreNotComplete` now
checks a gate-crossing sector timed within the lap (10 s on the 20 m/s
fixture), a complete partition, and that a hole in either part leaves it
untimed. `TelemetryTests::timesApprovedSectorsForTheOpenLap` checks that the
unsplit set already sums to the lap time. On the private day (approving
every proposal without splitting), the result is 1:47.905 theoretical
against a 1:49.898 best, with 1.993 s available.

## Timing consistency (KAN-62)

`summarizeConsistency` (`native/src/telemetry/Consistency.h/.cpp`, tag
`consistency-iqr-v1`) reports, for a set of times:

- the sample count;
- **typical** = median;
- **spread** = interquartile range (Q3 − Q1, in seconds): the width of the
  middle half of the laps. It ignores the quickest and slowest quarter, so a
  warm-up or traffic lap does not dominate it. No percentage score is used.
- minimum, Q1, Q3 and maximum.

Quantiles interpolate linearly between ordered samples, the same definition
as the ranking's lap distributions. Fewer than **3** finite samples
(`minimumConsistencySamples`) is `tooFewSamples`, with the count but no
statistics.

- **Laps:** `AppController::outingLapConsistency` uses `eligibleOutingLaps`,
  the ranking's own eligibility (compatibility group, exclusions, GPS issues,
  stale sources), for the whole day and per run. It is updated with the
  outing laps.
- **Sectors:** each segment in `outingTheoreticalBest` carries `consistency`,
  computed from the laps the theoretical best timed on the canonical axis.
  Laps without a time in that segment are left out.
- **Theoretical best…** shows the day's lap consistency in its headline and
  each segment's typical time and spread in the list.

On the private Jastrząb day the whole-day spread is 24.5 s (23 laps). The
day's progression from about 2:23 in the morning to 1:54 in the afternoon
dominates it, so per-run statistics (a spread of 1.7–9 s) are the meaningful
driver measure; KAN-64 presents them as progression.

`ConsistencyTests` covers exact quantiles (including interpolation), the
minimum, non-finite samples, robustness to one slow lap, and per-sector
filtering (timed laps only, other revisions ignored).
`TelemetryTests::reportsLapAndSectorConsistency` uses the two-run fixture.
Laps all take 48 s, so the lap spread is about 0 and the count equals the
ranking's eligible laps. Sectors show spread. After excluding laps down to 2,
lap consistency is unavailable.

## Braking, apex, exit and line variability (KAN-63)

The theoretical-best worker computes, for every eligible lap and every
approved corner, the same Corner Analyzer metrics on the canonical axis:
braking point (KAN-53), apex, minimum and exit speed (KAN-52) and throttle
pickup (KAN-54). It also computes the lap's **lateral offset** from the
reference line at the corner's geometric apex (mid-corner for a chain with
several apexes), and the recording's own `accuracy` value at that point.
`summarizeCornerVariability` (`DrivingVariability.h/.cpp`, tag
`driving-variability-v1`) summarizes each metric with the KAN-62 statistics
(median, IQR, count, minimum 3):

- Measured and inferred braking points and throttle pickups are summarized
  separately and never mixed.
- Speeds come only from the recorded speed channel, in its units (VBO units
  are not recorded; see KAN-117).
- The line spread (IQR of the lateral offset, metres) is shown next to the
  typical (median) GPS accuracy. It counts as resolvable only when it
  exceeds that accuracy. Without a stated accuracy it is never claimed
  resolvable.

**Theoretical best…** shows the selected corner's variability ("lap to lap")
in a box on the map.

Real-data correction to KAN-53 (tag `braking-metrics-v2`): the 200 m braking
approach now stops at the end of the previous approved corner
(`approachClippedAtPreviousCorner`). On the Jastrząb day, Corner 7 follows an
82 m straight, so the approach reached into Corners 5–6 and picked up their
braking on some laps. Its braking-point spread fell from 170.5 m to 8.0 m.

`DrivingVariabilityTests` covers per-metric counts, measured and inferred
kept apart, line spread against GPS accuracy (resolvable, noisy, unknown)
and signed lateral offset. `BrakingMetricsTests::stopsTheApproachAtThePreviousCorner`
covers the clip, braking after the previous corner, and an intervening
straight that does not clip. `TelemetryTests::reportsCornerVariabilityWithGpsLimits`
covers identical paths: line spread about 0 but not resolvable without
accuracy, and no speed or braking samples. On the private day, GPS accuracy
is about 0.15 m and line spreads are 0.7–8.1 m. The gate-crossing final chain
has line data only, because its speed and braking metrics are unavailable.

## Sector progression between sessions (KAN-64)

Day results → **Progression…** has two tabs: **Laps** (the KAN-25 per-run
distributions) and **By section** (`SectionProgressionView.qml`). In
**By section**, rows are the approved sections in track order and columns
are sessions in the progression's chronological order. Each column header
shows the session's lap typical time and spread, conditions and setup, with
notes in a tooltip.

Each cell shows that session's **typical** time (median) and **spread**
(interquartile range, seconds) in the section, with the lap count; fewer than
3 laps shows no statistics. The cell colour is relative within the row: green
is the session with the quickest typical time, and the brighter the orange,
the slower. Together, typical time and spread distinguish fast/variable from
slower/repeatable. Each cell is a button: it lists the laps behind the
figure, quickest first, and choosing a lap opens it.

`AppController::outingSectorProgression` builds this from the laps the
theoretical best timed on the canonical axis. It uses the same eligibility,
invalidation and minimum as KAN-62.

`TelemetryTests::showsSectionProgressionBetweenSessions` opens the dialog on
the two-run fixture and switches to **By section**. It checks two sessions in
order, one row per approved section, cell lap lists that match their counts,
and sessions whose typical times differ. It then opens a cell with the
keyboard and chooses its quickest lap, which opens, with no QML warnings.
On the private Jastrząb day every section's typical time improves from the
morning sessions to the afternoon. For example, Corners 2–3 go from 11.9 s
(spread 2.7 s) to 8.9 s (0.3 s).
## Timed G-G sample pairs (KAN-65)

`buildGgPairs` (`native/src/telemetry/GgPairs.h/.cpp`, tag `gg-pairs-v1`)
pairs the `longitudinalAcceleration` and `lateralAcceleration` aliases over a
time range:

- **Clock:** the longitudinal channel's samples are the clock. A lateral
  sample at the same time is used as is (`sharedClock`). Otherwise the
  lateral value is interpolated between the two lateral samples around it,
  but only when they are no further apart than the lateral channel's gap
  threshold. Gaps are never bridged, a sample without a lateral value
  yields no point (`skippedForGap`), and there is no extrapolation at the
  ends. The largest pairing offset is reported.
- **Signs (as recorded, verified on the owner's recordings):** longitudinal
  is + when accelerating and − when braking (median −0.59 g with the brake
  pressed). Lateral is + toward the left (left turns +0.54 g, right turns
  −0.53 g).
- **Units:** declared `g` is used as is, and m/s² (`m/s2`, `m/s^2`, `m/s²`)
  is converted. An undeclared unit is kept and reported (`unitsDeclared`;
  VBO units are not recorded, see KAN-117). Any other unit is
  `unsupportedUnit`, with no points.
- **Outliers:** a value beyond ±4 g is excluded and counted, never clipped.
  A missing axis is `missingLongitudinalAcceleration` or
  `missingLateralAcceleration`, with no points.
- **Provenance:** the channel names are reported. On RaceChrono VBO these are
  the calculated `longacc-calc`/`latacc-calc`; the raw `longacc`/`latacc`
  columns are constant placeholders.

`GgPairsTests` covers the shared clock with signs and range, interpolation
on an offset clock that is exact on a linear signal and never crosses a
0.5 s hole, unit conversion, undeclared and unsupported units, and outliers,
non-finite samples and missing axes. On the private Jastrząb day, the best
lap's 1,418 samples all pair on a shared clock with no gaps or outliers.

## A/B G-G scatter and observed peaks (KAN-66)

In the comparison view, **G-G** (`ComparisonGgPanel.qml`) opens a side
column next to the map and charts; only one side column shows at a time. It
plots both laps' KAN-65 pairs over the shared zoom window: the whole lap, or
the segment selected in the Corner Analyzer.

- **Axes:** lateral runs left–right as the driver feels it (left on the
  left); longitudinal points up for accelerating and down for braking. Rings
  mark every 0.5 g. Lap A is green, lap B orange, as elsewhere.
- **Peaks:** `computeGgPeaks` reports the peak lateral (largest |lateral|),
  peak braking (largest deceleration, as a positive value), peak
  acceleration and peak combined (largest magnitude), each with its point.
  Valid sample counts are shown for both laps. Peaks are always calculated
  from **every** pair. `decimateGgPoints` only thins what is drawn (at most
  1,500 points per lap) and always keeps the peak points.
- The panel names the source channels, says when units are undeclared, and
  states that these are observed accelerations, not a share of available
  grip. No percentage is shown.
- `AppController::comparisonGgScatter(start, end, maximumPoints)` is only
  evaluated while the panel is visible.

`GgPairsTests::computesPeaksIndependentlyOfDecimation` checks the peak
values and points on 5,000 points, that a 300-point decimation keeps
identical peaks, and that no pairs means no peaks.
`TelemetryTests::showsAbGgScatterWithPeaks` uses
`routeVboWithAccelerations`, whose peaks are known, with session 2 at 90 %:
lateral 1.00 against 0.90 g, braking 0.50 against 0.45 g. It checks that
drawn points are capped while the sample count is not, that dense and
decimated peaks are identical, that a quarter lap has fewer samples, and
that the panel shows the peaks with no QML warnings. On the private Jastrząb
day, the afternoon best lap (Session 5) reaches 0.98 g lateral, 0.87 g
braking and 1.02 g combined; a morning lap peaks around 0.6 g.
## Telemetry core boundary (KAN-123)

`flappedear_telemetry_core` links Qt Core and zlib only. Every pure test
target (`rcz`, `import`, `source_cache`, `lap_eligibility`, `track_*`,
`sector_timing`, `theoretical_best`, `time_loss`, `consistency`,
`driving_variability`, `braking_metrics`, `exit_metrics`, `corner_phase`,
`braking_onset`) links only that library. They therefore prove the analysis
code builds and runs without Gui.

`flappedear_telemetry_core_boundary` and `flappedear_telemetry_app_boundary`
run `native/tests/CheckLibraryBoundary.cmake` (KAN-154). It starts from the
library's own `SOURCES`, written at generate time, and follows every quoted
include the way the compiler resolves it: the including file's folder first,
then `native/src`. Each project file reached must be in the library's
`SOURCES` or in the `SOURCES` of a library it links (the allowlist), so a
relative include such as `"../export/X.h"` or one reached through another
header fails. A Qt Gui, Qml, Quick, Multimedia, Widgets, QProcess or QRhi
header fails anywhere along the way. Two must-fail fixtures in
`native/tests/fixtures/boundary/` keep the check honest:
`flappedear_boundary_rejects_relative-transitive` (a `../export/` include two
headers deep) and `flappedear_boundary_rejects_gui-header` (`<QPainter>`).
The first run found `telemetry/TelemetrySessionCache.h` missing from the
core's source list; it is listed now.

`EventProjectTests::acceptsAnalysisOnlyEventDocuments` checks that a v3
event document without `scene`, `exportSettings` and `mapSettings`
validates. A malformed scene still fails, and a v2 editor project still
needs its scene. The private real-day figures are unchanged by the split
(1:47.905 theoretical against 1:49.898).

## Recorded temperature summaries (KAN-67)

`summarizeChannel` (`native/src/telemetry/ChannelSummary.h/.cpp`, tag
`channel-summary-v1`) summarizes one recorded channel over an interval:

- **Mean:** time-weighted. Consecutive valid samples closer than the
  channel's gap threshold are joined linearly, and the mean is that line's
  integral over the covered time. A gap is never bridged.
- **Extrema:** minimum and maximum, with the time of each.
- **Coverage:** covered time as a fraction of the interval.
- **Units** as declared (VBO units are not recorded; see KAN-117), and the
  source channel name.
- **Artifact policy**, always counted in `excludedArtifacts`, never silently
  dropped:
  - temperatures outside −40…250 °C are excluded;
  - an exact 0 is excluded as an OBD placeholder when the channel's median
    is above 20 (a channel that really sits near 0 keeps its zeros);
  - an excluded sample also breaks the joined line.

`recordedTemperatureChannels` lists only the recording's own channels whose
name contains "temp". An absent sensor is not listed, and no thresholds are
invented. `AppController::requestOutingChannelSummaries()` runs a background
worker that decodes each run once. It is independent of segments and of the
comparison group, because vehicle health applies to every run.
`outingChannelSummaries` reports, per run and per temperature channel, the
whole-recording summary and one summary per recorded section (OUT, laps,
IN), giving run and lap coverage.

`ChannelSummaryTests` covers:
- an exact time-weighted mean on a linear ramp, including a sub-interval;
- a 4 s gap that is not bridged (coverage 0.6, mean of the covered parts);
- placeholder zeros and a 900 °C glitch excluded and counted, while zeros
  in a near-zero channel are kept;
- heart-rate plausibility;
- a missing sensor and an empty interval.

`TelemetryTests::summarizesRecordedTemperaturesPerRunAndSection` imports one
run with a coolant channel (three placeholder zeros, then a rising value) and
one without. It checks that the run without a sensor lists none, that the
three artifacts are excluded, the coverage, and that section means rise.

On the private Jastrząb day: 6 sessions × 4 channels (coolant, oil, gearbox,
intake). Oil peaks at 108 °C in Session 1 and 128 °C in Sessions 5–6, and
gearbox at 113 °C. OBD dropouts in Sessions 3–4 (up to 582 zero samples)
are excluded with coverage 0.95–0.98. KAN-68 shows the trends.

## Heart-rate summaries (KAN-69)

Heart rate comes only from the imported recording's own heart-rate channel
(the `heartRate` alias, e.g. RaceChrono `heart_rate-hrm`); there is no
separate importer. It uses the KAN-67 `summarizeChannel` with
`heartRateSummaryPolicy()`:

- **Mean:** time-weighted, joining samples only within the channel's gap
  threshold. A strap that updates about twice a second while the logger
  repeats the value at 10 Hz is therefore weighted by time, not by rows.
- **Extrema** with their times, and **coverage**.
- **Artifact policy:** values outside 30…230 bpm are excluded and counted.
  There is no zero-placeholder rule for heart rate.
- Heart rate is reported as a measurement only, never labelled as stress,
  effort or confidence.

Summaries exist per run and per recorded section (in `outingChannelSummaries`,
under `heartRate`). They also exist for a selected interval of the
comparison pair (`comparisonHeartRate(startMeters, endMeters)` on the shared
axis), for example a sector. A recording without heart rate has no summary.

`TelemetryTests::summarizesHeartRatePerRunSectionAndInterval` imports two
runs with held heart-rate values (140 and 150 bpm, each with one 255 bpm
artifact) and one without. It checks the per-run means, the one excluded
artifact, coverage, section summaries, the absence of a summary for the
third run, and both laps' means over half a lap. On the private Jastrząb
day, session means rise from about 119–123 bpm in the morning to 133–136 bpm
in the afternoon, with a maximum of 158 bpm and complete coverage.

## Thermal trends and cooling (KAN-68)

`findCoolingIntervals` (`ChannelSummary.h/.cpp`) finds stretches where a
recorded temperature falls continuously:

- The channel is split into continuous stretches. A stretch breaks at a gap
  longer than the channel's gap threshold or at an excluded artifact sample,
  so a cooling interval never spans missing data and nothing is inferred
  across a break.
- Each stretch is smoothed with a centred 5 s moving average, then walked
  from peak to trough. An interval closes when the value rises at least
  1° above the trough, or when the stretch ends. The trough is the first
  point of a flat bottom: holding a temperature does not lengthen the
  cooling.
- An interval is reported when it drops at least 5° over at least 30 s
  (`CoolingOptions`), with start/end time and value.

`outingChannelSummaries` adds, per run and temperature channel, a `trace` of
120 bins, each summarized on its own (`null` for a bin with no valid samples),
and a `cooling` list naming the recorded section each interval started in.
The heart-rate entry has the same trace, for KAN-70.

Progression → **Car & driver** (`CarDriverView.qml`) requests the summaries
when shown. It draws one chart per recorded channel: every session is its own
slot on a shared scale, the line breaks at `null` bins, and cooling intervals
are shaded blue. Below the chart is a per-session table of mean,
minimum–maximum, coverage and cooling (for example "−10 in 3:48 (lap 5)"). Undeclared units are
labelled as such, and no °C is assumed. A session without the sensor reads
"Not recorded". Cooling intervals are continuous by construction (full coverage),
so each shows its actual duration.

Tests:
- `ChannelSummaryTests::findsContinuouslyRecordedCoolingOnly`: one interval
  of about −20° over about 120 s, starting near the peak, with a flat tail
  that does not extend it. A 60 s recording gap is never spanned, and ±0.5°
  noise is not cooling.
- `TelemetryTests::summarizesRecordedTemperaturesPerRunAndSection`: the trace
  has 120 bins in recording order, and a rising coolant has no cooling.
- `TelemetryTests::showsRecordedTemperaturesThroughTheDayInQml`: one chart
  for the one recorded channel, and table cells matching the controller,
  including "Not recorded" for the run without a sensor. There are no QML
  warnings.

On the private Jastrząb day (screenshot from `analyzesPrivateTrackDayCorners`):
- Oil climbs to 124–128 in Sessions 3–6.
- The cool-down laps show as oil cooling of about 10° over 2–4 minutes in
  Sessions 4–6 (lap 5, lap 5, lap 3).
- Intake air falls 20–35° on the out lap as heat soak clears.
- Coolant holds 90–103 with small recorded dips.

## Heart-rate comparisons by run and segment (KAN-70)

Heart rate is shown as measured, from the recording's own channel, and never
worded as stress, fitness or confidence.

- **Progression → Car & driver** has a heart-rate card:
  - A day trend (`DayTrendChart.qml`, shared with the temperature charts):
    sessions as separate slots, holes at recording gaps.
  - Per session: mean, minimum–maximum, sample count, coverage, and excluded
    implausible values. A session without heart rate reads "Not recorded".
  - Per recorded section, a chip with the mean (and coverage below 95%).
    Selecting a chip calls `openOutingLapChannel(reference, channel)`, which
    opens that lap with the heart-rate channel first in its charts. The saved
    chart preference (`analysis/lapChannels`) is not rewritten, and a lap
    opened normally afterwards does not inherit the request.
- **Corner Analyzer** has an A/B heart-rate row for the selected segment:
  - Mean per lap and Δ, with samples and coverage per lap underneath.
  - ♥ puts the recorded heart-rate channel into the comparison charts,
    replacing the last one when four are shown.
  - A segment that crosses start/finish (end before start, typically the
    last corner chain) is summarized as the lap's end plus its beginning.
    `combineChannelSummaries` merges the two parts: the mean is weighted by
    covered time, extrema are the extremes of both parts, and coverage is
    measured over both lengths.

Tests:
- `ChannelSummaryTests::combinesDisjointIntervals`: an exact weighted mean
  across two parts; an empty part lowers coverage without inventing values;
  a channel missing in every part stays missing.
- `TelemetryTests::summarizesHeartRatePerRunSectionAndInterval`: a range
  across start/finish keeps each lap's level (140/150 bpm) and its coverage.
- `TelemetryTests::showsHeartRateByRunAndSegmentInQml`:
  - production QML, no warnings;
  - run rows (mean, samples, coverage, one excluded artifact each,
    "Not recorded");
  - a lap chip opening the lap with `heart_rate` first, with the preference
    untouched;
  - the Corner Analyzer row, opened as theoretical-best evidence, showing
    about 140/150 bpm, and ♥ adding the channel to the charts.

On the private Jastrząb day, per-lap means rise from 112–131 bpm (Sessions
1–2) to 139–153 bpm (Sessions 4–5). For Session 2 LAP 1 against Session 5
LAP 2 on "Corners 9–16", which crosses start/finish, the Corner Analyzer shows
123 against 154 bpm (429 and 317 samples, 99% and 100% covered).

## Computed day report (KAN-71)

`telemetry/DayReport.h/.cpp` (`day-report-v1`, schema
`flappedear.day-report` version 1) is the report model. Screens, the desktop
now and the phone app later, present it and never recalculate:

- `buildDayReport` assembles results that were already computed. Each result
  has:
  - an `id`;
  - its producing `algorithm` (plus a `revision` where the producer has one,
    such as the time-loss stamp);
  - a `range`: scope, group, and counts such as eligible laps, compared laps
    and runs;
  - a `status`: `available`, `unavailable`, `notComputed`, `computing` or
    `stale`;
  - a `value` only when available;
  - `evidence` a screen can open: `lap` (a lap reference), `segment` (a
    segment of a lap, with the reference lap for losses), `channel` (a run's
    recorded channel) or `run`.
- The report carries the **decisions key**: a hash of the group, approved
  segments, track configuration, exclusions and population, the same key
  the theoretical best uses. A result computed under another key is reported
  `stale`, and its value and evidence are dropped. The controller also
  resets the underlying results when decisions change, so this is a second
  line of defence. Channel summaries do not depend on the decisions (they
  are invalidated with the run set), so they carry no key.
- `validateDayReport` checks a report read from elsewhere: schema, version,
  unique ids, algorithm, known status and evidence kinds, and value only when
  available. Results and evidence are bounded (64 and 4096) before it
  iterates.

`AppController::outingDayReport` (cached, rebuilt when laps, the theoretical
best, the channel summaries or the document change) contains these results:
- `bestLap`
- `progression`
- `consistency` (evidence is the eligible laps)
- `theoreticalBest` (evidence is each sector's source lap and segment)
- `timeLosses` (top 10)
- `sectionProgression`
- `temperatures`
- `heartRate`

`requestOutingDayReport()` starts the background results it needs.

Tests:
- `DayReportTests`: provenance on every result; a value computed under other
  decisions is `stale` with no value or evidence, while a decision-independent
  result is not; malformed input is rejected (duplicate id, missing
  algorithm, evidence without a kind); read reports are validated for
  version, results type, value/status mismatch, unknown status and evidence
  kinds, and the result and evidence bounds.
- `TelemetryTests::buildsDayReportWithProvenance`, two runs with one heart-rate
  channel:
  - before the workers run, the ranking results are available and resolve
    their lap evidence, while the rest are `notComputed` with no value;
  - after `requestOutingDayReport`, every result is available or says why
    not: temperatures are "No temperature recorded", and heart rate has
    exactly one channel of evidence;
  - excluding the best lap changes the decisions key, so the theoretical
    best no longer shows its old value and the best lap moves.

On the private Jastrząb day, all eight results are available:
- best lap: 1 evidence item;
- progression: 6 sessions;
- consistency: 23 eligible laps;
- theoretical best: 15 evidence items;
- time losses: 10;
- sections by session: 84 segment × session cells;
- temperatures: 24 channels;
- heart rate: 6 channels.

The report is 93 KB of JSON. The report screen is KAN-72.

## Day report screen (KAN-72)

**Day results → Day report…** (`DayReportDialog.qml`) presents
`outingDayReport` and never recalculates. Opening it calls
`requestOutingDayReport()`. It uses one vertical scroll with touch-sized
rows and no information that needs hover. Every card leads to its evidence:

- **Best lap and what is left:**
  - The best lap and the theoretical best, with the time available across
    the approved sectors.
  - "Open best lap" opens the lap.
  - "Where it can improve (map)…" opens the theoretical-best map.
- **Largest time losses:**
  - The top five, each with the lap it happened on.
  - Selecting one opens the comparison against the group's best, with the
    Corner Analyzer at that segment (`openTimeLoss`). Closing the comparison
    returns to the report.
  - "All losses…" opens the full ranking.
- **Sessions:**
  - Best and median per session, as "x s faster/slower than the previous
    session".
  - Selecting a session opens its best lap.
  - "Sections by session…" opens that Progression tab.
- **Consistency:** typical lap and the spread of the middle half, or why
  there is none (fewer than three laps).
- **Car:** the peak of each recorded temperature and the session it came
  from, plus the count of recorded cooling intervals. Selecting a row opens
  Progression → Car & driver.
- **Heart rate:** mean and range per session, "Not recorded" where the
  recording has none. Selecting a row opens Car & driver.

A card without a result shows the report's reason instead of a value:
- "Calculating…" while it is computing;
- "Not calculated yet." when it has not been requested;
- the producer's own reason when it is unavailable (for example "No
  temperature recorded.");
- the reason for a result that is out of date after an analysis change.

A missing value is never shown as zero, and nothing depends on video.

`TelemetryTests::presentsDayReportWithEvidenceNavigation` uses production
`OutingLapPanel` at the 1180×720 minimum, with two runs, one of which has
heart rate. It checks:
- the report opens from Day results;
- the best time and the theoretical best fill in;
- the Car card says "No temperature recorded.";
- the heart-rate rows read "Not recorded" and "mean 140 bpm";
- the first loss opens the comparison, and closing it reopens the report;
- a session opens exactly the best-lap reference in the report's evidence;
- there are no QML warnings.

On the private Jastrząb day (screenshots from `analyzesPrivateTrackDayCorners`):
- 1:49.898 against a 1:47.905 theoretical best, 1.993 s available;
- the top loss is +11.290 s in Corners 9–16 on Session 2 LAP 1;
- sessions improve from 2:18.655 to 1:49.898;
- peaks of 103 coolant, 128 oil, 113 gearbox and 75 intake;
- heart-rate means of 119–136 bpm.

Limitation: the report is reached from its button and does not open by
itself after an import.

## Areas to inspect next (KAN-73)

The definitions are in `docs/telemetry-semantics.md` ("Areas to inspect
next"). The day report carries the `focusAreas` result, whose evidence is a
lap pair and a segment. The Day report's **Where to look next** card shows
each area as "Observed: …" followed by "Hypothesis: …" in italics.
"Compare A with B at X" (`openFocusArea`) opens that pair in the comparison,
with the Corner Analyzer at that segment. The repeated-loss input is ranked
without the 50-result truncation used for display
(`computeTimeLossRanking`).

Tests:
- `FocusAreasTests`:
  - one of each kind first, one area per segment, the typical lap nearest
    the median;
  - repeats and thresholds required (two losses are no pattern; tiny gaps
    and spreads are no area);
  - inferred braking points never mixed with measured ones;
  - the braking hypothesis explicitly makes no earlier/later claim;
  - no advice or causal wording in any area;
  - at most three, deterministic ties, empty input gives no areas.
- `TelemetryTests::selectsFocusAreasFromComputedObservations`, two sessions of
  equal lap time, each quicker in a different half:
  - `notComputed` before the workers run, then a sector-gap area over 0.5 s;
  - resolvable, distinct evidence laps;
  - `openFocusArea` opens the comparison at that segment with the best lap
    as A;
  - the report renders the labelled observation and hypothesis, and the
    compare row opens the comparison;
  - no QML warnings.

On the private Jastrząb day the three areas are:
1. the best lap (Session 5 · LAP 2) 0.619 s slower through Corners 9–16 than
   Session 6 · LAP 3;
2. Corners 2–3 lost in 5 of 5 session bests (median 2.035 s);
3. the measured braking point for Corners 5–6 spread over 20.6 m across the
   middle half of 21 laps.

## M4 acceptance (KAN-74)

`TelemetryTests::acceptsM4ReportLossCornerEvidenceWorkflow` runs the M4
workflow end to end:
- full recordings (`fullM4Vbo`: heart rate, coolant, calculated G on
  known-time warped laps) and a GPS-only limited run;
- values checked against the generated source;
- absent heart rate, temperature and G on the limited run;
- an exclusion and a split segment;
- navigation without video: loss → Corner Analyzer, focus area → pair, best
  lap;
- Save As keeping the computed results;
- reopen with an identical report.

The private real-day test also saves and reopens the Jastrząb day and
requires the same report. The full record, including the Save As defect
found and fixed, is `docs/kan74-m4-acceptance.md`.

## Day analysis in telemetry core alone (KAN-124)

`OutingPipelineTests` links only `flappedear_telemetry_core`, as the
Telemetry app will. It derives the laps of two recordings with
`deriveOutingLaps` and checks:
- the lap references are valid, and both runs land in one compatibility
  group;
- `rankOutingLaps` gives a best lap;
- an unchanged run is reused, not derived again;
- `summarizeOutingChannels` invents no temperature or heart rate for a
  route without them;
- a deleted recording gives a `missing-source` message while the other run
  continues;
- a tampered fingerprint is refused;
- a cancelled derivation reports `cancelled` with no rows.

The run sources are built the way the project provides them: reference and
fingerprint, derivation digest, and the import's timing-gate revision.

**Core tests without the application.** `flappedear_telemetry_core_tests`
(`TelemetryCoreTests.cpp`) links only `flappedear_telemetry_core`, with no
Gui. It holds the tests moved out of `TelemetryTests.cpp`: VBO parsing and
its resource bounds and cancellation, time formats and monotonic
timestamps, coordinate evidence, timing gates and lap derivation, track
geometry, lap references, exclusions, route inference, import grouping and
the bounded JSON loader. The optional real-VBO tests there skip unless
their file is set, as before.

**Telemetry controllers without the editor.** `flappedear_telemetry_app_tests`
links only `flappedear_telemetry_app` (Qt Core, no Gui) and drives
`TelemetryController`:
- imports two recordings, derives laps and a comparison group;
- approves segments and computes the day report (best lap, theoretical
  best, consistency);
- saves, reopens in a fresh controller and gets the same report, with the
  document clean.

A second test opens a project that carries editor state (the active run's
sync and video, chart channels). It edits the run, saves it into another
folder, and checks that the state survives and the video and recording
still resolve. Without the reference rebase in
`TelemetryController::withEditorState` the video check fails.

**Refactor check on real data.** With `FLAPPEDEAR_REPORT_DUMP=/path/report.json`,
`analyzesPrivateTrackDayCorners` writes the day report with sorted keys.
Dump before and after a refactor, then normalise the per-import identities:
- UUIDs;
- 64-hex keys;
- `compatibility-v1:` ids;
- the time-loss revision, which hashes segment ids.

Diff the results. For KAN-124 step 6 (report assembly moved to core) the
1,400-line reports were identical apart from those identities. The same was
true when `AnalysisController` (step 8) and `DocumentController` (step 9)
were extracted from `AppController`. The regression tests reach their
internals through `controller.m_analysis` and `controller.m_document`.

## A complete day through move, relink and recovery (KAN-82)

`TelemetryTests::keepsACompleteDayThroughMoveRelinkAndRecovery` takes one
analysed day through its whole life. The day has two M4 recordings in the
project's `media/` folder, run notes, approved segments, an A/B pair from
the largest loss with a saved range, and the computed report. Each step
compares the notes, the report's decisions key, best lap, theoretical best
and sector count, the A/B lap references, and the saved range:

1. **Save.** The results settle again after Save As, because the lap key
   follows the project path.
2. **Move** the project folder with its recordings. On reopen it is clean
   and identical.
3. **Missing recording.** Rename the active run's recording.
   - On reopen that run shows `missing-source` and the other stays
     inspectable.
   - Relinking the other session's recording is refused by identity (a
     telemetry mismatch, declined).
   - Relinking the right file brings everything back. The day is dirty
     until saved, and after saving it is identical.
4. **Crash** with an unsaved note: the recovery file is written and the
   controller ends without saving.
   - Recover brings the note back, with the document dirty, and everything
     else identical.
   - Discard returns to the saved project, which is clean.

**Defect found and fixed.** After step 3's relink, lap A of the saved pair
did not come back until the project was reopened. The saved pair was
restored only once per document, and at open time lap A's recording was
missing. Now any empty slot with a saved reference is retried when the
day's laps change. The saved reference was never lost. Dirty New, Open and
Quit decisions keep their own tests (`gatesDirtyDestructiveActions`,
`resolvesDirtyDecisionsSafely`).

## Minimum layout and keyboard workflows (KAN-78)

`TelemetryTests::keepsAnalysisControlsReachableAtMinimumSize` opens the
production `AnalysisWindow.qml` on a synthetic analysed day. The day has
two full M4 recordings, approved segments and a computed day report. The
test runs at 1180×720, the editor minimum, and at 760×480, the Analysis
window's own minimum. It walks the workflow:
- the lap list;
- an open lap;
- segment review and an approved segment being edited;
- the comparison with the Corner Analyzer;
- the theoretical-best, time-loss and day-report dialogs;
- all three progression tabs.

In every state, each visible, enabled control (buttons, fields, pickers,
sliders) must have its centre inside the window and inside every clipping
parent. A control inside a scrolling area counts when it lies within the
area's scrollable content; the check respects a ListView's own origin. No
dialog may be larger than the window. `FLAPPEDEAR_LAYOUT_REVIEW_DIR`
receives screenshots.

A display that cannot show the requested size skips that size rather than
checking a smaller window. The hosted CI runner is one: at most 1180×656
fits there. So CI checks 760×480, and 1180×720 is checked on a Mac whose
screen fits it (recorded below).

**Keyboard.** Typing a lap-exclusion reason or a segment name and pressing
Escape keeps the lap open. Before this fix, the lap view's (and the
comparison's) window-wide Escape shortcut closed the view and discarded
the text. Both shortcuts are now disabled while a text field or text area
has focus. The Analysis window's transport shortcuts are already off while
a day is open, and are blocked in text and other controls.

**Fixed at 760×480:**
- **Comparison:** "Reset zoom", "Add channel" and the Corner Analyzer's
  metric buttons were off the right edge. The lap labels now elide, each
  chart's channel picker narrows so its × stays visible, and the Corner
  Analyzer scrolls vertically.
- **Segment review:** an open edit form spilled over the section slider
  below the panel. The panel is now one vertical scroll surface when it is
  shorter than its content.
- **Theoretical best:** the summary line widened the dialog's content, so
  the "Laps today" text, the "Where the time is" column and the algorithm
  note were clipped. The summary now wraps, and the column narrows.

At 1180×720 every state already passed, apart from the Escape defect. The
lap-exclusion reason now uses the app's text field style instead of the
platform's black box.

**Real day.** With `FLAPPEDEAR_REVIEW_SIZE=760x480` (or `1180x720`),
`analyzesPrivateTrackDayCorners` also checks reachability in each of its
screenshot states on the Jastrząb day: 6 sessions and 14 segments. Both
sizes pass on a Mac mini M4, macOS 27.0 (27 September 2026).

**Limitations at 760×480:**
- The theoretical-best map shrinks to its legend; its segment list scrolls.
- With four comparison channels, each chart row is about 50 px tall, so
  the channel pickers cover the charts' name labels.

Everything stays reachable.

## Full-day responsiveness and memory (KAN-77)

`TelemetryAppTests::measuresAPrivateFullDay` measures a real day through
`TelemetryController`, headless, with no editor and no Gui. It runs only
when `FLAPPEDEAR_REAL_DAY` names a directory of private VBO recordings:

```bash
FLAPPEDEAR_REAL_DAY=/path/to/day ./build-native/native/tests/flappedear_telemetry_app_tests measuresAPrivateFullDay
```

It prints the hardware, the dataset, each phase's time and the process's
resident and peak memory, and fails when a budget is exceeded.

**Measured (27 September 2026):**
- Machine: Mac mini (Mac16,10), Apple M4, 10 threads, macOS 27.0.
- Dataset: the owner's Jastrząb day, 6 VBO recordings (33.6 MiB, 25 timed
  laps), with 14 segments approved on the best lap.
- Three runs of each build. Times show the range; memory is the largest
  reading. Release is what users run; the Debug build is the local test
  build.

| Phase | Release | Debug | Resident / peak (Release) |
|---|---|---|---|
| Import (digest, grouping, event) | 1.76–1.82 s | 4.69–4.73 s | 85 / 89 MiB |
| Lap derivation, all runs | 1.44–1.45 s | 4.21–4.24 s | 103 / 116 MiB |
| Open a lap (verified detail) | 0.27 s | 0.80–0.81 s | 104 MiB |
| Segment review and approval | 0.13–0.14 s | 0.16 s | 104 MiB |
| Lap cursor, 1,000 steps (slowest step) | < 1 ms (< 0.01 ms) | 4 ms (0.01 ms) | 104 MiB |
| A/B pair selection (both laps loaded) | 0.60–0.61 s | 1.71–1.75 s | 96 MiB |
| A/B cursor, 1,000 steps (slowest step) | 2 ms (1.1 ms) | 23–24 ms (11.7 ms) | 96 MiB |
| A/B chart data (delta, speed, G-G) | 1 ms | 8–9 ms | 97 MiB |
| Day report (theoretical best, losses, summaries) | 2.4–2.9 s | 7.1–7.9 s | 150 MiB peak |
| Save | 6–7 ms | 6–8 ms | — |

A full day, from import to report, takes about 7 s in Release. The
slowest A/B cursor step is the first one: it builds the pair's shared
progress axis lazily. Every later step takes about 0.01 ms (Debug).

**Budgets** (enforced by the test). They hold in the Debug build at about
3–4× its measurements, and one frame where the user drags a cursor:
- every cursor step, lap or A/B: under 16 ms (one 60 Hz frame);
- open a lap: under 3 s;
- load an A/B pair: under 6 s;
- A/B chart data: under 250 ms;
- import to laps: under 30 s;
- day report: under 30 s;
- peak memory: under 512 MiB.

No budget was exceeded, so no fix was needed.

**Limitations:**
- QML painting in the editor's Analysis window is not in these numbers.
  Playback does not rebuild static geometry on each tick; that is covered
  by `cachesStaticTrackGeometry` (its QML guard,
  `keepsStaticTrackIndependentFromTime`, went with the Track widget in KAN-192).
- The combined editor process adds the Qt Quick scene and video on top of
  this baseline.
- Phone and tablet budgets (KAN-129) need measuring on the device; these
  desktop figures are the reference for that work.

## Video-free day-result states (KAN-27)

`presentsDayResultStatesWithoutVideo` uses two distinct synthetic route recordings
and production QML at 760×480. It covers initial empty state, automatic results,
loading and repeated-retry rejection, partial availability with an identified
missing run, restoration through the Retry recordings button, source-identity
failure, all sources missing, and stale worker completion after New Project.
It asserts that unaffected detail geometry/cursor, editor run/sync and the clean
document revision survive retry and result inspection. Existing import, ranking,
progression and project-reopen regressions remain part of the full native suite.

This task restores macOS Debug/Release Cloud CI with owner authorization. No
separate run is requested for task 016; task 017 runs include its committed base.
Private recordings and GoPro hardware/interactive acceptance remain separate.

The resumed Qt 6.8.3 gate exposed a test-only dependency on the newer
`QtGuiTest::postFakeWindowActivation` helper. The window-shortcut regression now
injects a focus-window event using `QWindowSystemInterface` from the matching
GuiPrivate SDK and still asserts actual focus and Escape behavior. This retains
the existing synthetic activation approach on the supported Qt 6.8 SDK.

## Product naming and compatibility (KAN-18)

Two application regressions compare native settings/data/recovery locations before
and after initialization of the new display name, then reopen a saved project,
preferences and unsaved recovery across that rename. Production preference values
are never read or changed by these tests. Existing test identities remain isolated.
The production startup smoke checks the window and About titles, opens About and
then exercises the existing welcome, scene and startup-error paths. Since KAN-166
step 3 it expects the welcome screen over the editor (whose header holds the
`editorRunPicker`) and exactly one media player, the editor preview's. Release
packaging validates the renamed executable and bundle name, the stable bundle ID
and the unchanged absence of document/URL registration before SDK-isolated startup.
The `.fetproject` schema and File > Open Project workflow remain unchanged.
Windows naming edits are not Windows execution evidence.

## Cloud CI

**Resumed by owner on 13 September 2026 after the quota pause.** Verify each
published change against its own CI run; historical task records do not validate
new code. Keep the [local task workflow](development-workflow.md) for implementation
and private-media acceptance.

[Native CI](../.github/workflows/build.yml) runs on pull requests, pushes to `main`, and manual dispatch. Two macOS arm64 jobs configure Debug and Release Ninja builds with Qt 6.8.3 (the supported minimum minor), compile the application and tests with C++20, and run all 49 CTest registrations: 43 Qt Test executables (the five GUI application suites, the telemetry-core and telemetry-app suites, the per-module suites, the KAN-125 storage-migration suite, the KAN-180 content-id vector suite, the KAN-218 project vector suite and the KAN-178 command-line suite), the production QML startup smoke, the KAN-178 command-line usage check, the two library-boundary checks and the two checks that the boundary script rejects a bad fixture. Release jobs additionally deploy Qt and run installed startup with the build SDK hidden, then attach internal candidate archives. Windows builds, tests and installer validation are paused by owner direction on 13 September 2026; resume them only when explicitly requested. Earlier Windows results below are historical. See [Windows installer](windows-installer.md).

| Job | Renderer | Toolchain |
| --- | --- | --- |
| `macOS arm64 / Debug or Release / Qt 6.8.3` | Metal; Cocoa for native window interaction | `macos-15`, Apple Clang |
| `Linux x64 / ASan+UBSan / Qt 6.8.3` | None (offscreen; GUI-free tests only) | `ubuntu-24.04`, GCC, AddressSanitizer with leak checks and UndefinedBehaviorSanitizer |
| `Linux x64 / TSan / Qt 6.8.3` | None (offscreen; GUI-free tests only) | `ubuntu-24.04`, GCC, ThreadSanitizer |
| `Linux x64 / GUI ASan+UBSan / Qt 6.8.3` | Mesa software OpenGL on Xvfb | `ubuntu-24.04`, GCC, AddressSanitizer without leak checks and UndefinedBehaviorSanitizer |

A third job (KAN-154) builds a Debug configuration on Linux with
`-fsanitize=address,undefined -fno-sanitize-recover=undefined` and runs every
CTest test except the GUI application suite, the startup smoke, the command-line
usage check and the boundary scripts: 38 Qt Test executables since KAN-218. Those excluded
need a display, a GPU and FFmpeg 8.1, which the macOS jobs supply. Any sanitizer
report, including a leak, fails the job. Checked locally on 5 October 2026 with
Qt 6.8.3 and GCC 13: all 37 pass, `flappedear_recording_alignment_tests` takes
about 3.5 minutes under the sanitizers, and a heap overflow injected into a test
fails it with an AddressSanitizer report. The job sets
`ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1:quarantine_size_mb=32`.
The smaller quarantine (default 256 MiB) keeps freed memory held for
use-after-free detection from counting toward the 300 MiB peak-memory budget of
`boundsVboHeaderAndDecodedValues`: run alone, that test grows by 26 MiB without
sanitizers, by 563 MiB with the default quarantine and by 150 MiB with 32 MiB.
The budget still catches the gigabyte-scale growth it guards against. To run the
same locally:

```bash
flags='-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined'
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_C_FLAGS=$flags" \
  "-DCMAKE_CXX_FLAGS=$flags" "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"
```

A fourth job (KAN-219) builds the same GUI-free tests with
`-fsanitize=thread -O1 -g` and runs them under ThreadSanitizer; any report
fails it. Qt's prebuilt libraries are not instrumented, so
[`.github/tsan-suppressions.txt`](../.github/tsan-suppressions.txt) ignores the
calls Qt Core and Qt Test make into intercepted functions (`memmove`,
`pthread_cond_destroy`, ...) with `called_from_lib`; our own loads and stores
are still checked. The job also leaves out `flappedear_telemetry_app_tests` and
`flappedear_project_vector_tests`, which drive the reference app: its
`QtConcurrent::run` tasks reach the worker through `QThreadPool`, whose
futex-based locking ThreadSanitizer cannot see, so every value a task captures
reads as a race with the main thread that wrote it (over 300 reports, all of
that shape). Checking that suite needs a Qt built with `-sanitize thread`.
`TelemetrySessionCache::load` now polls `std::mutex::try_lock` every 2 ms
instead of `std::timed_mutex::try_lock_for`: GCC 13's ThreadSanitizer does not
intercept `pthread_mutex_clocklock`, so it saw every unlock of the cache lock
without the lock. Checked locally on 6 October 2026 with Qt 6.8.3 and GCC 13:
the other 36 suites pass with no report, and a race injected between two threads
(one on a `QVector`, one through `QThreadPool::start`) is still reported with
the suppressions in place. The job lowers `vm.mmap_rnd_bits` to
28, which GCC 13's runtime needs on the runner kernel. Locally:

```bash
flags='-fsanitize=thread -fno-omit-frame-pointer -O1 -g'
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_C_FLAGS=$flags" \
  "-DCMAKE_CXX_FLAGS=$flags" "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread"
TSAN_OPTIONS="suppressions=$PWD/.github/tsan-suppressions.txt" \
  ./build-tsan/native/tests/flappedear_source_cache_tests
```

A fifth job (KAN-219) runs three of the five GUI application suites
(`flappedear_native_tests_editor`, `_project` and `_widgets`) built with
`-fsanitize=address,undefined` on Xvfb with Mesa's OpenGL
(`QT_QPA_PLATFORM=xcb`, `QSG_RHI_BACKEND=opengl`). The sources and export
suites are left out because they need FFmpeg 8.1. Leak checks are off: on exit
Qt Quick's QML engine leaves only indirect leaks (cycles) whose stacks reach
no frame of this code, 671 allocations in the widgets suite. The job also
drops `strict_string_checks`, under which libxkbcommon's keymap loading fails
inside `strndup` before any test runs. Checked locally on 6 October 2026 with
Qt 6.8.3 and GCC 13: the editor (20 passed, 3 skipped) and widgets (44 passed)
suites report nothing; the project suite fails only its two atomic-write tests,
which cannot pass as root, and runs as a normal user in CI.

Pull request runs are cancelled by a newer push to the same pull request; runs
on `main` are never cancelled, so every `main` revision gets a completed run.

Native interaction tests expose real windows and therefore require native QPA handles. Offscreen QPA cannot supply the NSView/HWND required by a native QRhi swapchain. Export pixel tests retain QRhi render-control targets; the separate startup smoke explicitly uses offscreen/software. Do not suppress input assertions or renderer coverage to avoid a platform mismatch.

Qt's private GUI headers are required by CMake and supplied by the matching SDK. CMake accepts the GuiPrivate target exported by Qt 6.8's Gui package and loads the separate matching GuiPrivate package on newer SDKs when needed. Qt Multimedia and Shader Tools are installed explicitly; SVG comes with the base Qt archives. The Qt version is pinned because QRhi/GuiPrivate APIs are version-sensitive; changing it still requires local render/export smoke validation. This baseline does not establish compatibility with every later Qt release.

FFmpeg comes from Homebrew on macOS and Chocolatey on Windows. Each job fails early if `ffmpeg`, `ffprobe`, libx264, libx265 Main10, FFV1, or AAC support is absent. These package-manager versions can change; exact versions and encoder capabilities are retained in the diagnostic artifact. Synthetic media is generated by the tests, with no private recordings or repository secrets required.

The workflow supplies `QT_QUICK_BACKEND=rhi` and `QSG_RENDER_LOOP=basic`.
CTest overrides the workflow's default offscreen QPA with Cocoa/Windows for the
native application suite, because exposed windows need real platform handles.
Windows uses D3D11 WARP through `QSG_RHI_PREFER_SOFTWARE_RENDERER=1`. QRhi export
pixel tests continue to use render-control texture targets. Do not select Qt
Quick software/null to hide renderer failures. The separate startup-smoke test
uses offscreen/software because it checks QML loading and lifecycle, not pixels.


The Qt 6.8 build also covers the `QImage::mirrored(false, true)` vertical-readback compatibility path; Qt 6.9 and newer retain `QImage::flipped(Qt::Vertical)`. Both represent the same vertical flip, without changing the readback orientation or alpha contract.

`FLAPPEDEAR_SKIP_HARDWARE_TESTS=1` explicitly skips only `preservesTenBitFullRangeColorThroughVideoToolboxExport`. Private fixtures remain unset, and platform-specific cases retain their existing skip reasons. CTest runs verbosely so the full Qt Test pass/fail/skip output is retained even on success. Unset the hardware-skip variable for local VideoToolbox acceptance.

`QT_FORCE_STDERR_LOGGING=1` makes Qt and Qt Test diagnostics visible to CTest on Windows, where GUI executables otherwise send these messages to the debugger. Startup's required success marker and failure patterns are therefore checked against captured output.

Cancellation-marker failure uses the existing native stalled-process helper on both platforms, so the forced worker-stop assertions also exercise Windows Job Object supervision without requiring a POSIX shell.

The two active macOS jobs use read-only repository permissions, do not retain checkout credentials, pin action implementations to commit SHAs, cancel superseded runs, limit build parallelism to two, and retain only logs/JUnit results for 14 days. The pinned Qt installer implementation is called directly so its wrapper cannot introduce mutable nested action references. Build/job/test timeouts bound stalled runs. `qmllint` is deliberately excluded because its previous project invocation exhausted memory.

A successful cloud run verifies this synthetic regression gate. It does not certify hardware encoders, private VBO/GoPro recordings, interactive UI behavior, installers, signing, or notarization. Branch protection is a separate repository setting; after all required jobs pass, verify the exact PR head before merging. See each run's actual results before claiming CI passes.

For broadcast-HUD visual changes, render the same production QML acceptance composition against both supplied synthetic backgrounds:

```bash
"build-native/native/FlappedEar Overlays.app/Contents/MacOS/FlappedEar Overlays" \
  --render-visual-smoke docs/assets/motorsport-broadcast-acceptance.png
"build-native/native/FlappedEar Overlays.app/Contents/MacOS/FlappedEar Overlays" \
  --render-visual-smoke-dark docs/assets/motorsport-broadcast-acceptance-dark.png
```

The capture refuses to save unless all nine production widget frames are visible. Inspect both images; a successful command alone is not a visual acceptance result.

## Event import preparation

`flappedear_import_tests` is registered with the existing CTest gate. It covers
mixed VBO/RCZ batches, native channel/no-data preservation, source-derived lap
results (including complete timed laps), macOS linked-source format dispatch,
renamed/repeated exact duplicates, stable proposal identity across
input ordering, per-file failures, wrong file formats, byte/sample/file-count
ceilings and cooperative cancellation without partial publication. Synthetic
GPS pairs cover elapsed-origin differences, stationary/distant/sparse/duration
mismatches and ambiguous one-to-many candidates. Candidate matches must never
merge recordings automatically. See [the import contract](event-analysis-plan.md).

This slice requires macOS CI validation because this Work environment has no
usable Qt/CMake toolchain. Private RCZ/VBO equivalence, interactive import UI and
physical hardware export acceptance are separate; the latter two are not added
by a backend planner test. Existing CI jobs and their skip policy are unchanged.

Run the focused gate with:

```bash
ctest --test-dir build-native -R '^flappedear_import_tests$' --output-on-failure
```

## Private real fixtures

The optional day-analysis integration imports every VBO in a local directory
through the production controller, without layout, direction or group clicks:

```bash
FLAPPEDEAR_REAL_DAY="$PWD/jastrzab" \
  ./build-native/native/tests/flappedear_native_tests_sources automaticallyGroupsPrivateTrackDay
```

It expects multiple recordings of one compatible route and direction and checks
automatic rankings/progression for every run. The repo-root `jastrzab/` directory
is ignored; never add private sessions to the repository. Optional
`FLAPPEDEAR_DAY_REVIEW_PROJECT` (and, up to commit `7eae6cd`, `FLAPPEDEAR_DAY_REVIEW_IMAGE`) paths write a local
review project and captures of the production Analysis window and Progression
dialog, correction controls and GPS traces. Store those outside the repository. These captures and keyboard-driven
QML regressions complement, rather than establish, owner-operated acceptance.
The Escape regression synthesizes Qt window activation as well as key input:
macOS does not always grant foreground activation to a shell-launched test.
Its production shortcut and selection assertions remain enabled.
See [KAN-26 local validation](kan26-local-validation.md) for the executed gate
and private-recording results.

Optional real-media tests read paths from environment variables:

```bash
FLAPPEDEAR_REAL_GOPRO=/path/to/video.mp4 \
FLAPPEDEAR_REAL_VBO=/path/to/session.vbo \
  ./build-native/native/tests/flappedear_native_tests
```

Private VBO and GoPro media are ignored by Git and must remain local. A VBO-only run can set `FLAPPEDEAR_REAL_VBO`; GoPro synchronization requires both variables.

## Current test coverage

- VBO parsing, deterministic mid-parse/export-preparation cancellation, cancellable track construction, file/line/row/column/field limits, malformed input, text time formats, monotonic/rollover behavior, coordinate conversion, and optional real VBO parsing.
- Missing-versus-zero raw and overlay presentation semantics, bounded stale holding, and channel availability.
- Segmented raw analysis ranges, including controller-to-QML nested segment transport, partial overlap under non-zero synchronization offsets, constant traces, cadence-relative timestamp gaps with one cached cadence statistic per channel, bounded min/max decimation that retains short peaks, and an optional real-VBO 10,000-lookups cache benchmark.
- Deterministic, ambiguous, and cooperatively cancelled GPS-speed synchronization, plus optional real GoPro/VBO synchronization.
- GPS9 GPMF decoding, malformed packet extents, container-depth and KLV-header-count limits with count/limit/context diagnostics, and deterministic sorting/deduplication of timestamps.
- A portable native fake ffprobe covers prompt cancellation/reaping and bounded stdout without shell dependencies or long real inputs.
- JSON boundary tests cover normal acceptance, malformed and over-limit project files, widget/settings/template cardinality limits, and bounded recovery/manifest parsing. Process-boundary tests cover complete-payload overflow, retained diagnostic tails, and pathological no-newline FFmpeg progress.
- Widget, group, cue, template, and track-geometry behavior, including central semantic normalization across mutation, scene import, template application/import/reload, finite geometry, color/default recovery, cue repair, and duplicate-ID rejection; custom-template create/update/reload/rollback behavior; F1/bar G-Force defaults and persistence; retired widget types left out of scenes, templates and the library (KAN-192); optional font-setting serialization and duplication; a 30,000-point cache benchmark; same-address geometry invalidation; cache clearing; and marker movement. A production-RHI-backed pixel regression renders the Canvas-based `retroTachometer` through the offscreen lifecycle. It requires Canvas-colored pixels in the first non-zero-range frame and a later frame, and requires the first frame to match a repeated render at the same requested telemetry time. `repaintsOnlyMovingGaugeLayers` renders the Tech HUD in both styles and requires the gauge faces to stay unpainted across telemetry frames while the value layers repaint, and every layer to repaint after a settings change (KAN-199).
- Project atomic-save behavior; authoritative startup loading; QSettings document-key retirement; dirty-state actions for Quit, New, and Open; explicit saved and project-less recovery/discard; failed-save recovery retention; recovery v2 validity (newer, equal, older, malformed, and identity-mismatched snapshots), stale-cleanup retry after simulated deletion failure, and both new-project and existing-project Save As. Recovery-discard tests inject deletion and tombstone-write failures: Quit/New/Open continue after durable intent, Cancel leaves recovery untouched, startup removes a suppressed residual, newer same-identity and other-identity snapshots remain recoverable, dual failure cancels discard, legacy v1 ignores tombstones, and cleanup debt never sets `recoveryDegraded`; unknown-field preservation and v2 migration; project-relative source serialization and whole-folder moves; independent missing video/VBO states; deterministic fingerprints and sampled-byte mismatch; valid/invalid relink candidates; explicit mismatch replacement; stale relink rejection; and bounded controller shutdown.
- Export-output transaction safety, including native regular-file identity capture, unchanged replacement, in-place modification/replacement/disappearance refusal, new-target appearance refusal, and symlink rejection where the host permits link creation; plus export progress/diagnostics parsing, durable export-log creation/append/retention, media probing, exact rational rate comparison, and HEVC encoder detection.
- Source-media coverage for 8-bit, 10-bit, unknown depth, Rec.709, HLG/PQ/Log classification, coded/display raster, rotation/SAR retention plus export-only rejection for non-zero rotation and non-square SAR, arbitrary 5.3K and 8:7 rasters, non-heavy 8K representation, aspect-preserving downscales, checked RGBA sizing, continuous bitrate, centralized export profiles, renderer rejection, and encoder capability-cache keys. A deterministic FFmpeg composition proves that a Rec.709 10-bit source remains HEVC Main10/yuv420p10 with audio. On macOS with VideoToolbox available, a distinct-color full-range BT.709 fixture runs through the shared production Stage B graph and final HEVC encoder, software-decodes RGB, and enforces a per-channel mean absolute error of at most 12.
- Portable storage resolution coverage for existing files/directories, future files, nested future paths, and unavailable inputs; injectable multi-volume preflight and measured-sample/fallback/margin/overflow regressions.
- Portable raw-frame transport helper coverage for exact 1920×1080 and 3840×2160 RGBA frame transfers to a slow consumer, early consumer exit, sustained stall, prompt cancellation, and bounded queue size.
- KAN-157 gap rule (`flappedear_telemetry_core_tests`): `neverBridgesALossOfSignal` (a 30 s loss of fix is no data in every interpolation mode) and `syncIgnoresALossOfGpsFix` (dropped samples count like samples marked missing; without the rule auto-sync chose a wrong offset).
- Malformed/owned/live/idempotent manifest recovery rules; manifests written to the application data folder and legacy ones in the temporary folder both recovered, and a manifest path in another folder accepted by name and contents (KAN-164, `recoversManifestsFromDataAndLegacyFolders`); injected cancellation-marker write failure with synchronous supervised worker shutdown, and a macOS/Unix helper child/grandchild process-tree shutdown integration test. Windows Job Object setup errors are explicit. Windows runtime/export is validated on one known Windows 11 / Qt 6.11 / MSVC 2022 / Intel Iris Plus / Quick Sync configuration; native Windows ACL-denied coverage remains pending because `QFile::setPermissions()` does not model Windows ACL denial reliably.
- Deterministic bounded Stage B input-seek/absolute-trim mapping (including start-near-zero, short and late ranges) plus synthetic FFmpeg integrations for non-zero source stream PTS/audio, non-zero-range video/audio timelines, the exact `60000/1001` 30→90 boundary schedule (3,597 packets), VFR-to-CFR conversion, CFR packet/frame counts, completed-overlay frame identity, full decoded FFV1 staged-overlay frame counts, and premultiplied-alpha source-over samples including a translucent antialiased edge. The alpha fixture also decodes Stage A's FFV1/BGRA streams and requires byte-exact preservation before Stage B composition. The Main10 color test uses a fully transparent premultiplied overlay so any RGB divergence identifies the 10-bit overlay/encoder path rather than intended widget pixels.
- Frame-addressed export regressions cover the 78,272-frame `60000/1001` source domain and its exact `duration_ts` fallback, integer-derived bounded Stage-B timestamps, full/limited range counts, strict Stage-B-progress versus ffprobe agreement, and the 0/1..10/>10/surplus terminal-deficit matrix with exact contiguous-CFR timestamp evidence. A synthetic 30 fps controller integration additionally verifies Out lap/In lap navigation, C++-generated single-lap hotlap SMPTE IN/OUT, and exact parsed frame-range duration. Temporary-overlay validation separately retains the proven `60000/1001` Matroska 1 ms timestamp-quantization regression (`19001/317`) while rejecting a meaningful `30/1` mismatch.
- The startup QML smoke rejects `ReferenceError`, `TypeError`, and binding-loop diagnostics and verifies one primary decoder while Analysis is closed, two while open, and release back to one after close.
- Manual editor smoke at 1180×720 verifies both sidebar endpoints are reachable, full-screen transport is visible and scrubbed through the primary player, and Very Verbose detached log inspection does not move when diagnostics append.

The August 2026 macOS real-fixture regression run measured 1,536 GPMF packets, 259,584 parsed KLV headers, 14,796 GPS9 samples, +90.217 s synchronization offset, and 0.999575 correlation. The same source passed final HEVC/AAC packet validation at 3840×2160 `60000/1001` for 30→90 (3,597 packets), 1920×1080 `60000/1001` for 30→33 (180 packets), and 1280×720 `30000/1001` for 30→35 (150 packets). A separate private HERO11 5312×2988 Main10/BT.709 clip passed native 5312×2988 and 3840×2160 Main10 exports at `60000/1001`, each with 442 final video packets and AAC; its `gpmd` track had no usable GPS-speed samples. The transparent-chroma and Canvas-readiness regressions were independently validated on a 5.855-second 3840×2160 Main10/full-range BT.709 source: the production nine-widget VideoToolbox export produced 351 packets, AAC at 48 kHz, the Retro Tachometer in matched 0.860 s and 2.002 s decoded frames, normal software-decoded color, and 45.56 dB average PSNR in an overlay-free crop. These are private fixtures on one macOS machine, not broad compatibility claims.

Unit and synthetic integration tests do not replace manual real-media validation. The latter should identify the fixture class, platform, source range, output properties, and any untested behavior.

## September 2026 lap-timing validation

Deterministic tests cover malformed and ambiguous gates, directional passage derivation, telemetry-end-inside-gate finalization, first-pass Current state, controller publication/clearing, synchronized lap-to-video mapping, Out lap/In lap fragments, C++ hotlap range generation, and all six comparison widgets through the production QML scene. Historical endpoint interpretation: the optional private Jastrząb VBO/GoPro run derived four accepted passages and three complete laps, with the fastest lap at 111.245 s; GPS-speed synchronization measured offset 7.817 s, correlation 0.998, and confidence 0.970. The September 11 exporter-specific correction supersedes that lap count: the matching RCZ/VBO pair now derives five complete laps through both parsers. The old sync result remains historical; this is not fresh candidate-video acceptance.

## Before claiming a feature works

- Run the focused test and the normal local gate when applicable.
- For parser or synchronization work, include malformed and deterministic/ambiguous cases.
- For export changes, distinguish staged-overlay tests from final real-media validation.
- For real media, state platform and fixture scope; do not generalize one machine's result.
- For UI or host behavior, reproduce the visible interaction rather than inferring it from a build or unit test.

## September 11 shipping fixes: frame counts

`floorsConvertedFrameCounts` exercises the production range calculation with missing
frame metadata and changed rates: odd durations, residual denominators, sub-frame
ranges, invalid rationals, overflow and factor cancellation. Counts use checked
integer floor arithmetic. Native execution is covered by macOS/Windows CI; this
change does not claim a new private-recording export acceptance run.

## September 11 shipping fixes: synchronization confidence

Refinement cannot increase confidence above the global search result. Automatic
application also requires twenty seconds of usable resampled overlap in both
search passes. Regressions cover equal peaks separated by 20 seconds and a short
overlap with strong correlation; the existing distinctive 3.2-second fixture must
still auto-apply. Constant-speed and cooperative-cancellation checks remain.

## September 11 shipping fixes: template persistence

Template writes validate the same count, structure and byte limits as reads before
opening the destination, require a complete atomic write, and roll back the
in-memory mutation on failure. A rejected store stays untouched and blocks writes
until a successful reload; errors appear in the template sidebar/save popup.
Live add/duplicate/cue operations enforce the corresponding document count limits.
Regressions cover the 128-template boundary, writer byte growth, malformed and
oversized stores across reload/restart, recovery after restoring a valid store,
and widget/per-widget/total cue boundaries.

## September 11 shipping fixes: recovery ownership

The editor takes a per-user application-data `GuiSessionLock` before shared
settings, logs, export cleanup, or AppController initialization. Another editor
shows a startup error and cannot access recovery; export workers remain separate.
The lock disables age-based expiry and uses Qt process-identity stale-lock recovery
([QLockFile](https://doc.qt.io/qt-6/qlockfile.html)). Two-process native tests verify
exclusion, preservation of recovery bytes, clean release, killed-owner recovery,
and failure when the directory is unavailable. Startup smoke also loads the guard
error window. Older app versions do not participate in this lock and must be closed
before running this build. On Windows, Qt documents a stale-lock detection limitation
for non-ASCII hostnames; failure remains closed rather than risking recovery data.

## Source-loading interleavings

Ordinary import and explicit relink preserve the entire other pending request across
a source generation: path, fingerprint, mismatch-confirmation policy and dirty-state
intent. Eight controller regressions cover both asset orders, import/relink and matching/
mismatching project references. Replacing one asset must not strand the other or accept
a mismatched reference without confirmation. These source-loading rules also
remain required by the integrated transactional multi-file import/review and
whole-outing workflow. The planner, event persistence and controller integrations
are described in [batch import](batch-import.md) and [event analysis](event-analysis-plan.md);
they are present in the baseline recorded in the [delivery ledger](product-delivery.md).

## Original media timestamps

Stage B retains original input timestamps with `-copyts` and absolute timestamp seeking
(`-seek_timestamp 1`). Seek and trim share that domain; only filtered output is rebased.
Production argument/graph regressions encode frame identities into a positive-PTS MP4
and check every decoded frame in full, early and seeked ranges.
See [FFmpeg timestamp options](https://ffmpeg.org/ffmpeg.html#Advanced-options).

Audio trims use original timestamps and subtract the selected video origin, preserving
a track's real delay. Selection/validation use the intersection with the audio stream;
ranges before/after that stream export without audio. Worker regressions cover positive
video PTS, delayed short audio, and ranges before/within/after audio. They verify output
frame counts, audio start/duration and decoded tone energy near the start of the stream.

### Composition capability preflight (R8)

Before rendering any representative or full telemetry overlay, export executes three
64×64 frames through the production Stage B graph for the selected bit depth. This
checks explicit alpha-mode support rather than inferring compatibility from an
FFmpeg version or encoder listing. Failure is actionable, diagnostic output is bounded,
and the probe supports cancellation and a 20-second execution deadline. Synthetic
tests exercise both 8-bit and 10-bit graphs, missing filters, and cancellation.

## Manual timing edits during auto-sync

Controller regressions deliver a controlled asynchronous result after offset/scale
edits and an edit-then-restore sequence. Timing edits cancel work, invalidate review
candidates, and advance a revision so an already-completed result cannot overwrite
them. An unedited result still applies; explicit candidate application retains scale.

## RaceChrono VBO gate conversion

The RCZ suite verifies the identified Pro 10.2.4 centre/direction representation,
unchanged generic VBO endpoints, invalid geometry, and warning-only gate omission for
unverified RaceChrono exporters. The private pair test now checks both parsers against
recorded lap metadata: five laps each, maximum VBO duration error 0.0104 s. The local
Qt 6.8.3 run passed 30/30 tests including that private comparison.

The zlib 1.3.2 source has two upstream download locations (zlib.net and the official
madler/zlib release asset), verified against the same pinned SHA-256. The fallback
addresses intermittent invalid downloads without accepting changed dependency bytes.

## September 12 import-readability acceptance

PR #10's G-direction regression exposed a QML scope error in the G ball:
the nonvisual `GForceData` helper must bind to `root.frame` explicitly. The
regression now also requires both dots to be visible before checking braking,
acceleration and the retained manual longitudinal inversion setting.

The local Save As fixture now creates its destination directory, as a real save
requires. This lets macOS canonicalize the temporary-root alias consistently;
the test still requires the moved relative source to win over the stale absolute
fallback and retain its fingerprint.

On macOS 26.5.2 arm64 with Qt 6.11.1, the application build and all five CTest
registrations passed (302 Qt Test passes, six optional private-fixture skips).
QRhi rendering and synthetic FFmpeg integrations passed, including the distinct
VideoToolbox Main10/full-range color test; hardware skipping was not enabled.

A separate temporary Qt harness loaded production Main/Analysis QML with the real
AppController and isolated test settings. At 1180×720, native Cocoa/Metal windows
were exercised using Qt Test mouse/keyboard events: destination and grouping
dropdowns, RCZ linked to VBO, event creation, and Analysis without video. Captures
were visually inspected for readable highlighted rows and detected lap times.
Both production G widgets were captured with synthetic ±0.5 g longitudinal input:
braking above centre, acceleration below centre with inversion disabled, then
manual inversion enabled. The OS file-picker interaction was not exercised by
this harness. Captures and private data were not added to the repository.

Separately, the private matching VBO/RCZ parser comparison passed: five complete
laps through each parser, maximum duration differences against recorded metadata
of 0.010323 s (VBO) and 0.007576 s (RCZ). VBO parsing reported 13,819 samples,
49 channels and no warnings. This iteration did not run a private GoPro decode,
synchronization or final recording export acceptance.

## September 12 whole-outing lap list acceptance

The native macOS Debug build with Qt 6.11.1 passed
`cmake --build build-native --parallel` and
`ctest --test-dir build-native --output-on-failure`: all five registrations,
310 Qt Test passes and six optional private-fixture skips. The local VideoToolbox
ten-bit/full-range integration passed; hardware tests were not disabled.

Synthetic regressions cover automatic import through the production Analysis QML,
partial failure, duplicate handling, cancellation and stale generations, plus a
reverse-imported morning/afternoon outing sorted into ten OUT/LAP/IN sections.
Save/reopen preserves the derived list; missing and replaced sources are reported
without rejecting the document, and stale worker completion cannot refill a new
document. Parser cases include malformed/missing UTC metadata and midnight;
matching cases cover dated coordinate conventions and ambiguous alternatives.

Separately, a temporary native Qt Test harness loaded production Main/Analysis
with isolated settings on Cocoa/Metal. It clicked Lap Analysis, entered an outing
name containing spaces and submitted the private VBO/RCZ pair at the selected-files
boundary. The result was one run, VBO primary with RCZ retained, and seven rows:
OUT, five complete LAPs, IN. All 32 compared GPS points matched within 1.26 m after
accounting for the export coordinate conventions and absolute sample times.
Screenshots were inspected at the normal window size and 760×480, including
scrolling to the final IN row. The native OS file picker itself was not exercised.
Private recordings, temporary harnesses and screenshots are not committed.

This validates boundary-fragment classification; it does not establish pit-lane
or intermediate-pause detection. No fresh real-GoPro synchronization or private
recording export was performed for this iteration.

## September 12 clickable lap detail acceptance

The follow-up build and all five local CTest registrations passed on macOS with
Qt 6.11.1 (311 Qt Test passes; six optional private-fixture skips). The production
AnalysisWindow test now clicks an actual ListView delegate, waits for its detail
view, clicks Back, opens it with Enter and returns with Escape. Controller coverage
opens a lap from another run with a nonzero editor sync transform, checks the
bounded raw-time series/cursor, and verifies the editor document, active run and
playback remain unchanged. Delayed stale completion, rapid replacement selection,
Back cancellation and missing/replaced source errors are covered.

A separate temporary Cocoa/Metal harness clicked LAP 2 in the private matched
VBO/RCZ outing. The selected 1:49.898 lap displayed its map and velocity,
latacc-calc and longacc-calc charts. A real mouse movement over a plot advanced
the common cursor and map marker; Back restored the list. Screenshots were
inspected at 1240×760 and 760×480. Private fixtures/captures remain uncommitted.
The ordinary analysis chart renderer is shared with this view, and static map
paths stay separate from cursor updates. No private GoPro synchronization or
recording export was run; the local synthetic VideoToolbox integration passed.

The lap chart maps pointer position across its visible zoom window, not across
the whole lap, so the cursor follows the pointer exactly after zooming.
`opensOutingLapWithoutChangingEditor` loads the lap-detail `AnalysisPanel`, zooms
it, and checks that the left edge, middle and right edge of the plot map to the
zoom start, midpoint and end. It then checks that after a zoom reset the right
edge maps to the lap end again.

## Candidate acceptance

See [beta-acceptance.md](beta-acceptance.md) for supported scope, archive identity, installation prerequisites, the real-media walkthrough and required evidence. Passing a hosted startup check with the build SDK hidden is useful deployment evidence; it does not replace testing on a clean physical machine or using the final hardware encoder.

## Coordinated product hardening — 12 September 2026

Two additional standalone CTest registrations cover GPS-reference eligibility and
production export-log retention. Measured laps with interior GPS gaps/invalid
coordinates stay inspectable but cannot supply a reference or BEST/delta. Outing
rows propagate those reasons and best-of-run badges; cross-run compatibility is
still a separate requirement. Retention covers actual canonical UUIDs, legacy IDs,
active-log preservation, unrelated filenames and Unix symlinks.

Native build/CTest commands were attempted in the coordinator workspace and failed
because CMake/CTest are absent. Record exact-head CI results in the implementation
PR; this paragraph is not a native-pass claim. Earlier five-registration results
in this document are historical results for their stated snapshots.

### KAN-5: preserve gate evidence in interior-defect fixtures

PR #12 at `504fe26` compiled in all four Qt 6.8.3 CI jobs, but the new
lap-eligibility suite failed seven cases. The sparse event fixture uses each GPS
sample to arm, cross or finalize a timing-gate passage. Removing one of those
samples therefore removed a passage instead of testing an interior lap defect.

The corrected fixture interpolates the same synthetic path at 0.25-second
intervals and injects defects away from the gate. A separate regression verifies
that densification preserves the original start/end passage times and all three
eligible laps before defects are introduced. The defect cases still require
three measured laps, exclusion of invalid references, no numeric deltas from
invalid GPS, and propagation into outing rows and the renderer. Production
eligibility rules and CI rendering assertions are unchanged.

Final CI and merge evidence for this task is recorded in
[KAN-5](https://kozucharkadiusz.atlassian.net/browse/KAN-5) and
[PR #12](https://github.com/arekkozuch/VBOOverlay/pull/12). Local CMake/CTest
remain unavailable in this coordinator workspace; hosted checks do not establish
new physical Mac or private-video acceptance.

### KAN-13: export writers surviving their leader

The native suite now includes a synchronized helper whose isolated group leader
can exit before shutdown or during the grace period. Its descendant ignores
SIGTERM and repeatedly reopens an owned-path fixture for writing. Readiness is
explicitly acknowledged before the test releases the leader.

Regressions cover explicit stop, grace-period leader exit, supervisor destruction,
and cancellation-marker failure. They require the writer to be inactive when
shutdown returns, exercise repeated stop, and reject recreation after cleanup.
A controller regression checks that startup recovery retains an active group's
manifest after leader exit, then cancellation stops the writer before removing
owned artifacts and preserves an existing user target.

These Unix-specific cases run in macOS CI and are explicitly skipped on Windows;
the existing cross-platform cancellation-marker and export tests remain required.
Execution, final PR head and integrated-main evidence are recorded in
[KAN-13](https://kozucharkadiusz.atlassian.net/browse/KAN-13).

The controller cases cover both cancellation and reported success with a surviving
writer; the latter must fail without replacing the user's existing target. A
cross-platform case also verifies immediate/negative stop budgets and subsequent
bounded cleanup, so negative inputs cannot request infinite Qt waits.

The regression-only head `b7dc20c2e2ccaaf24eac89148d34ef5163e33348`
was run against unchanged production code in
[Native CI 34739681497](https://github.com/arekkozuch/VBOOverlay/actions/runs/34739681497).
Both macOS Debug and Release compiled and failed exactly the four surviving-writer
checks and the premature-recovery check; the existing cases had no new failures.
Final passing-head and integrated-main results are recorded in KAN-13.


### KAN-14: bounded VBO scanning

PR #15 adds synthetic coverage for separator-heavy rows and headers, exact and
exceeded line/field/column/line-count limits, CRLF versus terminal CR, ignored
oversized extra fields, multiline header fields, Unicode padding and preserved
ASCII whitespace/comma semantics. Extra values retain their original warning
counts without a field object for every separator. Existing file/sample limits,
valid VBO fixtures, timing-gate output and derived timestamp tests remain in the gate.

Four regression cases request cancellation while scanning input that would later
exceed a line, field or column limit. They must report OperationCancelled before
resource-limit validation, without depending on a wall-clock deadline. A separate
successful separator-heavy fixture is cancelled at every available checkpoint,
including discarded-field scanning. Test-only and corrected CI results are
recorded in [KAN-14](https://kozucharkadiusz.atlassian.net/browse/KAN-14) and
[PR #15](https://github.com/arekkozuch/VBOOverlay/pull/15).

The coordinator has no native CMake/Qt toolchain; actual compilation and CTest
execution require the four Qt 6.8.3 Native CI jobs. No private recording or
physical-hardware acceptance is claimed by these synthetic parser checks.


### KAN-15: derived VBO times and consumer conversions

Ten new Qt cases cover extreme finite inputs, elapsed differences beyond the
signed 64-bit microsecond range, mixed clock/relative formats, precision collapse,
the rounded-up integer boundary and its immediately preceding safe double,
midnight rollover, duplicates/backward clocks and overflowing chart ranges. The
safe boundary is exercised through the actual project telemetry fingerprint;
chart coverage includes the maximum int point budget and a zero-width range.
Existing timestamp text formats, UTC chronology and valid VBO fixtures stay in
the complete native gate.

Unsafe numeric ranges reject the complete parse with VboParseError before a
session can be published. Ordinary malformed text and duplicate/backward rows
inside the supported range retain warning/skip behavior. The numeric bound is
required by the existing signed 64-bit microsecond fingerprint conversion; it is
not a new recording-length product policy. Clock rollover and UTC date arithmetic
are checked independently.

The coordinator has no native CMake/Qt toolchain. Actual build/CTest evidence for
the final PR head and merged main belongs in
[KAN-15](https://kozucharkadiusz.atlassian.net/browse/KAN-15). Hosted validation does
not replace private-media or physical-hardware acceptance. Synchronization-engine
bounds remain the separate KAN-17 task.


### KAN-16: explicit VBO coordinate evidence

Thirty new Qt cases cover degrees, declared arc-minutes and the verified
RaceChrono Pro 10.2.4 marker in all four quadrants, crossings of both zero axes,
and a track where only one raw axis exceeds the former magnitude threshold.
Assertions check absolute sample coordinates, track origin/current marker,
gate endpoints or centre, and a physical gate width of approximately 20 metres.
Missing/unknown evidence, unsupported/empty declarations, conflicting units and
exporters in both orders, and spoofed derived metadata withhold GPS/gates while
preserving speed and timestamps. Bounds cover both signs, degree/minute limits,
non-finite values, and invalid live track positions.

Existing generic synthetic VBO fixtures now declare their degree units. Synthetic
fixtures carrying the verified RaceChrono marker now encode actual arc-minutes,
including dated RCZ pairing and the whole-outing/reopen regression. Existing lap
and pairing assertions are retained. The extension and deliberately limited
exporter recognition are documented in [telemetry-semantics.md](telemetry-semantics.md#gps-tracks).

The coordinator has no CMake/CTest/Qt toolchain. Exact build, complete CTest and
Release package/startup results for the PR and merged main are recorded in
[KAN-16](https://kozucharkadiusz.atlassian.net/browse/KAN-16). Only macOS arm64
Debug and Release are in scope under the owner's current platform instruction.
Historical private RCZ/VBO evidence above is not a new private-media or hardware
acceptance run.


### KAN-17: synchronization transform and search bounds

Thirty-three new Qt cases cover finite extreme transforms, forward multiplication/
addition overflow, inverse subtraction/division overflow, non-finite inputs,
zero/negative/tiny scales, and restoration of ordinary no-gap lookup. The shared
preview/offscreen context and controller analysis/value APIs expose no data on
overflow and recover when the transform is corrected. An event JSON round trip
preserves a finite extreme scale while the queried overflowing time is unavailable.

Auto-sync cases cover empty/mismatched channels, non-finite/non-monotonic times,
non-advancing numeric grids, overflowing differences and all source/grid/work
budgets. A cancellation watchdog prevents a stalled implementation from hanging
the regression; success requires an explicit error before that watchdog fires.
A real ambiguous engine result delivered through the controller's async watcher
preserves a confirmed non-default offset and scale. Invalid confidence/transform
candidates cannot auto-apply. Existing deterministic offset, periodic ambiguity,
short-overlap, cancellation, timing-edit revision, rendering and software-export
tests remain in the complete native suite.

The [consumer trace and numeric contract](telemetry-semantics.md#synchronization-transforms-and-numeric-bounds)
records which existing guards needed no change. Exact PR and main macOS arm64
Debug/Release CI evidence is recorded in [KAN-17](https://kozucharkadiusz.atlassian.net/browse/KAN-17).
The coordinator lacks CMake/CTest/Qt; native validation runs in CI. Windows remains
paused, and private recordings/physical hardware require separate acceptance.

## KAN-111: braking graph and lap-channel controls

Longitudinal-G graphs use braking-up presentation, matching the existing G ball
and radar. Signed samples, cursor readouts, other channel axes and track geometry
are unchanged. A presentation test verifies the selected longitudinal alias and
retained signs; the production QML regression checks opposite braking/acceleration
positions and unchanged lateral mapping.

Lap-detail controls add, replace and remove up to four recorded channels. The
picker uses the selected lap recording, independently of the active editor run.
Choices are stored as analysis preferences; unavailable channels are omitted in
other recordings and an intentionally empty selection stays empty. Reopen tests
cover preference retention, duplicate/unknown rejection, the four-channel bound,
and unchanged project, active run, cursor and track. The native QML test clicks
Add/Remove and operates the replacement selector with the keyboard.

Validation evidence and final PR/main macOS CI links are recorded in
[KAN-111](https://kozucharkadiusz.atlassian.net/browse/KAN-111). Windows execution
remains paused. No GPS parser or track layout changes are included.

## KAN-112: RaceChrono VBO map orientation

Five data-driven cases compare an asymmetric track encoded as east-positive
degrees and verified RaceChrono west-positive arc-minutes, across all hemispheres
and across zero. Static geometry and interpolated markers must agree, with east
right and north up. Source values remain intact; missing coordinates remain no
data. The shared preview/export render context uses the corrected marker.

A controller integration case imports the west-positive recording and opens lap
detail, checking its track and cursor against the editor while retaining the
project document, input file and static geometry. The lap geometry carries the
source convention into its reduced map session. Layout and graph controls are
unchanged; no parser or timing-gate changes are required.

Read-only inspection of the owner's paired Jastrzab VBO/RCZ files confirms the
opposite raw longitude signs. That inspection is separate from native execution
with private recordings. The coordinator has no CMake/CTest/Qt toolchain; exact
macOS arm64 Debug/Release PR and main CI evidence is recorded in
[KAN-112](https://kozucharkadiusz.atlassian.net/browse/KAN-112). Windows execution
remains paused.

## KAN-19: track configuration and source-bound derivation identity

Event codec regressions cover legacy unknown state, explicit JSON/recovery/editor
round trips, malformed layout/direction/revision fields, foreign/alternative
source bindings and stale fingerprints. Dependency keys change on configuration
or source changes while names, notes, synchronization and portable paths remain
independent. Same-content relocation retains configuration; replacement clears it.

Gate revision tests compare equivalent east/west-positive geometry, retain the
revision on label edits, change it on endpoint edits and preserve unresolved
states for missing, ambiguous and invalid gates; cancellation remains explicit.
A controller regression imports source gates, edits configuration transactionally,
invalidates open lap detail without reloading the editor, saves/reopens, rejects
an asserted stale gate revision and clears metadata after source replacement.

Native validation is performed by macOS arm64 Debug/Release PR and main CI because
the coordinator lacks CMake/CTest/Qt. Exact runs and results are recorded in
[KAN-19](https://kozucharkadiusz.atlassian.net/browse/KAN-19). Windows execution
remains paused. Synthetic coverage does not claim private-file or physical-Mac
acceptance, track recognition, compatibility grouping or durable lap references.

## KAN-20: stable lap references

Thirteen malformed-reference cases reject unsupported versions, absent/oversized
identities, invalid digests, invalid section types and non-finite/reversed bounds.
Controller regressions cover JSON reference round trips through Save As/reopen,
row reordering and display renumbering, exact matching, ambiguity, configuration
and gate changes, source replacement, loading and missing-source states. Invalid
or stale references cannot select a replacement row. Existing production QML
keyboard/click coverage now opens rows through the reference API.

A 512 KiB synthetic recording is edited outside the three sampled fingerprint
blocks. Its sampled digest remains identical while its full content revision
changes; detail rejects the old reference and a fresh derivation marks it stale.
The shared full-digest reader retains size bounds, exact byte counts and explicit
cancellation; batch import reuses that same reader.

Exact macOS Debug/Release PR/main test and installed-startup evidence is recorded
in [KAN-20](https://kozucharkadiusz.atlassian.net/browse/KAN-20). The coordinator
lacks a native Qt/CMake/CTest toolchain. Windows remains paused; private recording
performance and physical Mac acceptance are not inferred from hosted tests.

## KAN-132: tyre temperature and pressure

Core tests (`TelemetryCoreTests`) cover:
- **Channel mapping:** RaceChrono CAN names, declared bar, psi and °F units, a
  pressure outside every unit range, and names that are not one corner's tyre
  value (no corner, two corners, both kinds, a brake temperature). A recording
  without tyre channels maps nothing.
- **Readings:** placeholders before a sensor's first report, interpolation
  between valid samples, no bridging across a placeholder or a gap, no value
  after the recording or at a non-finite time, and an implausible temperature
  as no data.

`TelemetryTests` checks that:
- preview (Main.qml) shows the primed frame's values and dashes;
- the inspector switches pressure to psi;
- the offscreen export scene draws the front-left corner once its sensor
  reports.

`FLAPPEDEAR_REAL_DAY` adds `readsPrivateTyreData`, which gives each corner's
coverage and range per session, and a real mid-session export frame
(`tyres-export-real.png` when `FLAPPEDEAR_LAYOUT_REVIEW_DIR` is set). Private
results are in `telemetry-semantics.md`.

## KAN-180: content ids shared with FlappedEar Telemetry

`ContentIdVectorTests` recomputes every content id in
`native/tests/fixtures/content-id-vectors.json` with this repository's own
functions and compares the result byte for byte:
- `timingGateRevision` (`gates-v1`), 48 gate sets including west-positive
  longitudes, splits, and the unresolved cases (no start, two starts, an unknown
  gate, an invalid coordinate);
- `lapCompatibilityGroupId` (`compatibility-v1`), 15 track configurations;
- `trackSegmentSetRevision` (`track-segments-v1`), 23 segment lists;
- `EventProjectCodec::lapDerivationKey`, 3 runs, and `lapReferenceKey`, 2 lap
  references;
- Qt's compact JSON itself: 20 parse and write round trips and 1,333 doubles.

The file is a copy of FlappedEar Telemetry's
`packages/fetproject/test/fixtures/qt_hash_vectors.json` (FET-12), which was
generated by calling these functions at `0ec7416` with Qt 6.8.3. Telemetry
checks its Dart implementation against the same file, so a failure here means
saved exclusions, comparison slots or segments would stop matching between the
apps. A deliberate change needs a new id version agreed with the owner and
Telemetry (KAN-170). The vector `gate-revision-trailing-newline` expects no
compatibility group: since KAN-181 (4 October 2026) both apps reject a gate
revision followed by a newline, which Qt's `$` used to accept.

Checked on Linux with Qt 6.8.3 (Debug): the test passes, and altering one
recorded gate revision makes `gateRevisions` fail.

## KAN-218: .fetproject round-trip vectors

`ProjectVectorTests` reads `native/tests/fixtures/project-vectors/vectors.json`.
Each accepted case is a day as FlappedEar Telemetry saves it, with its
recordings: `telemetry-day.fetproject` carries every analysis field (lap
exclusions, comparison group, range and channels, run details, segments and
their review, newer versions of the versioned fields, unknown keys at the root,
event and run) and `event-demo.fetproject` is a minimal day with relative paths
only. The test copies a case into a temporary `day/` folder, opens it in the
reference analysis app (`TelemetryController`), saves it unchanged to the
case's `savePath` (in place, or Save As to `archive/2026/`) and compares the
result, as JSON values, with the expected file. The temporary folder in each
`absolutePath` becomes `/vectors`, and the generated `documentState.saveId`
(and `documentState.id` when the day had none) becomes `<generated-uuid>` once
checked to be a UUID. A second save of the saved day must give the same file,
and no save may change a run's `lapDerivationKey`. Three documents both apps
must refuse are validated too: root `sources` in v3, an event in a version 2
document, and a malformed version tag (`channel-fusion-vX`).

What the expected files pin, as of 6 October 2026: a save keeps every field
it does not own; Save As rewrites each `relativePath` (`../../day/first.vbo`)
and keeps `absolutePath`; a day without them gains `absolutePath`,
`documentState.id`, `savedRevision`, `mapSettings.providerId` `none` and
`exportSettings.quality` `high`.

FlappedEar Telemetry should copy this folder and run the same cases against
its Dart save (KAN-218 port; that repository is not reachable from here). A
deliberate change to how a day is saved regenerates the expected files with
`FLAPPEDEAR_UPDATE_PROJECT_VECTORS=1` and is agreed with the owner and
Telemetry, as for the content ids (KAN-170). Checked on Linux with Qt 6.8.3
(Debug): the test passes; removing an unknown key from an expected file fails
it.

## KAN-178: command-line modes

`native/src/app/CommandLine.cpp` decides the mode before Qt starts. A known
mode needs its exact argument count; any other argument that starts with `--`
prints the supported modes to stderr and exits with code 2, without opening the
editor or taking the session lock. Arguments without `--`, such as Qt's own
`-platform`, still open the editor.

- `flappedear_command_line_tests` (Qt Core only) checks every mode, unknown
  options, missing and extra arguments, and the usage text.
- `flappedear_command_line_usage` runs the application with `--export-test x y`
  and `--startup-smoke extra` and requires a non-zero exit and the usage text
  within 30 seconds; an editor that opened would time out.

Checked on Linux with Qt 6.8.3 (Debug): both tests and the startup smoke pass,
and the usage script fails against a program that exits 0 or prints nothing.

## KAN-175: sources trimmed with an edit list

`exportsEditListSourceThroughWorker` encodes a 4 s, 30 fps H.264 video with a
keyframe every second, then trims it at 1.5 s with `-c copy`. The trimmed MP4
has 90 video packets, but its edit list hides the first 15, so 75 frames are
presented. The test checks that `MediaProbe::probe` reports 75 frames, that the
full export range is frames 0 to 74, that the untrimmed source still reports
its header count of 120, and that a whole-video export through the worker
passes final validation with 75 frames.

Checked on Linux with Qt 6.8.3 (Debug) and conda-forge FFmpeg 8.1.2: before
the fix the worker failed final validation with "Video packet count expected=90
actual=75"; after it, the test passes.

## KAN-155: parser fuzzing

`native/fuzz/` holds one libFuzzer harness per untrusted input format. Each
feeds the input to the decoder Overlays uses and accepts any `std::exception`
as a rejection; running out of memory (`std::bad_alloc`), a sanitizer report, a
timeout or the RSS limit is a finding.

| Harness | Decoder | Input |
| --- | --- | --- |
| `flappedear_fuzz_vbo` | `VboParser::parse` | the bytes as UTF-8 text |
| `flappedear_fuzz_rcz` | `RczParser::parseFile` | the bytes written to one scratch file |
| `flappedear_fuzz_gpmf` | `GoProTelemetrySource::decodeGpsPackets` | byte 0 picks one to four packets; the rest is split between them |

The seeds in `native/fuzz/seeds/` are synthetic: the two VBO test fixtures, the
RCZ test archive compressed and stored, and a GPS9 and a GPS5 packet. The
harnesses need Clang with the libFuzzer runtime (`libclang-rt-18-dev` on
Ubuntu); `FLAPPEDEAR_BUILD_FUZZERS=ON` builds the whole project with coverage,
ASan and UBSan:

```bash
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 -DFLAPPEDEAR_BUILD_FUZZERS=ON
cmake --build build-fuzz --target flappedear_fuzz_vbo flappedear_fuzz_rcz flappedear_fuzz_gpmf
mkdir -p corpus/vbo && cp native/fuzz/seeds/vbo/* corpus/vbo/
build-fuzz/native/fuzz/flappedear_fuzz_vbo corpus/vbo -max_total_time=600 -max_len=1048576 \
  -timeout=30 -rss_limit_mb=2560
```

Native CI's `Linux x64 / Fuzz parsers / Qt 6.8.3` job runs each harness for
60 seconds from the seeds and uploads any crash input with its logs.
Since KAN-219 the inputs may grow to 1 MiB (`-max_len=1048576`, was 64 KiB), so
the fuzzer is no longer kept below the parsers' own header, line and channel
limits. libFuzzer still lengthens inputs gradually, so a 60-second run rarely
gets near that size; the limit tests remain the check of the limits themselves.

Checked on Linux with Qt 6.8.3, clang 18 and ASan, UBSan and leak detection,
10 minutes per harness on 5 October 2026: no finding. VBO ran 666,735 inputs
(peak RSS 488 MB), RCZ 1,386,886 (337 MB) and GPMF 2,862,813 (551 MB).
libFuzzer grows the input length slowly, so the VBO inputs stayed near the
seed size (414 bytes); the size limits themselves stay with the existing
limit tests, such as `boundsVboHeaderAndDecodedValues`.

## KAN-154: clang-tidy

`.clang-tidy` at the repository root enables `bugprone-*`, `clang-analyzer-*`
and `performance-*`. `bugprone-*` and `clang-analyzer-*` findings are errors;
`performance-*` and `bugprone-unchecked-optional-access` are advice. These
checks are off because, on this code base, they flagged only intended code:

| Check | Why it is off |
| --- | --- |
| `bugprone-easily-swappable-parameters`, `-narrowing-conversions`, `-implicit-widening-of-multiplication-result` | style, hundreds of hits |
| `bugprone-switch-missing-default-case`, `-assignment-in-if-condition`, `-empty-catch` | deliberate patterns (the empty catches carry a comment) |
| `bugprone-integer-division` | grid layout code divides on purpose, even inside `static_cast<double>` |
| `clang-analyzer-security.FloatLoopCounter` | tests step time in floating point on purpose |
| `clang-analyzer-cplusplus.NewDelete`, `-NewDeleteLeaks` | report inside Qt's `QSharedPointer` and `QObject` code; ASan covers these defects |
| `performance-enum-size`, `-implicit-conversion-in-loop` | no measurable gain |

`native/tests/.clang-tidy` also turns off the optional and null-pointer
checks: a test dereferences a value after `QVERIFY` has checked it, which the
analyzer cannot see.

Native CI's `Linux x64 / clang-tidy / Qt 6.8.3` job runs on pull requests
only. It builds with clang 18 for the compile database and the moc files, then
runs `clang-tidy-18` on each changed `native/**/*.cpp` file in that database.
Headers are not in the compile database, so since KAN-219 a changed
`native/**/*.h` adds its sibling `.cpp` file, or, when there is none, up to
three `.cpp` files that include it; clang-tidy checks the header through them.
Locally:

```bash
cmake -S . -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-18 \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-tidy
run-clang-tidy-18 -p build-tidy -quiet "$PWD/native/(src|tests)/"
```

The first run over `native/src` and `native/tests` found no defect; an injected
use-after-move fails the file with exit code 1. It flagged
two use-after-move false positives (a moved `QVector` reused after `clear()`,
now `std::exchange`) and an optional unwrapped and wrapped again
(`CornerSpeeds.cpp`); both are rewritten.

## KAN-153: editor UI invariants

Three `AGENTS.md` UI invariants now have automated tests:

- **Sidebar at the minimum** (`EditorTests::keepsSidebarReachableAtMinimumSize`). The editor
  window is opened at 1180×720 with a Retro Custom widget selected (the most inspector controls).
  For the WIDGET, DATA and CUES tabs, the test checks that the tab's scroll view lies inside the
  window and that no nested scroller holds content it cannot show. It then scrolls that one
  surface to each visible control and checks the control comes fully into view. The window is
  never larger than 1180×720; a smaller CI display only makes the check stricter.
- **Transport shortcuts while editing**
  (`EditorTests::disablesTransportShortcutsWhileEditing`). Space, Left, Right, Shift+Left,
  Shift+Right, Home and End are off while an inspector text field or spin box has focus, and
  come back when focus leaves it.
- **Very Verbose viewport** (`ExportTests::showsVeryVerboseDiagnosticsLive`). Dragging the
  scroll bar to the very end with the mouse no longer resumes following, even as new lines arrive;
  only Jump to latest does. This assertion failed before the fix, when a drag into the last 2 %
  re-attached the log.

The audit's other two items were already gone: the A/B comparison video left with the analysis
(KAN-166), and the Dial, Arc and Retro Grand Prix gauges were removed (KAN-192). The remaining
tachometers draw their faces on a separate canvas (KAN-199).
