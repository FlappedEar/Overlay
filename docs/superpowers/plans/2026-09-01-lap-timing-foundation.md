# Lap Timing Foundation Implementation Plan

> Historical design/implementation plan. Status and validation statements below describe the original planning checkpoint. For current implementation, verified exporter geometry and remaining release gates, use the [delivery ledger](../../product-delivery.md), [testing](../../testing.md) and [beta acceptance](../../beta-acceptance.md).


> **Status:** Production tasks 1–4 are implemented on `main`. This is a historical execution record, not an instruction to invoke Superpowers. Automated/macOS/private-fixture validation remains deferred.

**Goal:** Turn RaceChrono Start metadata and raw GPS samples into a usable lap list in the existing Analysis workspace, without requiring video, changing projects, or touching export.

**Architecture:** `VboParser` owns bounded source syntax and stores normalized timing gates on `TelemetrySession`. A pure telemetry-domain `LapDetector` derives immutable passages and laps from raw GPS/time. `AppController` runs the detector in the existing VBO worker and exposes a read-only view to a focused QML panel.

**Tech Stack:** C++20, Qt 6 Core/Concurrent/QML, Qt Quick, CMake.

**Spec:** `docs/superpowers/specs/2026-08-29-lap-timing-foundation-design.md`

## Global Constraints

- Work was completed directly on `main`; publication follows the active environment's repository policy.
- Preserve raw, time-based telemetry semantics. Never use FPS, video frames, render smoothing, or export cadence for lap detection.
- Missing GPS remains missing and breaks continuity; it is never interpolated across a source gap.
- Malformed or absent `[laptiming]` data must not prevent ordinary VBO loading.
- Source gates and derived laps are not persisted in `.fetproject` and do not mark the document dirty.
- Keep all detector logic out of QML and `AppController`.
- Do not touch export, widgets, templates, GoPro parsing, project recovery, or the accepted HUD.
- The user explicitly deferred automated and host-runtime tests. Do not claim compilation or runtime success until the later validation pass runs on the macOS Qt toolchain.
- Each task below produces one focused commit for one complete behavior, not one commit per file.

---

### Task 1: Structured RaceChrono timing gates

**Files:**
- Create: `native/src/telemetry/TelemetryGeometry.h`
- Create: `native/src/telemetry/TelemetryGeometry.cpp`
- Create: `native/src/telemetry/TimingGate.h`
- Modify: `native/src/telemetry/TelemetrySession.h`
- Modify: `native/src/telemetry/VboParser.h`
- Modify: `native/src/telemetry/VboParser.cpp`
- Modify: `native/src/telemetry/TrackGeometry.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces `GeoCoordinate`, `MetricPoint`, `TimingGateType`, and `TimingGate` telemetry value types.
- Produces `normalizeCoordinateDegrees(CoordinateAxis, double)` and `projectCoordinate(GeoCoordinate, GeoCoordinate)` shared helpers.
- Produces `TelemetrySession::timingGates` in original source order.
- Preserves current channel coordinate normalization through the same helper.

- [x] **Step 1: Add shared coordinate and timing-gate value types**

```cpp
enum class CoordinateAxis { Latitude, Longitude };

struct GeoCoordinate {
    double latitudeDegrees = 0.0;
    double longitudeDegrees = 0.0;
};

struct MetricPoint {
    double eastMeters = 0.0;
    double northMeters = 0.0;
};

enum class TimingGateType { Start, Split, Unknown };

struct TimingGate {
    TimingGateType type = TimingGateType::Unknown;
    QString sourceName;
    GeoCoordinate endpointA;
    GeoCoordinate endpointB;
    QString sourceDescription;
};
```

- [x] **Step 2: Extract coordinate normalization and local projection**

```cpp
std::optional<double> normalizeCoordinateDegrees(CoordinateAxis axis, double value);
MetricPoint projectCoordinate(const GeoCoordinate &coordinate, const GeoCoordinate &origin);
```

Return no value for non-finite or out-of-range degrees. Convert signed total arc-minutes only within ±5400 latitude and ±10800 longitude. Keep projection east/north; `TrackGeometry` alone negates north for screen Y.

- [x] **Step 3: Parse `[laptiming]` independently from metadata**

Parse exactly one name token, four finite numeric coordinate tokens in longitude/latitude order, and at most 4096 characters of trailing description. Classify `Start` and `Split` case-insensitively, retain unknown names, reject identical endpoints, cap accepted gates at 128, and add bounded parser warnings without failing telemetry.

- [x] **Step 4: Store gates on the parsed telemetry session**

```cpp
class TelemetrySession {
public:
    QVector<TimingGate> timingGates;
};
```

Do not add the gates to project serialization or source fingerprints.

- [x] **Step 5: Review the focused diff and commit**

```bash
git diff --check
git add native/src/telemetry native/CMakeLists.txt docs/superpowers/plans/2026-09-01-lap-timing-foundation.md
git commit -m "feat: parse RaceChrono timing gates"
```

---

### Task 2: Pure lap detection and derivation

**Files:**
- Create: `native/src/telemetry/LapTiming.h`
- Create: `native/src/telemetry/LapTiming.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes `TelemetrySession`, one normalized `TimingGate`, `LapDetectionOptions`, and `CancellationCheck`.
- Produces `LapSession detectLaps(...)` containing accepted passes, complete laps, fastest-lap index, status, and bounded diagnostics.

