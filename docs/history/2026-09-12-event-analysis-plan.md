# Flapped Ear Telemetry: event analysis delivery plan

Updated 12 September 2026. This is the working plan for extending the existing
application in this repository, not for creating another product.

## Product and architecture decision

One macOS-first application provides an overlay editor/generator and a full
telemetry analysis workspace. Both use the same telemetry parsing, lap timing,
synchronization, project ownership and source-resolution modules. Keep analysis
logic independent of QML and overlay rendering. The existing binary names,
namespace and project compatibility identifiers are not renamed by this work.

The analytical hierarchy is **Event → Run → Lap**. Sources attach to a Run,
not to individual laps. Vehicle and track/configuration are reusable metadata
references, not a requirement to nest or duplicate all data under a vehicle.
An Event is a user-confirmed track-day grouping; a Run is one recording/stint,
potentially backed by multiple exports of that recording. File count is not run
count. Lap identity must include its owning run, not just a lap number.

Video is optional for telemetry analysis. The editor selects a run and its video
binding/time transform; an analysis reference lap can come from another run.
Do not concatenate runs across paddock breaks or reset telemetry at a future
GoPro chapter boundary. HR remains a recorded VBO/RCZ channel; no separate HR
importer is introduced. Export continues to use the shared production scene.

## Verified baseline and gaps

Baseline: `0715918` on `main`. The working tree was clean and there were no open
pull requests when this iteration began.

- `TelemetrySource` dispatches VBO/RCZ into `TelemetrySession`; the existing
  worker derives track geometry and `LapSession` before controller commit.
- `AppController` owns one session, one telemetry source reference, one video
  and a central synchronization transform. Source generation and cancellation
  protect asynchronous commits.
- Analysis uses raw channel segments; preview/export use the separate render
  context and shared QML scene. These boundaries should be retained.
- `.fetproject` v2 source loading/recovery/export assumes one active telemetry
  source. A batch cannot safely be implemented by looping over `loadVbo()`.
- Cross-run selection, persisted events, confirmed RCZ/VBO source groups,
  distance-aligned comparison and event summaries are not implemented.
- The existing lap-reference GPS-gap hardening item must be resolved before
  extending numerical lap delta across runs.

This is a scoped architecture review, not a new full shipping audit. Existing
beta acceptance requirements and open correctness findings remain open.

## Slice 1: review-only import preparation

Implemented API: `prepareTelemetryImport()` in `telemetry/TelemetryImportPlan`.
It is part of the existing native core and is **not connected to QML yet**.

Acceptance criteria:

- [x] Accept a bounded list of VBO/RCZ paths through one worker-compatible API.
- [x] Reuse existing parsers and source-defined lap detection; keep each
  recording's raw timeline, channels, missing values, metadata and warnings.
- [x] Return immutable sessions, source provenance, source-derived laps and
  content-addressed proposal IDs, independent of input order or filename.
- [x] Return one result for every selected path, including duplicate and failed
  files. Ordinary file failures do not discard other successfully parsed files.
- [x] Recognize identical same-format bytes with a full SHA-256 digest, not a
  filename or the project's lightweight sampled fingerprint. Recheck the
  digest after parsing; reject ordinary mid-import source changes.
- [x] Expose conservative cross-format GPS matches as review candidates only.
  Never silently merge candidates, choose a preferred format, or assign dates.
- [x] Bound file count, bytes read and retained channel samples; preserve the
  existing parser bounds and cooperative cancellation.
- [x] Add a separately registered Qt Test target for this API.

