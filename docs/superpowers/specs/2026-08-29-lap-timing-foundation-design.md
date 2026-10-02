# Lap Timing Foundation Design

> Historical design/implementation plan. Status and validation statements below describe the original planning checkpoint. For current implementation, verified exporter geometry and remaining release gates, use the [delivery ledger](../../product-delivery.md), [testing](../../testing.md) and [beta acceptance](../../beta-acceptance.md).


## Status / baseline

This document records the original architecture specification for the first Lap Timing & Session Analysis product slice. The production implementation now exists on `main`: source gates, raw-GPS passage/lap derivation, `LapSession` publication, Analysis lap list/seek, live best-lap comparison, and six independent lap/speed tiles are present. The deterministic/macOS/private-fixture validation pass remains deferred, so implementation presence must not be read as runtime acceptance.

The original baseline and scope language below is retained as design history. Later implementation extended the initial non-goals by adding live delta, lap traces, and lap/speed overlay tiles. The current product status is tracked in `docs/product-delivery.md` and `docs/telemetry-semantics.md`.

The source-of-truth product-code baseline is `0749eff31c87698a737cc62a1b54bd2fc9d1fb2c` (`fix: preserve Canvas widgets in offscreen export`). The documentation baseline is its exact child `9598e534d6baa960582a8e83a95be1f81b8d0193` (`docs: capture current development state`). This design is written on `feature/lap-timing-foundation` from that documentation baseline.

The existing native build and host-authorized Qt Test suite passed before this design was written. The private RaceChrono fixture was inspected separately with a read-only ad-hoc analysis; it was not added to the repository and was not exercised by an application integration test in this design iteration.

## Problem statement

FlappedEar Telemetry can parse raw telemetry, synchronize it to video, display raw analysis series, and render a static track, but it has no structured source timing gates or derived lap model. The next product slice must parse RaceChrono Start metadata, detect robust same-direction Start passages from raw GPS positions, derive complete laps between consecutive passages, identify the fastest lap, and expose a minimal lap list in the existing Analysis workspace.

Exact intersection between a sampled vehicle path segment and the finite timing-gate segment is not sufficient. The supplied real session contains four visually and physically consistent Start passages, yet none of its consecutive GPS segments intersects the finite Start segment mathematically. The detector must therefore model a bounded proximity corridor while still rejecting stopped, creeping, approximately parallel, reverse-direction, duplicated, and gap-bridged candidates.

Lap timing is a raw telemetry analysis. It uses raw telemetry timestamps and raw latitude/longitude samples. It must never depend on video FPS, frame numbers, widget interpolation, `TelemetryRenderContext` smoothing/holding, or export cadence.

## Real fixture findings

The private file used for the design audit is `session_20260829_172004_jastrząb_kaizenvtec.vbo`. The repository-local copy is ignored by `/*.vbo`; its SHA-256 matched the separately supplied external copy. Neither copy is committed.

Independently observed source facts:

- producer: RaceChrono Pro 10.2.4;
- session name: `Jastrząb Kaizen/Vtec`;
- 50 declared columns and 9,093 valid data rows;
- first timestamp: `15:20:04.42`;
- last timestamp: `15:35:13.62`;
- duration: 909.200 seconds;
- every adjacent timestamp interval: 0.100 seconds within floating-point representation;
- timing metadata: `Start   -1256.082320 +3075.802860 -1256.073959 +3075.793421 ¬ New trap`.

The four timing values are `longitude1 latitude1 longitude2 latitude2` in signed total arc-minutes. Applying the existing VBO normalization rule gives:

- endpoint A: latitude 51.263381000 degrees, longitude -20.934705333 degrees;
- endpoint B: latitude 51.263223683 degrees, longitude -20.934565983 degrees;
- local metric gate length: 20.0002 m using the same 6,371,000 m Earth-radius/equirectangular convention as current track geometry.

Exact finite-segment intersection detected zero crossings. A 5 m finite-segment proximity corridor produced exactly four contiguous passage regions. Nearest raw-sample observations were:

| Relative telemetry time | Distance to finite gate | VBO speed | VBO heading |
|---:|---:|---:|---:|
| 443.4 s | 1.411 m | 77.124 km/h | 38.730 deg |
| 558.8 s | 1.212 m | 58.590 km/h | 44.090 deg |
| 675.1 s | 1.660 m | 80.429 km/h | 37.550 deg |
| 786.3 s | 1.296 m | 84.651 km/h | 38.430 deg |