- [x] **Step 1: Add the immutable result model**

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
```

- [x] **Step 2: Implement bounded finite-segment geometry**

Calculate closest points between the vehicle segment and the finite gate segment, returning distance plus fractions on both segments. Reject non-finite geometry and gate lengths outside 1–200 m.

- [x] **Step 3: Implement the passage state machine**

Use the specified defaults: 5 m inner corridor, 10 m outer corridor, 2 m/s ground speed, 2 m/s absolute normal speed, 0.10 normal ratio, 1 s refractory interval, and 5 s maximum cluster duration. Start disarmed, discard clusters interrupted by gaps, require an outside observation before re-arming, and establish accepted direction from the first qualified passage.

- [x] **Step 4: Enforce cadence, cancellation, and resource bounds**

Use aligned raw latitude/longitude timestamps, `telemetryGapThreshold()`, cancellation checks at least every 256 segments, O(1) active-cluster state, and a hard maximum of 100000 accepted passes.

- [x] **Step 5: Derive complete laps and fastest state**

For `N` accepted passages, create exactly `max(0, N - 1)` positive-duration laps. Choose the earliest lap on an exact fastest-time tie and compute deltas from unrounded durations.

- [x] **Step 6: Review the focused diff and commit**

```bash
git diff --check
git add native/src/telemetry/LapTiming.h native/src/telemetry/LapTiming.cpp native/CMakeLists.txt
git commit -m "feat: derive laps from raw telemetry"
```

---

### Task 3: Atomic AppController lap publication

**Files:**
- Modify: `native/src/telemetry/TelemetrySession.h`
- Modify: `native/src/telemetry/TelemetrySession.cpp`
- Modify: `native/src/app/AppController.h`
- Modify: `native/src/app/AppController.cpp`

**Interfaces:**
- Produces `std::optional<double> telemetryToVideoTime(double, const SyncTransform &)`.
- Produces QML properties `lapTimingStatus` and `lapSummaries`.
- Produces `videoMillisecondsForTelemetryTime(double)` for safe lap-start seeking.
- Extends `VboLoadResult` with one `LapSession` committed with its matching session and track geometry.

- [x] **Step 1: Add the safe inverse sync boundary**

```cpp
std::optional<double> telemetryToVideoTime(double telemetryTime, const SyncTransform &transform);
```

Return no value when input or transform fields are non-finite or `timeScale <= 0`.

- [x] **Step 2: Run detection in the existing VBO worker**

Select a Start gate only when exactly one parsed Start exists. Return explicit no-source or ambiguous statuses otherwise. Call `detectLaps()` under the existing VBO cancellation token and store the result on `VboLoadResult`.

- [x] **Step 3: Commit and clear lap state atomically**

Assign `m_lapSession` only in the same accepted-generation path that assigns the parsed session and track geometry. Reset it whenever committed telemetry is cleared or replaced. Do not call `markPersistentChange()` for derived state.

- [x] **Step 4: Expose bounded QML summaries**

Each summary map contains `number`, `startTelemetryTime`, `durationSeconds`, `deltaToBestSeconds`, and `isBest`. Every non-available status exposes an empty list and one concise status string.

- [x] **Step 5: Expose safe lap-start conversion**

Convert telemetry seconds centrally, reject negative or non-finite video time, reject times after the actual loaded-media boundary when known, and return `-1` to QML when seeking is unavailable.

- [x] **Step 6: Review the focused diff and commit**

```bash
git diff --check
git add native/src/telemetry/TelemetrySession.* native/src/app/AppController.*
git commit -m "feat: publish derived lap timing"
```

---

### Task 4: Analysis lap list and lap-start seek

**Files:**
- Create: `native/qml/LapTimingPanel.qml`
- Modify: `native/qml/AnalysisPanel.qml`
- Modify: `native/qml/AnalysisWindow.qml`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes `appController.lapTimingStatus`, `appController.lapSummaries`, and `appController.videoMillisecondsForTelemetryTime()`.
- Emits `seekRequested(real milliseconds)` through the existing Analysis path.

- [x] **Step 1: Add a compact bounded lap panel**

Show columns `LAP`, `TIME`, and `DELTA`; format durations to one decimal place; show `BEST` in green for the fastest row and a signed delta for all other rows. Keep selection styling independent from fastest state.

- [x] **Step 2: Add explicit no-data states**

Show one concise status for missing timing metadata, ambiguous Start gates, invalid gate, unusable GPS, no accepted passes, or insufficient passes. Do not hide charts or the track map.

- [x] **Step 3: Route activation to main playback**

On row activation, call the controller conversion for `startTelemetryTime`; emit seek only for a non-negative result. Do not perform sync arithmetic in QML and do not create another media player.

- [x] **Step 4: Mount the panel above existing charts**

Keep the current Analysis workspace layout, decoder lifetime, chart behavior, and minimum dimensions. The panel receives a bounded preferred height and does not turn the existing chart area into an unreachable nested scroller.

- [x] **Step 5: Review the focused diff and commit**

```bash
git diff --check
git add native/qml/LapTimingPanel.qml native/qml/AnalysisPanel.qml native/qml/AnalysisWindow.qml native/CMakeLists.txt
git commit -m "feat: add lap timing to analysis"
```

---

### Deferred validation pass: macOS Qt tests and real fixture

This pass is deliberately deferred by the user and is not part of the four production commits above.

- Add deterministic parser, detector, sync-inverse, controller, and QML startup coverage from the matrix in the design specification.
- Add the optional `FLAPPEDEAR_REAL_LAP_VBO` integration slot for the private Jastrząb fixture.
- Verify 9093 samples, four accepted passes, three laps near 115.4/116.3/111.2 s, and Lap 3 fastest.
- Run `cmake --build build-native --parallel`, `ctest --test-dir build-native --output-on-failure`, host visual inspection, and the full end-to-end macOS workflow.
- Update README, architecture, telemetry semantics, testing docs, roadmap, and current-state claims only after those checks pass.