Verification gate: the new target and existing macOS regressions must pass on
the exact PR head. [PR #7](https://github.com/arekkozuch/VBOOverlay/pull/7) retains
the current check status and validation record; implementation checkboxes above
do not themselves claim that execution passed.

### Import limits and failure behavior

Defaults/ceilings are 64 input paths, 128 MiB per file, 256 MiB input bytes per
batch and 16 million retained channel samples (summed across all channels).
Callers may lower these limits but may not raise them. Paths are limited to
4,096 characters. Parsing is sequential, never a parallel allocation burst.
Admitted sessions stay within the sample budget; one in-flight parser and lap
derivation still use their existing scratch allocations/limits. This is not a
claim that total process memory is capped at the retained sample payload size.

Input byte accounting includes duplicate and malformed file attempts. Full
digests use cancellable 64 KiB reads before parsing and again after parsing, so
successful non-duplicates have additional bounded I/O. Parser-specific reads
remain governed by their own limits. The source-change check is an accidental
mutation guard, not an atomic filesystem snapshot/security identity guarantee.

Invalid limit settings or excessive file count reject the whole request.
Per-file byte/sample failures return errors for those files. Cancellation throws
`OperationCancelled` out of the entire request; no partial result is published.
An eventual controller must still generation-check the returned plan before
displaying or committing it. An empty input produces an empty plan.

Proposal IDs are derived from content hashes for repeatable review and duplicate
references. They are not yet persisted Run IDs: a confirmed run must retain its
own stable identity across source replacements, grouping and gate edits.

### Possible same-run evidence: deliberately not automatic matching

For VBO/RCZ pairs only, the planner samples 32 evenly spaced positions over each
recording's common latitude/longitude extent. It uses nearby **raw** GPS samples
(at most 0.6 seconds away), not interpolated/smoothed positions. Both axes need
at least 16 samples, the GPS extent must be at least ten seconds, at least 29
positions must be comparable, and the trace must move at least 50 metres away
from its first usable point. GPS duration difference is limited to 1% of the
shorter extent, bounded to 1–2 seconds; maximum sampled position separation is
ten metres.

These fixed thresholds are review heuristics, not a calibrated confidence
score. GPS is compared by elapsed recording position, which tolerates differing
first-sample offsets but does not prove common absolute time or date. Similar
drives on different days can remain candidates. Cropped, shifted, sparse,
stationary, same-format nonidentical or differently paced recordings can be
missed. Names do not influence the decision. One-to-many candidates remain
explicit; all sources stay separate and no event best is calculated from them.

Before automatic grouping is enabled, validate representative private RCZ/VBO
pairs, promote verified source-clock/date metadata, handle conflicting track
configurations and define a review UI. No source wins on extension alone.

## Next slices and acceptance gates

Slice 3 now includes the native multi-file picker and transactional review,
explicit source grouping, new event creation and append. See
[batch import workflow and limitations](../batch-import.md). Folder discovery and
drag/drop remain follow-ups; this does not implement event-wide statistics.

Slice 2 is now implemented as development `.fetproject` v3 with a run-local
source/sync model and an Active run selector in Analysis. See
[event project format and macOS fixture walkthrough](../event-project-format.md).
The owner authorized schema evolution without a legacy-data migration burden.
The existing v2 single-recording path remains; event creation/batch review is
still slice 3. Validation results belong to the implementation PR, not these
implementation checkboxes.

| Slice | Scope | Required acceptance |
| --- | --- | --- |
| 2 — persisted event model | Event/Run/Lap identity, source groups, active editor run, optional per-run video binding; explicit project-version/migration decision | Existing v2 projects open unchanged; event save/open/recovery round trip; missing/relinked sources preserve other runs; conflicting legacy active-source projection cannot silently overwrite event state |
| 3 — macOS batch review UI | Add files, bounded folder discovery, drag/drop, progress/cancel, per-file diagnostics, source-group and event confirmation | Import six separate files; review a paired RCZ/VBO and malformed file; reject stale results; cancellation leaves the current project intact; telemetry-only operation needs no video |
| 4 — event overview | Run list, notes, available channels, laps and best-of-run/event | Aggregate only confirmed compatible track configuration and eligible laps; explicit no-data states; retain per-run provenance |
| 5 — cross-run comparison | Distance alignment, delta, synchronized charts/map, independent reference selection | GPS gaps/crossings and changed gates covered; deterministic alignment tests; preview/export still use selected editor run and its transform |
| 6 — sectors and driver analysis | Reviewed sectors, corner metrics, consistency, G-G, coasting/driving-state evidence, sector theoretical best | Separate measured/calculated/inferred values; no fabricated pedals; theoretical sectors carry compatibility and provenance; unsupported metrics stay unavailable |
| 7 — event insights | Thermal trends, HR, progression, summaries, prioritized time-loss observations | No cooling curve across unrecorded breaks; no temperature causation or HR-as-stress claim; traffic/conditions can exclude misleading comparisons |
| 8 — advanced analysis | Compatible-segment potential, comparison video, multi-event history and evidence-based explanations | Validate algorithms before numeric potential claims; retain shared video/overlay pipeline; text explanations only describe computed evidence |

Slices are dependency-ordered, not a commitment to implement everything in one
change. First ship the correct import/persistence/comparison model. Automatic
coaching, multi-source channel fusion, multi-chapter video, new sensors and
Windows-specific feature work are not part of the current slice.

## Validation and publication

Development focus is macOS. Existing CI remains intact, including its automatic
Windows jobs; no new Windows runtime/packaging work is planned here. The new
import suite runs through ordinary CTest alongside parser, application,
render/export regressions and QML startup checks.

This Work environment cannot run the native gate: CMake and Qt are absent and
system package setup was permission-blocked. The user authorized a development
branch and PR so the existing CI can compile and execute tests. Do not report a
native pass until its workflow actually passes. A hosted test pass does not
validate private recordings, interactive macOS UI or physical VideoToolbox
hardware. The owner now authorizes merging development PRs after all required
CI checks pass. Releases remain outside this authorization.