Minimum segment-to-gate closest approaches with sub-sample timing were 443.4434 s at 1.291 m, 558.8503 s at 1.052 m, 675.1153 s at 1.631 m, and 786.3535 s at 1.136 m. Consecutive differences reconstruct laps of 115.407 s, 116.265 s, and 111.238 s; Lap 3 is fastest.

The expected values supplied with the milestone are consistent with the file. Small differences in distance and speed arise because the supplied values refer approximately to the nearest 10 Hz sample, while the segment calculation minimizes distance and interpolates time within a segment. The second nearest-sample heading is 44.09 degrees, consistent with the supplied approximate 44 degrees.

One additional detector constraint came from the audit. Around each passage the position-derived vehicle motion is mostly along the gate tangent, but still has a consistent approximately 3–4 m/s component along one gate normal. A strict near-perpendicular crossing-angle rule would reject the real session. The policy must reject genuinely parallel motion using a minimum absolute normal speed plus a modest normal-motion ratio, not require the vehicle path to be close to the gate normal.

## Goals

The first implementation slice will:

1. Parse source-defined RaceChrono timing gates as normalized structured telemetry-source data.
2. Preserve multiple gate records and original source ordering even though only one unambiguous Start gate is used initially.
3. Detect robust Start passages using raw GPS segments and raw telemetry time.
4. Derive complete timed laps only between consecutive accepted same-direction Start passages.
5. Identify the fastest complete lap and compute each lap's delta to it.
6. Publish derived lap data through `AppController` to the existing Analysis workspace.
7. Show a minimal lap list containing lap number, formatted time, delta to best, and best-lap state.
8. Seek main playback to a selected lap's start through the central telemetry/video time transform.
9. Cover the subsystem with deterministic committed Qt tests and an optional private-fixture integration test.
10. Preserve ordinary VBO loading when timing metadata is absent or malformed.

## Non-goals

The first slice does not include sectors, theoretical best, live delta, lap widgets, reference-lap comparison, distance-normalized comparison charts, automatic sectors, interactive Start/Finish editing, project-persisted manual gates, a track database, online track lookup, circuit recognition, multi-GoPro chapters, Widget Runtime v2, `.fewidget`, VBOOverlay-Editor, export refactoring, or HUD redesign.

Calculated laps and source gates remain derived from the loaded telemetry source. The `.fetproject` schema does not change, and neither calculated laps nor source timing gates are persisted in this slice. Pre-first-pass and post-final-pass regions are not reported as complete timed laps.

## Existing architecture affected

- `VboParser` currently performs bounded section parsing, timestamp parsing, alias resolution, and signed arc-minute coordinate normalization. Non-column/data section lines without `:` or `=` become opaque metadata entries. It does not understand `[laptiming]` structurally.
- `TelemetrySession` is the immutable-after-load raw telemetry model. It owns raw channels, aliases, source metadata, warnings, sample count, duration, and raw time-based lookup. This is the appropriate lifetime for structured source timing definitions.
- `TrackGeometry` independently projects valid GPS values into a local metric plane and derives normalized static display geometry. Its projection formula is suitable to extract into a small shared telemetry-geometry helper, but normalized display points are not suitable lap-detector input.
- `AppController` runs VBO parsing and track construction on a QtConcurrent source worker under one cancellation token and generation, commits the resulting session atomically, and exposes raw analysis data to QML. Lap detection belongs in that worker pipeline after parse, not on the UI thread.
- `AnalysisWindow.qml`, `AnalysisPanel.qml`, and `TrackMapPanel.qml` form a restrained dark secondary workspace. Analysis window visibility is transient and the secondary decoder exists only while that window is open.
- `TelemetryTests.cpp` is the existing aggregate Qt Test suite. `native/tests/CMakeLists.txt` links it to `flappedear_core` and `AppController`; new pure lap files belong in `flappedear_core` and new test slots belong in the same suite.

No export, widget, project/recovery, GoPro, or FFmpeg ownership changes are required.

## Architecture options considered

### A. `VboParser` computes laps directly

This would put source syntax, raw telemetry construction, GPS geometry, passage state, and lap derivation in one parser. It could compute results during one pass through rows, but it would make parser behavior depend on detector options, complicate malformed-metadata recovery, couple reusable analysis to the VBO format, and make later recomputation with a manual gate difficult. It also conflicts with the current separation between `VboParser` and `TrackGeometry`.

### B. `TelemetrySession` carries source gates; a pure `LapDetector` returns derived data

This preserves parser ownership of source syntax and normalization, session ownership of source telemetry metadata, and a separate deterministic analysis boundary. The detector can be tested with synthetic `TelemetrySession` values, can share cancellation and local projection helpers, and can later accept a manual gate without changing VBO parsing. `AppController` remains orchestration and QML receives only a read-only derived view.

