# Integer Frame Export Implementation Plan

> Historical design/implementation plan. Status and validation statements below describe the original planning checkpoint. For current implementation, verified exporter geometry and remaining release gates, use the [delivery ledger](../../product-delivery.md), [testing](../../testing.md) and [beta acceptance](../../beta-acceptance.md).


> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make export scheduling and range selection frame-addressed so a source with N usable video frames schedules exactly N frames, and retain a valid completed export with a small terminal deficit.

**Architecture:** `MediaProbe` retains source timing as integer stream ticks and determines a usable video-frame domain without consulting container duration. A new C++ frame-range/timecode value layer carries inclusive frame ranges from `AppController` through `ExportEngine`; FFmpeg timestamps are emitted from reduced integer rationals only. Final validation classifies exact success, bounded terminal-deficit success-with-warning, or failure before the output transaction is finalized.

**Tech Stack:** C++20, Qt 6 Core/Test/QML, CMake, FFmpeg/ffprobe.

**Spec:** User request in the 2026-08-30 Codex conversation; `AGENTS.md`; `docs/export-pipeline.md`.

## Global Constraints

- Work directly on `main`; no branches, no worktrees, no push.
- Preserve the two-stage FFV1/BGRA and Main10 straight-alpha pipeline, cancellation, and output transaction rules.
- No floating-point value determines authoritative frame boundaries or expected count.
- Custom IN and OUT are inclusive C++-owned SMPTE strings; QML performs no frame arithmetic.
- Use portable checked integer/rational operations; rounding a non-exact representable boundary is down.
- Do not run `qmllint`; complete with build, CTest, `git diff --check`, and a clean main.

---

### Task 1: Frame-domain and SMPTE core

**Files:**
- Create: `native/src/export/ExportFrameRange.{h,cpp}`
- Modify: `native/CMakeLists.txt`, `native/src/export/MediaProbe.{h,cpp}`, `native/tests/TelemetryTests.cpp`

**Interfaces:**
- Produces `ExportFrameRange { qint64 firstFrame; qint64 lastFrame; }`, `frameCount()`, checked frame/tick conversion, and C++ `formatTimecode`/`parseTimecode`.
- Produces `MediaInfo::videoFrameCount` plus a documented frame-count source from `nb_frames`, exact `duration_ts/time_base`, or packet count.

- [ ] Write failing Qt tests for every required rational rate, inclusive 100-frame and one-frame ranges, timecode round trips, invalid FF values, and the `78273` requested / `78272` actual source-domain regression.
- [ ] Run the focused test binary and confirm the assertions fail because the frame model/API is absent.
- [ ] Implement reduced, overflow-checked integer operations and the smallest `MediaInfo` additions necessary to select the actual video frame domain.
- [ ] Run focused tests and the existing media-probe tests until green.
- [ ] Commit `fix: make export timing frame addressed`.

### Task 2: Export engine and FFmpeg integer boundary

**Files:**
- Modify: `native/src/export/ExportEngine.{h,cpp}`, `native/src/export/TemporaryOverlayValidation.{h,cpp}`, `native/src/export/ExportStoragePolicy.{h,cpp}`, `native/tests/TelemetryTests.cpp`

**Interfaces:**
- Consumes `ExportFrameRange`, source frame domain, and exact `MediaRational`.
- Produces an integer expected schedule, exact timestamp text for FFmpeg seek/trim/audio boundaries, and diagnostics with `sourceFrameCount`, range, generated/submitted/temporary/progress/final counts.

- [ ] Write failing tests proving full-video N schedules N, fractional decimal metadata cannot create an extra frame, the 59.94 synthetic N-frame Stage A/Stage B pipeline preserves N, and bounded non-zero selection remains frame-correct.
- [ ] Run those focused tests and record the expected old float-derived failure.
- [ ] Replace authoritative range/count/seek calculations with the integer model while retaining doubles only at rendering/display interfaces.
- [ ] Run focused tests, then all native tests; inspect generated Stage-B filter text to ensure no floating-point-derived count is used.
- [ ] Commit the completed engine conversion with Task 1 if the interface cannot safely be split.

### Task 3: Terminal-deficit result and safe transaction completion

**Files:**
- Modify: `native/src/export/ExportEngine.{h,cpp}`, `native/src/app/AppController.{h,cpp}`, `native/tests/TelemetryTests.cpp`

**Interfaces:**
- Produces explicit `Success`, `SuccessWithWarning`, `Failure`, and `Cancelled` terminal classification with expected/actual/deficit counts.
- Consumes all existing final codec/raster/rate/color/audio/transaction checks before accepting a 1..10 terminal deficit.

- [ ] Write failing tests for 0, 1, 10, 11 missing frames and surplus, verifying only 1..10 with every other validation check passing is committable.
- [ ] Run the focused tests and confirm current exact-count behavior rejects the accepted warning cases.
- [ ] Implement classification before AppController failure cleanup and commit `SuccessWithWarning` through the existing `ExportOutputTransaction` only when all safety predicates pass.
- [ ] Run focused tests and all native tests.
- [ ] Commit `fix: preserve exports with small terminal frame deficit`.

### Task 4: Frame-addressed export UI and durable observability

**Files:**
- Modify: `native/src/app/AppController.{h,cpp}`, `native/qml/Main.qml`, `native/tests/TelemetryTests.cpp`

**Interfaces:**
- Exposes C++-formatted full/custom range timecode strings; accepts IN/OUT strings and reports parse/range errors without QML frame arithmetic.
- Writes exact integer frame evidence and result classification to the persistent export log.

- [ ] Write failing controller tests for full-video timecode display, valid inclusive custom IN/OUT, and rejection of invalid/reversed/out-of-domain strings.
- [ ] Run the focused tests and confirm missing C++ API/old seconds API fails.
- [ ] Replace decimal range controls in the export dialog with IN/OUT SMPTE fields and bind only strings to C++.
- [ ] Run focused tests and a QML launch/smoke if available; inspect the export dialog without running qmllint.
- [ ] Commit `ui: use SMPTE timecode for export ranges`.

### Task 5: Documentation and final verification

**Files:**
- Modify: `README.md`, `currentstate.md`, `docs/export-pipeline.md`, `docs/export-output-safety.md`, `docs/testing.md`, and `AGENTS.md` only for durable invariants; review `ROADMAP.md` without turning it into a changelog.

- [ ] Update documentation to state the implemented integer/frame-addressed invariant, full-video source-domain choice, inclusive SMPTE semantics, exact FFmpeg boundary, and bounded warning policy without overstating real-media coverage.
- [ ] Run `cmake --build build-native --parallel`, `ctest --test-dir build-native --output-on-failure`, and `git diff --check`.
- [ ] Run a short synthetic 60000/1001 integration check and separately record the read-only real-source metadata evidence; do not run a long 4K export.
- [ ] Commit documentation only if it does not fit the preceding focused commits; otherwise amend no commits and make a dedicated `docs: document frame-addressed export invariants` commit.
