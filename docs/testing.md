# Testing

## Normal local gate

```bash
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
```

Run the local gate appropriate to the change before claiming a behavior works. Cloud CI is enabled for macOS Debug and Release by owner direction; Windows remains paused. Follow the current [local task workflow](development-workflow.md).

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
`native/src/app/AppControllerSegmentReview.cpp` and the UI is
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
is in `AppControllerSegmentReview.cpp` and the editor is the approved-segment
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
  (`AppController::formatElapsedTime`).

The VBO parser still records no channel units: RaceChrono declares them in
`[header]`, but channel units are part of the recording fingerprint, so
adding them would ask every saved project to relink its recordings. Speeds
therefore show without a unit.

Opt-in real-day check (recordings stay out of Git; screenshots go to a local
directory):

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
then exercises the existing welcome, scene and startup-error paths. Release
packaging validates the renamed executable and bundle name, the stable bundle ID
and the unchanged absence of document/URL registration before SDK-isolated startup.
The `.fetproject` schema and File > Open Project workflow remain unchanged.
Windows naming edits are not Windows execution evidence.

## Cloud CI

**Resumed by owner on 13 September 2026 after the quota pause.** Verify each
published change against its own CI run; historical task records do not validate
new code. Keep the [local task workflow](development-workflow.md) for implementation
and private-media acceptance.

[Native CI](../.github/workflows/build.yml) runs on pull requests, pushes to `main`, and manual dispatch. Two macOS arm64 jobs configure Debug and Release Ninja builds with Qt 6.8.3 (the supported minimum minor), compile the application and tests with C++20, and run the complete CTest registration: the application, RCZ/parser, import, event-project, GPS-lap-eligibility and export-log suites, plus production QML startup smoke. Release jobs additionally deploy Qt and run installed startup with the build SDK hidden, then attach internal candidate archives. Windows builds, tests and installer validation are paused by owner direction on 13 September 2026; resume them only when explicitly requested. Earlier Windows results below are historical. See [Windows installer](windows-installer.md).

| Job | Renderer | Toolchain |
| --- | --- | --- |
| `macOS arm64 / Debug or Release / Qt 6.8.3` | Metal; Cocoa for native window interaction | `macos-15`, Apple Clang |

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
"build-native/native/Flapped Ear Telemetry.app/Contents/MacOS/Flapped Ear Telemetry" \
  --render-visual-smoke docs/assets/motorsport-broadcast-acceptance.png
"build-native/native/Flapped Ear Telemetry.app/Contents/MacOS/Flapped Ear Telemetry" \
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
  ./build-native/native/tests/flappedear_native_tests automaticallyGroupsPrivateTrackDay
```

It expects multiple recordings of one compatible route and direction and checks
automatic rankings/progression for every run. The repo-root `jastrzab/` directory
is ignored; never add private sessions to the repository. Optional
`FLAPPEDEAR_DAY_REVIEW_PROJECT` and `FLAPPEDEAR_DAY_REVIEW_IMAGE` paths write a local
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
- Widget, group, cue, template, and track-geometry behavior, including central semantic normalization across mutation, scene import, template application/import/reload, finite geometry, color/default recovery, cue repair, and duplicate-ID rejection; custom-template create/update/reload/rollback behavior; classic/F1/bar G-Force defaults and persistence; optional font-setting serialization and duplication; a 30,000-point cache benchmark; same-address geometry invalidation; cache clearing; marker movement; and a QML guard against time-driven static-path reconstruction. A production-RHI-backed pixel regression renders `retroTachometer`, `arcGauge`, `dialGauge`, `retroGrandPrix`, and `retroSpeedArc` through the offscreen lifecycle. It requires Canvas-colored pixels in the first non-zero-range frame and a later frame, and requires the first frame to match a repeated render at the same requested telemetry time.
- Project atomic-save behavior; authoritative startup loading; QSettings document-key retirement; dirty-state actions for Quit, New, and Open; explicit saved and project-less recovery/discard; failed-save recovery retention; recovery v2 validity (newer, equal, older, malformed, and identity-mismatched snapshots), stale-cleanup retry after simulated deletion failure, and both new-project and existing-project Save As. Recovery-discard tests inject deletion and tombstone-write failures: Quit/New/Open continue after durable intent, Cancel leaves recovery untouched, startup removes a suppressed residual, newer same-identity and other-identity snapshots remain recoverable, dual failure cancels discard, legacy v1 ignores tombstones, and cleanup debt never sets `recoveryDegraded`; unknown-field preservation and v2 migration; project-relative source serialization and whole-folder moves; independent missing video/VBO states; deterministic fingerprints and sampled-byte mismatch; valid/invalid relink candidates; explicit mismatch replacement; stale relink rejection; and bounded controller shutdown.
- Export-output transaction safety, including native regular-file identity capture, unchanged replacement, in-place modification/replacement/disappearance refusal, new-target appearance refusal, and symlink rejection where the host permits link creation; plus export progress/diagnostics parsing, durable export-log creation/append/retention, media probing, exact rational rate comparison, and HEVC encoder detection.
- Source-media coverage for 8-bit, 10-bit, unknown depth, Rec.709, HLG/PQ/Log classification, coded/display raster, rotation/SAR retention plus export-only rejection for non-zero rotation and non-square SAR, arbitrary 5.3K and 8:7 rasters, non-heavy 8K representation, aspect-preserving downscales, checked RGBA sizing, continuous bitrate, centralized export profiles, renderer rejection, and encoder capability-cache keys. A deterministic FFmpeg composition proves that a Rec.709 10-bit source remains HEVC Main10/yuv420p10 with audio. On macOS with VideoToolbox available, a distinct-color full-range BT.709 fixture runs through the shared production Stage B graph and final HEVC encoder, software-decodes RGB, and enforces a per-channel mean absolute error of at most 12.
- Portable storage resolution coverage for existing files/directories, future files, nested future paths, and unavailable inputs; injectable multi-volume preflight and measured-sample/fallback/margin/overflow regressions.
- Portable raw-frame transport helper coverage for exact 1920×1080 and 3840×2160 RGBA frame transfers to a slow consumer, early consumer exit, sustained stall, prompt cancellation, and bounded queue size.
- Malformed/owned/live/idempotent manifest recovery rules, injected cancellation-marker write failure with synchronous supervised worker shutdown, and a macOS/Unix helper child/grandchild process-tree shutdown integration test. Windows Job Object setup errors are explicit. Windows runtime/export is validated on one known Windows 11 / Qt 6.11 / MSVC 2022 / Intel Iris Plus / Quick Sync configuration; native Windows ACL-denied coverage remains pending because `QFile::setPermissions()` does not model Windows ACL denial reliably.
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