### C. `AppController` or QML computes crossings from track points

Current `TrackGeometry::points` are normalized display geometry with invalid samples removed and no per-point timestamps. Computing laps from them would lose raw gap and time semantics. Reconstructing the calculation in `AppController` or QML would couple UI state to geometry, encourage duplicated sync formulas, bypass pure cancellation/testing boundaries, and risk using smoothed presentation values.

## Selected architecture

Select option B.

Add structured timing-gate value types under `native/src/telemetry`, store parsed source gates on `TelemetrySession`, and add a pure `LapDetector` under the same module. Extract the local metric projection used by `TrackGeometry` into an internal shared telemetry geometry helper so track rendering and lap detection use one coordinate convention without sharing display-normalized point data.

`AppController::VboLoadResult` will carry the parsed `TelemetrySession`, `TrackGeometry`, and derived `LapSession`. The existing VBO worker will parse, build track geometry, select an unambiguous source Start gate, run lap detection, build the source fingerprint, and return one generation-bound result. Committing or rejecting that result remains atomic under the current source-generation rules.

`AppController` will own the committed derived `LapSession` for exactly the lifetime of the committed telemetry source. It will expose read-only lap summaries and status to QML. It will clear them on source clear, source replacement, or document-first project open before the external source resolves. Sync or video changes do not recompute raw lap detection.

## Source timing-gate model

Introduce value types equivalent to:

```cpp
enum class TimingGateType { Start, Split, Unknown };

struct GeoCoordinate {
    double latitudeDegrees = 0.0;
    double longitudeDegrees = 0.0;
};

struct TimingGate {
    TimingGateType type = TimingGateType::Unknown;
    QString sourceName;
    GeoCoordinate endpointA;
    GeoCoordinate endpointB;
    QString sourceDescription;
};
```

`TelemetrySession` gains a `QVector<TimingGate> timingGates` in source order. Canonical coordinates are finite degrees. `sourceName` retains the first token's spelling; `sourceDescription` retains at most 4,096 characters after the fourth coordinate, such as `¬ New trap`, for diagnostics only. Neither field controls detection except the normalized `TimingGateType` classification.

The parser accepts up to 128 timing gates. Once that bound is reached, it ignores further timing lines and emits one capped parser warning rather than allocating without limit or failing otherwise valid telemetry. Unknown gate names can be represented as `Unknown`; this keeps the source model extensible without claiming they are sectors.

The first slice selects a Start gate only when exactly one valid `TimingGateType::Start` exists. No Start produces a no-source-gate status. Multiple Start gates produce an ambiguous-source-gate status and no derived laps; the first slice does not silently choose among them. Future manual selection can resolve that ambiguity without changing the parser model.

## RaceChrono `[laptiming]` parsing grammar

Section matching follows the current case-insensitive, trimmed VBO section behavior. Each non-empty, non-comment line in `[laptiming]` is parsed independently as:

```text
gate-line       = gate-name whitespace number whitespace number whitespace number whitespace number trailing-text?
gate-name       = one non-whitespace token
number          = a finite signed decimal accepted by QString numeric conversion
trailing-text   = optional whitespace followed by bounded uninterpreted source text
coordinate order = longitude1 latitude1 longitude2 latitude2
```

For the real line:

```text
Start   -1256.082320 +3075.802860 -1256.073959 +3075.793421 ¬ New trap
```

`Start` classifies case-insensitively as `TimingGateType::Start`. The four numeric tokens are longitude A, latitude A, longitude B, and latitude B. `¬ New trap` is optional trailing source description, not a fifth coordinate or a detection directive.

Coordinate conversion must call the same helper as telemetry-column conversion:

- latitude with absolute value greater than 90 and at most 5,400 is signed total arc-minutes and is divided by 60;
- longitude with absolute value greater than 180 and at most 10,800 is signed total arc-minutes and is divided by 60;
- otherwise the finite value is already treated as degrees.

After normalization, latitude must be within -90 to +90 and longitude within -180 to +180. Both endpoints must be finite and distinct. A line with a missing token, invalid or non-finite number, out-of-range coordinate, identical endpoints, or unbounded field is ignored and produces a bounded parser warning identifying the timing line. Other telemetry remains loadable. Absence of `[laptiming]` preserves current behavior with an empty `timingGates` vector and no new warning.

The parser does not compute gate length, crossings, or laps. It only creates structurally valid normalized source records.

## Coordinate / local metric geometry

Introduce one internal local projection used by `TrackGeometry` and `LapDetector`. Its origin is the timing-gate midpoint. For latitude `lat`, longitude `lon`, origin `lat0`, `lon0`, and Earth radius 6,371,000 m:

```text
xEast  = radians(lon - lon0) * R * cos(radians(lat0))
yNorth = radians(lat - lat0) * R
```

`TrackGeometry` currently negates north for screen Y; the shared geographic projection should remain east/north. Track display code can negate Y when creating screen-local geometry. Lap detection uses east/north directly. This preserves the current distance convention while making direction signs explicit.

The detector validates projected values and gate length before processing samples. Default accepted gate length is 1–200 m. An invalid gate returns `InvalidGate` with no passes; it does not invalidate the source session.

The detector never consumes `TrackGeometry::points`: those points omit invalid rows, are display-normalized, and do not carry timestamps.

## Gate-pass detector

### Public boundary

The pure boundary is equivalent to:

```cpp
LapSession detectLaps(
    const TelemetrySession &session,
    const TimingGate &startGate,
    const LapDetectionOptions &options = {},
    const CancellationCheck &cancelled = {});
```

It depends only on telemetry-domain value types and `SourceOperation.h`. It has no QML, video, FFmpeg, `WidgetModel`, export, or project dependency.

### Bounded defaults

Initial defaults are:

- inner gate-corridor radius: 5.0 m;
- outer re-arm radius: 10.0 m;
- minimum position-derived ground speed: 2.0 m/s;
- minimum absolute gate-normal speed: 2.0 m/s;
- minimum absolute normal-motion ratio: 0.10 of total motion;
- short post-event refractory interval: 1.0 s;
- maximum active passage-cluster duration: 5.0 s;
- valid gate length: 1–200 m;
- maximum accepted gate passes: 100,000.

Options are validated before analysis: values must be finite, non-negative where appropriate, the outer radius must exceed the inner radius, the normal ratio must be within 0–1, and bounds must stay below conservative hard maxima (50 m inner radius, 100 m outer radius, 1,000 m gate length, and 10 s refractory interval). Invalid options throw `std::invalid_argument` before scanning because they are a programmer/configuration error, not a source-data status; they never cause unbounded work.

The 5 m default has margin over the observed 1.1–1.7 m misses and is not tuned to 1.5 m. The 2 m/s normal-speed and 0.10 ratio defaults accept the independently measured 3–4 m/s consistent normal component while rejecting stationary noise and truly parallel travel. Deterministic tests must lock these intended boundaries without hard-coding private coordinates.

### Raw GPS pairing and gap policy

Resolve the raw latitude and longitude channels through `TelemetrySession::aliases`. A usable GPS sample requires matching finite timestamps, finite coordinates, and normalized degree bounds. VBO parser output has aligned channel timestamps; the detector nevertheless treats size or timestamp mismatch as a continuity break rather than interpolating one channel onto the other.

Consecutive samples form a vehicle segment only when timestamps are strictly increasing and their interval is no larger than three times the median positive interval of the paired GPS stream. The implementation may reuse the existing cadence statistic where the channels are aligned. A non-finite sample, missing coordinate, timestamp mismatch, backward/equal time, or larger gap terminates and discards any active passage cluster, disarms detection, and requires a later valid outside-outer-corridor observation before re-arming. No segment is constructed across a gap.

### Candidate geometry and sub-sample time

For each usable consecutive GPS pair:

1. Project both vehicle points and both gate endpoints into the local metric plane.
2. Compute the closest points between the finite vehicle segment and finite gate segment, returning distance, fraction `u` along the vehicle segment, and fraction `v` along the gate.
3. Mark the segment as inside the inner corridor when closest distance is at most 5 m. The finite gate includes its endpoints; proximity to an endpoint is valid and is essential for the real fixture.
4. Store candidate telemetry time as `t0 + u * (t1 - t0)` and retain closest distance and gate fraction as diagnostics.

Exact intersection is the zero-distance special case of this geometry, not a separate required path and not the sole criterion.

### Motion and parallel-travel policy

Motion qualification is calculated from raw positions, never from the VBO heading channel. The gate tangent is the normalized vector from endpoint A to endpoint B; one perpendicular vector defines the signed gate normal.

For a proximity cluster, use aggregate displacement from the first usable vehicle point in the cluster to the last usable point, divided by aggregate time. This reduces 10 Hz single-segment jitter. The cluster is motion-qualified only when:

- total ground speed is at least 2.0 m/s;
- absolute normal speed is at least 2.0 m/s; and
- absolute normal speed divided by total ground speed is at least 0.10.

Stationary or creeping data therefore cannot generate a pass. Exactly or nearly parallel travel with no meaningful gate-normal progress is rejected. The policy deliberately does not require near-perpendicular travel because the real accepted path has a modest normal component while moving predominantly along the gate tangent. Source `velocity` and `heading` values may be copied into diagnostics at the closest time when available, but neither is required for acceptance.

### Candidate clustering, hysteresis, and re-arm

The state machine begins disarmed. A valid continuous segment outside the outer 10 m corridor arms it; this prevents a recording that starts on the line from manufacturing an immediate pass.

When armed detection enters the 5 m inner corridor, it opens one passage cluster. While the path remains within the 10 m outer corridor, all contiguous proximity segments belong to that one cluster. The detector stores only constant-size aggregate state and the minimum-distance candidate; it does not retain every segment. Excursions between 5 and 10 m do not split the physical passage.

When the path leaves the outer corridor, the cluster is finalized. It emits at most one pass if it has a finite minimum candidate, satisfies the aggregate motion policy, lasted no more than 5 seconds, and satisfies direction policy. A cluster interrupted by a GPS gap is discarded rather than finalized. After finalization the detector is disarmed until it has remained outside the outer corridor and the 1 second refractory interval since the last accepted event has expired.

This outer-corridor hysteresis is the primary duplicate protection. The short refractory interval is a secondary guard against numerical oscillation; it is not a guessed minimum lap time and cannot replace re-arming.

### Direction establishment

The first motion-qualified finalized passage establishes the session's accepted direction from the sign of aggregate displacement dotted with the chosen gate normal. The sign itself depends on endpoint ordering and has no product meaning beyond consistency.

Later passages must have the same sign. An opposite-direction cluster is counted in diagnostics but not appended to accepted `GatePass` values, does not change the established direction, and cannot create a complete lap. A future manual gate may provide explicit direction, but source RaceChrono Start metadata in this fixture does not.

### Cancellation and resource bounds

Call `throwIfCancelled(cancelled)` before analysis and at least every 256 GPS segments, during any cadence scan, and before returning. Cancellation throws `OperationCancelled` exactly like VBO parsing and track construction. The AppController worker treats that as a cancelled source operation and does not commit partial lap state.

The algorithm is O(n) after cadence calculation and uses O(1) active-cluster state plus bounded accepted/rejected diagnostics. At 100,000 accepted passes it throws `ResourceLimitError`, following current telemetry-source conventions; it never grows an unbounded result vector even if input reaches the existing 500,000-row VBO limit.

## Lap derivation

Accepted `GatePass` values are emitted in strictly increasing raw telemetry time. For `N` accepted same-direction passes, derive exactly `max(0, N - 1)` complete laps:

```text
lap i start = pass i telemetry time
lap i end   = pass i+1 telemetry time
duration    = end - start
```

Lap numbers are one-based in presentation. A duration must be finite and positive; an impossible non-positive pair produces a detector diagnostic and no lap across that pair. There is no long fixed minimum-lap debounce in the first slice.

The portion before the first accepted pass is an untimed pre-start region. The portion after the final accepted pass is an untimed post-finish region. Neither is inserted into `timedLaps`. Four accepted passages therefore always produce three complete timed laps.

The fastest lap is the complete lap with the smallest duration; an exact tie selects the earlier lap deterministically. Each lap's delta is `duration - fastestDuration`, with the best lap represented as zero. Display rounding does not affect fastest selection.

## Derived session model

Introduce values equivalent to:

```cpp
enum class LapSessionStatus {
    Available,
    NoSourceStartGate,
    AmbiguousSourceStartGate,
    InvalidGate,
    NoUsableGps,
    NoAcceptedPasses,
    InsufficientPasses
};

struct GatePass {
    double telemetryTime = 0.0;
    double closestDistanceMeters = 0.0;
    int direction = 0;
    double gateFraction = 0.0;
    double groundSpeedMetersPerSecond = 0.0;
    double normalSpeedMetersPerSecond = 0.0;
};

struct TimedLap {
    int number = 0;
    double startTelemetryTime = 0.0;
    double endTelemetryTime = 0.0;
    double durationSeconds = 0.0;
    double deltaToBestSeconds = 0.0;
};

struct LapSession {
    LapSessionStatus status = LapSessionStatus::NoSourceStartGate;
    std::optional<TimingGate> selectedStartGate;
    QVector<GatePass> acceptedPasses;
    QVector<TimedLap> timedLaps;
    std::optional<qsizetype> fastestLapIndex;
    LapDetectionDiagnostics diagnostics;
};
```

These are value objects built once by the detector and treated as immutable after publication. `LapSession` owns a copy of the selected normalized source gate so its result is self-describing. Diagnostics contain bounded counters and reason codes, not private paths or full source rows.

`Available` means at least one complete timed lap exists. One accepted pass yields `InsufficientPasses`; zero yields `NoAcceptedPasses`. QML receives an empty lap list for every non-available state, while the controller retains the diagnostic status for an explanatory no-data label and logs.

## Telemetry/video time boundary

All `GatePass` and `TimedLap` boundaries are stored in raw telemetry seconds relative to the parsed session origin. Detector behavior is independent of video and synchronization.

Current forward conversion remains:

```text
telemetryTime = videoTime * timeScale + offset
```

Add a central safe inverse helper beside `videoToTelemetryTime`:

```text
videoTime = (telemetryTime - offset) / timeScale
```

The helper returns no value for non-finite input, non-finite transform fields, or `timeScale <= 0`. `AppController` exposes a QML-safe conversion method and clamps or rejects the result at the actual media boundary. QML must not reimplement the formula.

Selecting a lap converts only its stored `startTelemetryTime` through this helper and forwards the resulting video milliseconds through the Analysis window's existing `seekRequested` path to main playback. Sync changes alter the seek mapping but do not alter raw lap duration or rerun detection. FPS is irrelevant because neither transform contains frames or cadence.

## AppController integration strategy

1. Extend `VboLoadResult` with a `LapSession`.
2. On the existing source worker, parse the VBO, build `TrackGeometry`, select the unique source Start gate, and call `LapDetector` with the same `CancellationCheck`.
3. Preserve current cancellation and exception handling. A cancelled detector cancels the whole candidate source result; malformed or absent timing metadata returns a no-data lap status while the parsed telemetry remains successful.
4. Preserve the current fingerprint contract in the first slice. Source gates and calculated laps do not enter the existing telemetry fingerprint unless a separate compatibility decision is made later; the sampled source-byte digest already changes if the timing text changes in a sampled region, while structural fingerprint fields remain stable.
5. Commit session, track geometry, and lap session together only after current generation/fingerprint checks pass.
6. Add read-only properties for lap summary rows and lap-timing status, notified with `telemetryChanged` or a dedicated `lapTimingChanged` emitted at the same atomic commit.
7. Add a QML-invokable safe telemetry-to-video conversion method. Keep MediaPlayer ownership and actual seek signaling in QML as today.
8. Clear derived lap state whenever committed telemetry is cleared or replaced. Do not persist it in `currentProjectObject()` and do not mark the document dirty when it is derived.

Lap detection executes once per telemetry load/relink/project-source resolution. Opening or closing Analysis does not trigger recomputation. Video load, sync editing, playback, preview rendering, and export do not mutate `LapSession`.

## Analysis UI strategy

Add a focused `LapTimingPanel.qml` to the top of the existing right-side `AnalysisPanel` column. It uses current dark colors, compact typography, borders, and spacing; it does not redesign `AnalysisWindow`, `TrackMapPanel`, charts, transport, or main workspace.

The panel shows a compact header and bounded list:

```text
LAP    TIME       DELTA
1      1:55.4     +4.2
2      1:56.3     +5.0
3      1:51.2     BEST
```

Rows receive numeric duration/delta and `isBest` from the controller-derived summary. QML formats one decimal place for this first 10 Hz source slice; the domain model retains full derived precision. The fastest row uses the existing restrained green accent and `BEST`; other rows show a signed delta. Selection has a subtle current-row background independent of fastest state.

Activating a row requests a seek to its start through the controller's safe inverse transform and the existing Analysis-to-main `seekRequested` signal. Selection does not change lap timing or synchronize a second playback model. If conversion is unavailable or outside loaded media, activation is disabled or safely rejected with no seek.

When there is no unique source Start, no usable GPS, or fewer than two accepted passages, the area shows one concise status and no manufactured lap rows. Ordinary charts and track analysis remain available.

## Error/no-data behavior

- No `[laptiming]`: telemetry loads normally; timing gates and lap rows are empty.
- Malformed timing line: that line is ignored, one bounded parser warning is added, and valid telemetry still loads.
- Multiple valid Start gates: source gates remain visible in the model, but first-slice lap detection is not run and status is ambiguous.
- Invalid or implausible gate geometry: no laps; telemetry and track remain available.
- No latitude or longitude alias, no aligned usable coordinate pairs, or all invalid coordinates: `NoUsableGps`, no crash.
- A missing/non-finite coordinate or meaningful timestamp gap breaks continuity and discards an active candidate.
- Low-speed, excessive-duration, insufficient-normal-motion, or opposite-direction clusters are rejected with bounded diagnostic counters.
- Zero or one accepted same-direction pass creates zero complete laps.
- Cancellation prevents any partial `LapSession` from committing and follows existing source-operation status behavior.
- A detector failure must not route through export diagnostics or alter project/recovery state.

## Private fixture strategy

Add one optional Qt Test slot driven only by `FLAPPEDEAR_REAL_LAP_VBO`. Do not reuse `FLAPPEDEAR_REAL_VBO`, whose current test assumes a different generic private session. If the new variable is unset, the test skips explicitly.

For the supplied fixture, assert:

- parse succeeds with exactly 9,093 samples and duration 909.2 s within 0.001 s;
- exactly one Start gate is present;
- normalized endpoints match the independently observed degrees within `1e-7` degree;
- gate length is 20.0 m within 0.1 m;
- exact finite intersection count is zero in a diagnostic/helper assertion if retained;
- default robust detection accepts four same-direction Start passages;
- it derives exactly three complete timed laps;
- durations are within 0.3 s of 115.4, 116.3, and 111.2 s;
- Lap 3 is fastest.

Passage telemetry times may also be checked within 0.3 s of 443.4, 558.8, 675.1, and 786.3 s. These tolerances reflect a 10 Hz GPS timeline and the first sub-sample estimator; they do not imply transponder or millisecond accuracy.

The optional test is private integration evidence, reported separately from committed deterministic coverage. The file stays local and ignored, and no absolute user path appears in source, tests, or documentation.

## Deterministic test matrix

Committed synthetic Qt tests must cover:

1. A `[laptiming]` Start line parses into one normalized `TimingGate`.
2. The parser uses longitude/latitude ordering and the same signed arc-minute normalization as GPS columns.
3. A malformed Start line is ignored, emits a warning, and does not prevent telemetry loading.
4. Absence of `[laptiming]` preserves ordinary VBO behavior without warnings or gates.
5. A normal exact finite-line crossing emits one passage.
6. A path missing finite intersection by approximately 1–2 m but entering the corridor emits one passage.
7. Several 10 Hz segments near the gate cluster into exactly one passage.
8. Stationary and very-low-speed samples near the gate emit no passage and cannot repeatedly lap.
9. Motion inside the corridor with normal speed/ratio below thresholds is rejected as parallel.
10. A later opposite-direction return is rejected and creates no same-direction lap.
11. Four accepted same-direction passages derive exactly three complete laps, with pre/post regions excluded.
12. A non-finite/missing GPS sample prevents a synthetic bridge.
13. A timestamp gap above the cadence threshold prevents a synthetic bridge.
14. A large synthetic session cancels deterministically within a bounded number of checks.
15. Missing or unusable latitude/longitude produces no lap timing and no crash.

Also cover: first-event direction establishment; inner/outer hysteresis; recording start inside the corridor; re-arm after leaving the outer corridor; short refractory behavior; gate endpoint proximity; invalid gate length/options; multiple and ambiguous Start gates; multiple non-Start gates preserved in source order; trailing `¬ New trap` text; fastest-lap tie policy; safe inverse sync conversion; AppController generation rejection/clear behavior; lap list mapping; and QML startup with each no-data status.

Private coordinates, sample rows, and expected private lap times must not appear in committed synthetic fixtures except in the optional environment-driven assertions.

## Documentation changes for implementation

When the first slice is implemented and validated:

- update `README.md` current capabilities and private-fixture variables;
- update `docs/architecture.md` with source-gate, detector, worker, and Analysis flow;
- update `docs/telemetry-semantics.md` with raw lap-time and gate/gap/direction semantics;
- update `docs/testing.md` with deterministic lap coverage and `FLAPPEDEAR_REAL_LAP_VBO`;
- update `ROADMAP.md` checkboxes only for completed behavior;
- leave `docs/project-format.md` unchanged except for an explicit statement that derived laps/source gates are not persisted if clarification is useful;
- keep `AGENTS.md` limited to durable invariants and add nothing unless implementation reveals a genuinely permanent rule;
- update `currentstate.md` only as a later checkpoint, never to describe design as implemented behavior.

## Rollout / phases

### Phase 1: source model and parser

Add shared coordinate normalization, `TimingGate`, bounded `[laptiming]` parsing, warnings, and parser tests. Ordinary VBO parsing remains the acceptance gate.

### Phase 2: pure geometry and detector

Add local projection sharing, closest-segment geometry, state machine, direction/hysteresis/gap/cancellation logic, lap derivation, and the complete deterministic detector matrix.

### Phase 3: application integration

Run detection inside the current VBO worker, commit derived state atomically under generation/fingerprint checks, expose read-only summaries/status, and add the safe inverse sync helper.

### Phase 4: Analysis UI

Add the compact lap panel, no-data states, best/delta formatting, and lap-start seek through the existing main playback signal. Preserve current decoder lifetime and chart behavior.

### Phase 5: real-fixture and documentation validation

Run the optional `FLAPPEDEAR_REAL_LAP_VBO` test, compare passage/lap results with the documented tolerances, run the full local build/CTest gate, inspect the Analysis behavior in the real host application, and update current-behavior documentation.

Each phase remains reviewable, but the implementation plan may group changes into focused commits only after this design is approved.

## Future extension points

- A future manual/project Start/Finish definition can override source timing metadata by passing a different `TimingGate` to the same detector. That feature will require a deliberate `.fetproject` schema design and is not part of this slice.
- Source Split/Sector gates can reuse the structured gate model later. Sector derivation, automatic sector generation, sector deltas, and theoretical best are not designed here.
- Reference-lap selection, distance-normalized comparison, and live delta can consume immutable lap telemetry boundaries later, but require a separate lap-distance model.
- A lap overlay can consume a validated session model later. No widget, manifest, template, or export work is introduced now.
- A track database or circuit-recognition layer could provide alternate gates later through the same detector input boundary without entering `VboParser`.

## Risks

- The real path approaches a finite gate endpoint and has a modest normal component. Thresholds that demand near-perpendicular travel will regress the known session; thresholds that accept any endpoint proximity risk nearby-road false positives. The combined corridor, absolute normal speed, motion ratio, direction, and hysteresis tests are all required.
- GPS noise can create apparent motion while stopped. Aggregate-cluster motion, minimum speed, maximum cluster duration, and outer re-arm reduce this risk, but broader private fixtures are needed before claiming general track coverage.
- Establishing direction from the first accepted pass assumes that first candidate is genuine. Strong motion qualification and recording-start disarm reduce the risk; future manual direction can override it.
- RaceChrono may emit gate-name variants or multiple Start definitions not present in this fixture. Unknown records are preserved, ambiguity is explicit, and telemetry remains usable.
- Current latitude/longitude channels are aligned for VBO parser output. The detector must still reject mismatched raw channel shapes rather than interpolate or read out of bounds.
- Sub-sample closest-time estimation improves repeatability but does not create accuracy beyond the source GPS/cadence. UI and documentation must avoid millisecond/transponder claims.
- Adding lap work to the source worker increases load latency. O(n) bounded processing and cooperative checks should keep this controlled; implementation should measure the optional real fixture and a 500,000-row synthetic case.
- `AppController` is already large. This feature should add only orchestration and QML mapping there; detector logic must not migrate into it, and unrelated controller decomposition remains out of scope.

## Acceptance criteria

The first implementation slice is complete only when all of the following are true:

- RaceChrono Start metadata is structured, degree-normalized, bounded, and tolerant of malformed/absent metadata.
- Parser and telemetry session retain multiple gate records without computing laps.
- A pure cancellable C++ detector consumes raw session GPS/time plus a selected gate and returns deterministic derived values.
- Exact finite intersection is supported but is not the core/sole criterion.
- The detector uses a 5 m default corridor, finite-segment closest approach, sub-sample candidate time, clustering, outer hysteresis, low-speed rejection, normal-motion rejection, same-direction enforcement, raw-gap breaks, and resource bounds as specified.
- Four accepted passes produce exactly three timed laps; outlap/inlap fragments are not manufactured.
- Fastest lap and deltas use unrounded raw durations.
- Lap state is derived, is not written to `.fetproject`, and does not mark the document dirty.
- Sync affects only telemetry-to-video seek conversion; FPS, frames, overlay smoothing, and render hold behavior do not affect lap results.
- The existing Analysis workspace shows the minimal list and seeks main playback to a selected lap start without redesigning the workspace.
- All deterministic matrix cases pass in committed Qt tests.
- The optional private test, when run with the supplied fixture, yields four passes, three laps within 0.3 s of 115.4/116.3/111.2 s, and Lap 3 fastest.
- Production export, widgets, templates, project/recovery semantics, and current preview/export scene architecture are unchanged.
- `cmake --build build-native --parallel`, `ctest --test-dir build-native --output-on-failure`, and `git diff --check` pass; private-fixture evidence is reported separately.
