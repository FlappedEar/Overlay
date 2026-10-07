# FlappedEar Telemetry: handover to the architect

Written on 2 October 2026 for the architect of the new FlappedEar Telemetry app
([KAN-168]). The reference revision is `arekkozuch/VBOOverlay` at `0ec7416`. Paths are
relative to this repository, which is public. Everything here is cross-reference
material: it describes behaviour, data and decisions, not code to reuse.

## Mandate

The owner's decisions of 2 October 2026:

- **Two applications.** FlappedEar Overlays, this repository, is the desktop
  video-overlay editor for macOS and Windows (Qt 6, C++20, QML). FlappedEar
  Telemetry is the track-day analysis app for macOS, Windows, iOS and Android.
- **Telemetry is a Flutter app** ([KAN-167]).
- **A blank page.** It is built in a new repository, `FlappedEar/Telemetry`, with
  its own Jira project, and shares no code with this repository. In the owner's
  words: "We will be starting fresh with new Jira and repository - blank piece of
  paper - just cross-referencing." Choosing the architecture is your job; this
  document prescribes no package structure, state management or storage library.
  Where it states a rule, the rule comes from the owner or from the shared document
  format.
- **One document format.** Both apps read and write the same `.fetproject` format,
  and it stays compatible in both directions ([KAN-170]).
- **No data loss.** The owner set this condition for every migration and for project
  data.
- **Overlays becomes an overlay editor.** On 5 October 2026 the owner approved
  removing its Lap Analysis window and day import ([KAN-166], done that day). The last Overlays
  `main` commit with the full analysis, `7eae6cd`, is recorded on [KAN-169] as a
  reference. Day documents keep every analysis field.
- **The app is free for users** and is not sold.
- **Identity:** `com.flappedear.telemetry` (see
  [Identity and storage](#identity-and-storage)).
- **Platforms:** macOS is the active development platform. In this repository,
  Windows work was paused on 13 September 2026; on 2 October 2026 the owner resumed
  Windows code changes and validated Windows locally
  ([record](windows-validation-2026-10-02.md)), while Windows CI and packaging stay
  paused. Confirm with the owner how this applies to the new app.
- **Minimum OS versions** (owner, 2 October 2026): iOS 15 and Android 8.0 (API
  level 26) as the starting point. Desktop minimums are not decided; Flutter 3.47
  supports macOS 12 and later and Windows 10 and later
  ([Flutter supported platforms](https://docs.flutter.dev/reference/supported-platforms)).
- **Where the work lives:** the Jira space [FET] ("FlappedEar Telemetry"), the
  public repository `FlappedEar/Telemetry` (an initial README only), and the private
  repository `FlappedEar/refdata` for the reference recordings.

## Product brief

At the track nobody carries a laptop. Between sessions a driver needs the day's
analysis on a phone or tablet: where the best lap loses time, which sections
improved, and what to change on the next run. RaceChrono already records on that
phone. Telemetry has **no video**; video belongs to Overlays.

The central workflow ([product vision](product-vision.md)) is: import a day, see
the important results, select a loss, inspect its corner, then check the map and
channels. The first screens proposed for phones and tablets are listed in
[Mobile specifics](product-split-plan.md#mobile-specifics-telemetry):

1. the day, with its sessions and laps;
2. where the best lap can improve, on the map (KAN-120);
3. time losses;
4. sections by session (KAN-64);
5. lap and comparison: the Corner Analyzer, charts and G-G.

A full day is six recordings of about 6–7 MB each. Import on phones uses the share
sheet or intents (no file dialog, no absolute paths), and the files are copied into
the app sandbox.

### How results are presented

Sources: [`handover.md`](../handover.md) "Owner direction and preferences", the user guide, and the code cited.

**Lead with the result**
- **Driver first.** Lead with the best lap and where the time is, on the track map. Tables of numbers are not enough.
- **Names, not files.** Say "Session 3 · LAP 2", not file names. Runs are named "Session N" in recording-time order.
- **Mobile.** The mobile plan is touch-first: no hover, no tooltips, large targets.

**Times**
- **Format.** From one minute, times read `m:ss.mmm` ("1:49.898"); below a minute, "28.662 s"; a non-finite value is "—".
- **Rounding.** Round before splitting minutes, so a time never reads "x:60" (KAN-149). Overlays formats times in three places: [`AnalysisControllerComparison.cpp`](../native/src/app/AnalysisControllerComparison.cpp), [`LapTiming.cpp`](../native/src/telemetry/LapTiming.cpp) and [`OutingProgressionDialog.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/OutingProgressionDialog.qml).

**Results are observations**
- **Missing results.** A missing result says why and is never shown as zero.
- **Statistics.** Typical means the median, spread means the interquartile range, and at least three laps are needed. There are no percentage scores.
- **No advice.** Results are observations, not causes or driving instructions.
- **Provenance.** Measured and inferred values are labelled. Brake data is never fabricated.

**Comparison and charts**
- **Delta.** Δ is A − B; a positive value means A is behind.
- **Colours.** A is green `#55e6a5` and B is orange `#d95926`. The owner found the earlier blue for B hard to tell from green.
- **Longitudinal G.** Braking points upward in charts; acceleration points upward in G-G and on the map.
- **Gaps.** Data gaps are never bridged.

## Readiness checklist

**The owner, before the architect starts** (status on 2 October 2026):

- [x] Create the new Jira space: [FET], "FlappedEar Telemetry". KAN is renamed
      "FlappedEar Overlay".
- [x] Give access to `FlappedEar/Telemetry`: the Claude GitHub App is installed on
      the `FlappedEar` organisation.
- [ ] Choose the licence of the new repository. Deferred by the owner ("I don't
      care for license yet").
- [x] Decide the minimum OS versions: iOS 15 and Android 8.0 to start with
      (KAN-122 decision 5). Desktop minimums are open.
- [x] Hand over the private real-day recordings: they are in the private
      repository `FlappedEar/refdata` (see
      [Reference data](#reference-data-and-figures)).

**The architect, first steps (suggestions, not prescriptions):**

- [ ] Read this document, then the [reading list](#reading-list).
- [ ] Write the new repository's `AGENTS.md`, carrying over the
      [invariants](#invariants-to-carry-over).
- [ ] Create the backlog in the new Jira project. Re-create [KAN-126]–[KAN-130]
      there, link both ways, and close the KAN copies as moved ([KAN-168]).
- [ ] Agree with the owner where the shared format fixtures live and who owns schema
      changes ([KAN-170]).
- [ ] Bootstrap CI: macOS first, then the iOS simulator and the Android emulator.
- [ ] First vertical slice: import one VBO file, find its laps, and show the best lap
      on a map. Check the figures against this repository's fixtures.

## Identity and storage

- Bundle and application identifier: `com.flappedear.telemetry`.
- Until 2 October 2026 the desktop editor itself was called FlappedEar Telemetry
  and used `com.flappedear.telemetry`. Its Qt data still sits in the
  `FlappedEar Telemetry` locations listed in
  [application identity](application-identity.md#moving-existing-data) until
  Overlays moves it on first start. The new app must never read, write or delete
  those locations.
- On a Mac that ran the old Overlays candidates, macOS may already hold state
  keyed by `com.flappedear.telemetry`, such as a preferences domain or privacy
  permissions. Medium confidence; check before the first release.
- The brand is written **FlappedEar**, without a space (owner direction, KAN-171), so
  the app is "FlappedEar Telemetry". That is also the old Overlays app's Qt storage
  name. Any default location derived from a company and product name (for example
  `FlappedEar` / `FlappedEar Telemetry` in a Windows runner's version resource) must
  not resolve to the Qt locations above. Verify each platform's default directories
  before the first release (medium confidence that Flutter's defaults differ).

## The shared contract: `.fetproject`

The `.fetproject` document is the only thing the two apps share ([KAN-170]). A document written by either app must open in the other and survive a re-save without losing anything.

- **Specification:** [event projects](event-project-format.md) and [project format](project-format.md).
- **Code:** `native/src/project/` (`EventProjectCodec`, `ProjectLimits`, `ProjectWriter`, `ProjectRecoveryStore`, `ProjectSourceReference`, `BoundedJsonLoader`) and [`DocumentController.cpp`](../native/src/app/DocumentController.cpp).
- **Tests:** `EventProjectTests.cpp` with `EventProjectFixture.h`, and `TelemetryAppTests::keepsTheEditorStateAnotherAppSaved`.

**Versions and structure**
- **Versions:** `version` is 2 or 3.
    * Version 3 is the event document that Telemetry reads and writes.
    * Version 2 is the editor's single-recording project, with a `scene` and no `event`. Whether Telemetry must read it is open.
- **Version 3 root:**
    * `version`;
    * `event`: `id`, `name`, `activeRunId`, `runs[]`, optional `lapExclusions` and `analysisDecisions`;
    * `documentState`: `id` and `savedRevision`, a decimal string;
    * optional `scene`, `analysis`, `mapSettings` and `exportSettings`. A version 3 document may omit `scene`.
- **Run:**
    * `id`, `name`, `primaryTelemetrySourceId`;
    * `sources.telemetry[]`, each with `id`, `reference`, optional `contentSha256` and `importProvenance`;
    * optional `sources.video`;
    * `sync` with `offset` and `timeScale`, which is **required on every run**;
    * optional `trackConfiguration`, `trackInference`, `trackSegments`, `trackSegmentReview`, `fusion`, `notes`, `conditions` and `setupChanges`.

**Who owns which fields**
- **Shared:** `version`, event identity, runs, `activeRunId`, telemetry sources and `documentState`. Selecting another run is an edit.
- **Analysis:**
    * `lapExclusions`;
    * the comparison group, slots, range and channels;
    * `trackConfiguration`, `trackInference`, `trackSegments` and `trackSegmentReview`;
    * `fusion`;
    * run name, notes, conditions and setup changes.
- **Overlays:** `scene.widgets`, `analysis.channels`, and each run's `sync` and `sources.video` with its chapters. Telemetry must keep all of these unchanged.

**What survives a round trip**
- **Open objects keep unknown keys** through Save, Save As and recovery: the root, `event`, runs, telemetry sources, references, video and `sync`. The only bounds are generic: depth 32, keys and strings up to 4,096 characters, and 4 MiB per file.
- **Closed objects refuse extra keys.** One extra key makes the whole document invalid:
    * a lap reference has exactly 10 keys;
    * a `lapExclusions` entry has exactly 2;
    * a `trackSegments` item has exactly 6;
    * `trackSegmentReview` has exactly 4, and each of its decisions exactly 3.
- **Replaced whole when written,** so unknown keys inside are lost:
    * `trackInference` on save;
    * `trackConfiguration` when the primary changes or its content is replaced;
    * the comparison range when edited;
    * in Overlays, each widget and the active run's chapter entries.
- **Validators reject the whole document when a known field has an unknown value:**
    * a `fusion` algorithm other than `channel-fusion-v1`;
    * a `trackSegmentReview` version other than `track-segment-review-v1`;
    * a segment type other than sector, corner or straight;
    * a `trackInference` layout that does not start with `gps-route-v1:`;
    * a comparison group or configuration reference that does not match `compatibility-v1:<64 hex>`;
    * a malformed `trackConfiguration`, comparison slot or source reference.

  Since KAN-170 (6 October 2026), a well-formed *newer* version tag on the fusion algorithm, the review version, the inference algorithm or a `compatibility-v1:` identity is kept and ignored instead. See the [compatibility rules](event-project-format.md#compatibility-between-the-two-apps-kan-170).

**Hash identities stored in documents.** These ids are SHA-256 hashes over Qt's compact JSON bytes ([`OutingLaps.cpp`](../native/src/telemetry/OutingLaps.cpp), [`LapTiming.cpp`](../native/src/telemetry/LapTiming.cpp), [`TrackSegments.cpp`](../native/src/telemetry/TrackSegments.cpp), [`EventProjectCodec.cpp`](../native/src/project/EventProjectCodec.cpp)):
- `gates-v1`, the timing-gate revision;
- `compatibility-v1`, the compatibility group;
- `lap-derivation-v1`, the derivation key inside every lap reference;
- `track-segments-v1`.

Two consequences:
- `lap-derivation-v1` hashes the whole `trackConfiguration` and the primary fingerprint. One extra key in `trackConfiguration` changes every lap reference, and saved exclusions and comparison slots stop matching.
- The exact bytes (key order, number and string formatting) are not specified anywhere, so a second implementation cannot be checked without test vectors.
- Whole-document behaviour is pinned the same way since KAN-218: `native/tests/fixtures/project-vectors/` holds days as Telemetry saves them and what a no-change save, in place or Save As, must write. Telemetry should run these vectors against its Dart save.

**Source references**
- **Paths:**
    * `relativePath` uses forward slashes and is written when the recording is within two parent folders of the document.
    * `absolutePath` is canonical.
    * Resolution tries the relative path first, then the absolute one, with no search. Save As rebases every reference.
- **`contentSha256`:** SHA-256 of the whole file, in lowercase hex.
- **`telemetry-v1` fingerprint** ([`ProjectSourceReference.cpp`](../native/src/project/ProjectSourceReference.cpp)):
    * contents: size; the SHA-256 of the first, middle and last 64 KiB; sample count; duration; the sorted channel list (name, unit, samples) **as the C++ parser reads it**;
    * matching: Overlays' day analysis requires an exact match and refuses a run without one;
    * so the new parser must agree on channel names, units and sample counts, or [KAN-170] must change how fingerprints are matched.
- **`contentSha256` in a reference** (KAN-208): Overlays writes the full SHA-256 of a telemetry file into its reference (an open object); in an event the source's own `contentSha256` holds it. Videos and chapters keep the sampled fingerprint only. See [full-content identity](project-format.md#full-content-identity-kan-208).
- **Relinking:** the same content keeps `trackConfiguration`. New content rewrites `contentSha256` and resets `trackConfiguration`. A mismatching file needs explicit confirmation.

**Saving and recovery: rules for both apps**
- **Identity and revision:**
    * Keep `documentState.id`.
    * Continue `savedRevision` from the loaded value and add one per edit.
    * Each app keeps its own recovery snapshot, and a snapshot counts as stale only when its revision is at most the saved file's revision. If one app saves a lower revision, the other app offers its older snapshot over the newer save.
- **Writing:**
    * Replace the file atomically.
    * Validate before committing a load and before saving. The limit is 4 MiB.
    * Never add keys to closed objects.
- **When the primary changes or its content is replaced,** do what Overlays does: set `trackConfiguration` to unknown, and remove `fusion` and `trackInference`.
- **No cross-process lock.** There is no lock between apps and no detection of external changes, so the last writer wins. Editing the same document in both apps at once loses changes.

**Where Overlays changes analysis data today**
- **On open:** nothing since KAN-166 step 2. Before that it approved automatic segments when a layout had none, which could leave a freshly opened document unsaved.
- **On save:**
    * `trackInference` and `analysis.channels` are kept as loaded (since KAN-166 step 5; before it Overlays' analysis replaced `trackInference`). Replacing the active run's recording removes that run's `trackInference`;
    * the active run's primary reference is rewritten from the loaded file. If its fingerprint differs, `contentSha256` is added and `trackConfiguration` reset;
    * default `mapSettings`, `exportSettings` and `documentState` are added, and every reference path is rewritten.

**Limits** ([`ProjectLimits.h`](../native/src/project/ProjectLimits.h), [`EventProjectCodec.h`](../native/src/project/EventProjectCodec.h))
- **Files:** 4 MiB for the document and for the recovery snapshot.
- **JSON:** depth 32; keys and strings up to 4,096 characters; ids up to 128 characters; names up to 160.
- **Event:** 64 runs; 8 sources per run; 128 sources in total.
- **Analysis:** 64 segments; 64 review decisions; 20,000 exclusions; reasons up to 256 characters; notes up to 4,096.
- **Comparison:** 4 channels; range up to 10⁶ m.
- **Fusion:** 64 rules; offset up to 86,400 s; drift up to 1,000 ppm.

**Known gaps ([KAN-170])**
- **Closed since 6 October 2026:** `SourceTests::keepsEveryTelemetryFieldThroughAnOverlayEdit` shows that Overlays keeps every analysis field when it re-saves a day, including a day without `scene`. Newer versions of the versioned fields are kept rather than rejected.
- **Format:**
    * The hash serialization is pinned only by test vectors (`QtHashVectorTests`).
    * Fingerprints depend on the parser's output.
    * There is no cross-process lock.

## Reference map

### Input formats and import

Recordings come from RaceChrono, as VBO text exports and native RCZ archives.
- **Specification:** [RCZ format](rcz-format.md), [telemetry semantics](telemetry-semantics.md) and the [telemetry data](user-guide/pages/telemetry-data.html) guide page.
- **Code:** `native/src/telemetry/`; tests in `native/tests/`.

| Area | Reference | Rules a re-implementation must match | Tests |
| --- | --- | --- | --- |
| File identity | `TelemetrySource` | The parser is chosen by extension only (`vbo`, `rcz`). `contentSha256` covers the whole file. Size must be 1 B to 128 MiB, and is checked again after reading. | `RczTests`, `TelemetryImportTests` |
| Recording model | `TelemetrySession` | Time is in seconds (double). Values are float32, and NaN means missing. A lookup outside the range or on NaN gives no value. Linear lookup needs both neighbours finite; "previous" uses the preceding sample; "nearest" prefers the earlier sample on a tie. The `throttle` alias prefers an accelerator-pedal channel with data. | `TelemetryCoreTests` |
| Gaps | `telemetryGapThreshold` | A gap is an interval longer than 3× the channel's median sampling interval. RCZ import inserts NaN markers at gaps; VBO import does not, so the analysis modules apply the threshold. The lookup itself interpolates between any two finite neighbours. | `TelemetryCoreTests`, `RczTests` |
| VBO | `VboParser` | See below. Limits: 128 MiB; 1,000,000 lines; 500,000 rows; 512 columns; 1 MiB per line; 64 KiB per field; 40 M decoded values. At most 200 warnings and 128 gates. | `TelemetryCoreTests` (VBO slots), `fixtures/basic.vbo` |
| RCZ | `RczParser` | See below. Limits: 128 MiB archive; 32 MiB per member; 256 MiB expanded; 1,024 members; 2 M samples per clock; 8 M decoded values; 256 channels; 24 h span; 64 gates. | `RczTests` with `RczFixture.h` |
| Memory budget | `TelemetrySessionCache` | 256 MiB decode budget; at most two entries, least recently used evicted first. Entries are validated on a hit, and a failed check evicts. Phones need their own measured budget (KAN-129). | `TelemetrySessionCacheTests` |
| Folder or drop | `TelemetryFolderScan` | Depth 8, 20,000 entries, 64 files; callers may only lower these. Symbolic links are never followed and hidden entries are skipped. Paths are deduplicated by canonical path and sorted. Too many files is an error, never a truncation. | `TelemetryCoreTests` |
| Batch plan | `TelemetryImportPlan` | 64 files; 128 MiB per file; 256 MiB per batch, where every attempt counts; 16 M retained samples; paths up to 4,096 characters. | `TelemetryImportTests` |
| Clock alignment | `RecordingAlignment` | Describes a clock, never applies it: primary = candidate + offset + drift × candidate. Result: aligned, ambiguous, conflicting or insufficient. Thresholds: correlation ≥ 0.9; residual ≤ 0.3 s; drift ≤ 1,000 ppm; declared clock within 2 s; up to 8 windows of ≥ 60 s searched ±10 s; overlap ≥ 20 s. Below 0.75 confidence, a matching declared clock is required. | `RecordingAlignmentTests` |
| Channel fusion | `ChannelFusion` | Aligned sources only. Channels match by alias, otherwise by name. Units must be equal; a unit only one side declares (VBO) counts as equal when ≥ 10 samples agree (KAN-184); nothing is rescaled or resampled. RCZ gap markers stay inside their gap on the primary clock, and a merged channel carries NaN gap markers wherever its sources' own thresholds see a gap (KAN-188). A conflict needs ≥ 10 compared samples with a median difference above km/h 2, % 3, g 0.05, °C 2, rpm 100, otherwise 5 % of the range. | `ChannelFusionTests` |
| Temperature and laps | `TemperatureAssociation` | Spearman correlation with averaged tied ranks, at least 8 pairs. Weak below 0.3, moderate below 0.6, otherwise strong. A time-of-day confound is flagged at ≥ 0.6. | `TemperatureAssociationTests` |
| Tyres | `TyreData` | The channel name contains tyre or tire, exactly one of temp or pressure, and one corner. Temperatures are °C unless Fahrenheit is declared. Pressure uses its declared unit, otherwise the median decides: 0.5–8 bar, 8–100 psi, 100–1,000 kPa. An exact 0 is a placeholder. Plausible ranges are −40 to 250 °C and up to 10 bar. The RCZ import maps RaceChrono's CAN-bus tyre channels (kind 12) to the VBO names (see `rcz-format.md`); `TelemetryRenderContext::tyreChannelsMissing` drives the inspector's no-tyre-channels notice, and the widget's `temperatureSourceFL`…`pressureSourceRR` settings replace a corner's channel through `withTyreChannelChoices` (KAN-203). | `TelemetryCoreTests`, `NativeWidgetTests` |

**VBO** (`VboParser.cpp`; [telemetry semantics](telemetry-semantics.md#vbo-timestamps))

- **Text:**
    - UTF-8; a byte-order mark is stripped; CRLF and CR line endings are accepted.
    - Lines starting with `;` or `#` are skipped.
    - Section names are case-insensitive. Exactly one data section and one column-names section; other sections become metadata.
    - A row containing a comma is comma-separated; otherwise it splits on whitespace.
    - Duplicate column names are made unique.
- **Time column:**
    - It is `time`, `timestamp` or `utc time`. A file without one is rejected; row numbers are never used as time (KAN-207).
    - `HH:MM:SS[.f]` and six-digit `HHMMSS[.f]` clock times are read first, then plain seconds.
    - A clock that goes from 23:00 or later to 01:00 or earlier crosses midnight (+24 h).
    - Duplicate or backward rows are skipped with a warning. Time zero is the first accepted row.
- **Values:**
    - Values are float32. Invalid or out-of-range cells become NaN.
    - Columns without any finite value are dropped.
    - VBO channels carry no units.
- **Coordinate units are never guessed from the numbers.** Either the `Generated by RaceChrono Pro v10.2.4` comment (arc-minutes, west-positive longitude) or a `coordinate units = degrees|arc-minutes` header line is required. Without one, positions and gates are withheld with a warning.
- **Timing gates:** gates from RaceChrono 10.2.4 are converted to endpoints; gates from other versions are dropped. An RCZ on a library track has no traps; its start line is rebuilt from `session.json` lap boundaries (KAN-204, `rcz-format.md`).
- **Absolute start time:** only from a RaceChrono comment, clock-format times and exactly one `File created on dd/MM/yyyy at HH:mm:ss` line.
- **Channel aliases:** `VboParser.cpp`; the `latacc-calc` and `longacc-calc` channels are preferred for G.

**RCZ** ([RCZ format](rcz-format.md))

- **Archive:**
    - A flat ZIP32 archive, stored or deflated only, read in memory and never extracted.
    - Headers, CRCs and sizes must agree.
    - Overlapping members, links, ZIP64, encryption, unsafe paths and any `/` in a member name are rejected. A `/` means a multi-session or resumed archive.
- **Session files:** `session.json` and `sessionfragment.json` must both be version 1 with the same first timestamp, and no lap may be resumed.
- **Channels:**
    - Member names encode kind, device, group, id and storage.
    - Id 1 is the group's clock in epoch milliseconds, measured from the session origin.
    - Position is two int32 values divided by 6e6, in degrees, with east-positive longitude.
    - Sentinel values become NaN.
- **Errors and warnings:**
    - Two devices for the same meaning, or missing speed or latitude, is an error. An unknown channel gives a warning.
    - Every session carries a device-axis accelerometer warning.
- **Verification so far:** one private RaceChrono Pro 10.2.4 recording, plus synthetic archives.

**A day import, end to end** (`TelemetryImportPlan.cpp`, [`DocumentControllerImport.cpp`](../native/src/app/DocumentControllerImport.cpp), [batch import](batch-import.md))

1. **Collect paths.** A folder or a drop becomes a sorted, deduplicated path list.
2. **Prepare each file, cancellably.**
    - Size checks, then a SHA-256 of the content. A repeated digest is a duplicate.
    - Parse and apply the sample budget.
    - Derive laps; this needs exactly one Start gate.
    - Hash again: a file that changed in between is an error.
3. **Identity is the content, not the file name.** The source id is `sha256:<hex>`. Each file is Ready, Duplicate or Error, and one error does not stop the batch. Cancellation returns no partial plan.
4. **Pair exports of the same session.** A VBO and an RCZ are paired when all of these hold:
    - they are dated within 1 s of each other;
    - their durations agree within 2 s;
    - their 32-point GPS signatures match: at least 29 points compared, none more than 10 m apart.
    The VBO becomes the primary and the RCZ an alternative. Channels are not fused automatically.
5. **Commit.**
    - Runs are ordered by absolute start time (undated runs last) and named "Session N". They are never renumbered.
    - Each source stores its reference and fingerprint, `contentSha256` and import provenance.
    - A final digest check runs before the commit.
6. **Fingerprint (`telemetry-v1`).** File size, SHA-256 of three 64 KiB blocks, sample count, duration and the channel list. It is checked again on later loads (`ProjectSourceReference.cpp`).
7. **Dates.** Day-list times come from the recording's absolute start, never from file names or modification times.

**Open points**
- Alignment and fusion are verified on synthetic data and on the six VBO/RCZ pairs of the private real day (all aligned; 31 of 34 shared channels compared after KAN-184), not yet on other days or loggers.

### Analysis

The modules below are in dependency order. Code: `native/src/telemetry/`; tests: `native/tests/`. [Testing](testing.md) has one section per feature ticket, recording what was verified and how. The thresholds are the current values; check the code for any not listed.

| Module | Computes | Rules to match | Tests |
| --- | --- | --- | --- |
| `LapTiming`, `TimingGate.h` | Gate passes, timed laps and lap traces from the recording's only Start gate | Exactly one Start gate. Gate 1–200 m long, corridor 5 m inner and 10 m outer. Ground and crossing speed ≥ 2 m/s, crossing ratio ≥ 0.10. 1 s refractory period, clusters ≤ 5 s. The direction most valid crossings share is accepted, the first crossing's on a tie; crossings the other way are rejected (KAN-213; before 6 October 2026 the first crossing's direction locked). A pass must really cross the line within the gate span widened by 5 m at each end; a same-side near miss is rejected (KAN-205, as Telemetry FET-198). A lap under 3 s or over 1 h, under 200 m, over 100 m/s on average, or under 80 % of the recording's (or, across the day, its group's) median path is `implausible-lap`: listed, never ranked (KAN-225, as FET-199). A gap breaks continuity. A lap with a GPS gap or invalid GPS is kept but not eligible. | `TelemetryCoreTests`, `LapEligibilityTests` |
| `OutingLaps` | OUT / LAP / IN rows (UNKNOWN without a pass), lap references, compatibility groups, ranking | A lap reference is its exact start and end, the file's SHA-256 and a derivation key; never a lap number. Group id = SHA-256 of layout, direction and gate revision; none when one is unknown. Ties order by duration, clock, run, start, end. Quantiles interpolate linearly. | `LapEligibilityTests` |
| `TrackInference` | Route shape, direction, layout groups | Closed route: ends ≤ 25 m apart, 100–30,000 m, 256 points, clockwise when the area is negative. Routes match with the same direction, length within ±5 %, cross-track ≤ 25 m max and ≤ 10 m RMS. The largest cluster needs ≥ 60 % of laps. A lap more than 12 m off the others is dropped (needs ≥ 3 laps). A manual layout always wins. | `TelemetryCoreTests` |
| `OutingLapDerivation`, `OutingLapLoader`, `TrackGeometry` | Rows per run; verified lap sessions | Content hash, fingerprint and stored gate revision are checked; changed content is stale. One failing run does not stop the others. | `OutingPipelineTests` |
| `TrackProgress` | Distance-along-track axis, lap projection, delta | Axis 50–30,000 m at about 2 m spacing, 0 where the reference lap crosses the gate segment; if it crosses more than once, the crossing nearest the lap's first point (KAN-212). A path that never crosses but passes within 15 m of the segment starts at the point nearest the gate midpoint; a gate farther away gives no axis, with the distance in `ProgressAxis::problem` (KAN-212). Progress is unwrapped within a lap: a first fix within 5 s of the start projecting onto the far half is negative, and fixes past the finish exceed L; a fix up to 3 m behind the previous one in its segment is held at the previous progress. The timed delta runs from each lap's timed start, at 0 and L at its timed start and end when coverage is within 15 m of the gate. Curvature from the circular-mean heading, positive to the left. Projection: match ≤ 20 m, runner-up ratio 0.7, re-acquire after 5 s, window clamp(1.6·v·dt, 15, 150) m, backward step ≤ 3 m. A failed fix ends the segment. | `TrackProgressTests` |
| `TrackSegments`, `TrackSegmentProposals`, `TrackSegmentReview`, `TrackSegmentEditing` | Corner and straight proposals; the approved set, its stamps and edits | Corner where \|curvature\| ≥ 1/250 m⁻¹. Turns < 0.35 rad are ignored. A straight < 20 m joins the corners; < 40 m makes the boundary uncertain. At most 64 segments, sorted; only the last may wrap the gate. A stamp is current only when configuration, revision and algorithm match. Split keeps the first id, merge the earlier. | the four `TrackSegment*Tests` |
| `SectorTiming` | Sector times per lap | 0 and L use the lap's own timing. Coverage within 15 m of the gate counts. A sector is untimed unless fully covered. A sum is given only for a complete partition, matching the lap within 1 ms. | `SectorTimingTests` |
| `CornerPhases`, `CornerSpeeds` | Entry, apex, minimum and exit speeds | Apex = midpoint of the region ≥ 0.8 × peak curvature (closes below 0.6 ×). Minimum speed is sampled every 1 m, and any hole leaves it unresolved. A corner across the gate has no speeds. | `CornerPhaseTests` |
| `BrakingOnset`, `BrakingMetrics` | Braking point, duration, distance, deceleration | Brake 10 / 5 %. Only without a brake channel, inferred from −longitudinal G at 0.30 / 0.15 g. Minimum 0.2 s. The search starts 200 m before the corner entry. | `BrakingOnsetTests`, `BrakingMetricsTests` |
| `ExitMetrics` | Throttle pickup, exit speed | Throttle 20 / 10 %, otherwise longitudinal G 0.10 / 0.05 g. Minimum 0.2 s; a lift must come first. The window is the following straight, otherwise +200 m. | `ExitMetricsTests` |
| `TheoreticalBest` family | Fastest time per sector with its donor lap; actual best; loss | Results from another revision are ignored. Any untimed sector means no total. The axis is the canonical run's fastest eligible lap. Loss = actual − fastest. | `TheoreticalBestTests` |
| `TimeLoss` | Loss windows between laps; the ranking | Increment = A − B per segment. A straight starting ≤ 0.5 m after a corner continues it. Only positive increments count, the reference lap is skipped, and rows sort by loss. | `TimeLossTests` |
| `Consistency`, `DrivingVariability` | Median and interquartile range; spreads per corner | Finite values only, n ≥ 3. Measured and inferred are kept apart. Line offset is measured at the apex. A line spread counts only when it exceeds the GPS accuracy. | `ConsistencyTests`, `DrivingVariabilityTests` |
| `DrivingStates`, `CoastingAnalysis` | Braking, throttle, cornering and coasting intervals | Brake 10 / 5 %, throttle 15 / 8 %. Inferred braking 0.15 / 0.08 g, acceleration 0.10 / 0.05 g. Cornering 0.30 / 0.20 g. Moving ≥ 10 km/h. Minimum 0.2 s. A recorded pedal is never inferred. | `DrivingStatesTests` |
| `GgPairs` | G-G pairs and peaks | Longitudinal samples set the clock; lateral is interpolated within its gap threshold. m/s² is converted (÷ 9.80665); other units are refused; beyond ±4 g is excluded. | `GgPairsTests` |
| `ChannelSummary`, `OutingChannelSummaries` | Min, max, mean, coverage, cooling; temperatures and heart rate per run | Trapezoid mean, joined only within the gap threshold. Temperatures −40 to 250 °C; heart rate 30–230. Cooling = a drop ≥ 5 over ≥ 30 s. | `ChannelSummaryTests` |
| `MapLayers` | Colour layers on the map | ≤ 4,000 positions. Neighbouring samples must be plausible and within the gap threshold. | `MapLayersTests` |
| `FocusAreas` | Up to 3 areas to look at next | Sector gap ≥ 0.05 s. Loss ≥ 0.05 s on ≥ 3 laps. Measured braking spread ≥ 10 m. Minimum-speed spread ≥ 5 % of the median. One area per segment. | `FocusAreasTests` |
| `DayReport`, `OutingDayReport` | The day report | A result whose decisions key no longer matches is marked stale and its value dropped. Top 10 losses. | `DayReportTests`, `TelemetryAppTests` |

**One day, from laps to the report**

1. **Laps.** Each run is verified, its laps are derived, the track is inferred, and lap rows and references are built.
2. **Groups.** Runs get a layout and direction, and laps form compatibility groups. Off-line laps are flagged.
3. **Ranking.** For the chosen group: ranking, progression and lap consistency over the eligible laps.
4. **Segments.** They are approved by the user, or automatically in Overlays only.
5. **Sector results.** Every eligible lap is projected onto the canonical axis. Sector times and corner metrics are computed, then the theoretical best.
6. **Published results:** the theoretical best against the actual best, sector consistency, corner variability, the time-loss ranking and sector progression. Channel summaries are computed alongside.
7. **Focus areas** come from the sector gaps, the losses and the corner observations.
8. **The day report** is assembled and keyed by a decisions hash over the group, segments, configuration, eligible laps, exclusions and best lap.

**Rules that apply everywhere**
- **Gaps:** every scan splits at a gap or a non-finite sample and never fills with zero.
- **No invented channels:** deceleration stands in for braking only when no brake channel exists. Speed never comes from GPS.
- **Provenance:** every value is measured, calculated, inferred or unavailable. Comparisons refuse mixed methods, channels or units.
- **Units:** a declared unit must match the threshold's unit; an undeclared unit is used but flagged. Nothing is rescaled except for G-G.
- **Lap identity:** exact bounds plus content hash. One eligibility function feeds the ranking, consistency, the theoretical best and the decisions key.
- **Segments:** only segments approved for the exact compatibility reference are used. Proposals never feed a metric.
- **Coordinates:** time is the recording's seconds; distance is metres from the gate on one shared axis. Local metres use an equirectangular projection with R = 6,371,000 m.

**Check before re-implementing.** These are findings in the current code, not decisions.
- **Thresholds differ between modules.** Inferred braking is 0.30 / 0.15 g in `BrakingOnset` and 0.15 / 0.08 g in `DrivingStates`. Throttle is 20 / 10 % for pickup and 15 / 8 % for driving states. Whether this is intentional is unclear.
- **Trace density.** Fixed in KAN-220: a lap trace keeps up to 4,096 points and `projectLapTrace` up to 8,000 fixes per lap, each spread evenly by time order (`keepEvenlySpread`). A whole-number stride used to keep about half the budget once a lap was just over it, and the projection used to pick fixes by latitude extremes.
- **Progress order.** Fixed in KAN-152: projection may step back up to 3 m without losing lock, and `projectLapTrace` holds such a fix at the previous progress, so progress never falls within a segment (`TrackProgress.h` now says non-decreasing).
- **Track progress on a jittery axis.** Fixed in KAN-237 (as Telemetry FET-249). GPS jitter makes a recorded lap longer than the axis resampled from it, so a 25 Hz RCZ lap's search window, centred at progress divided by the spacing, fell 16 m behind after 1.7 km and the lock was lost. The window is now centred by the axis's cumulative distances (`indexAtProgress`). The first fix of a segment after a gap may also be up to 30 m behind the last one: with the 3 m tolerance, a fix 4 m back had moved the rest of the lap one lap on.
- **Different axes.** Segments are approved on the reviewed lap's axis but timed on the canonical lap's axis, so boundaries can shift by a few metres ([testing](testing.md)).
- **Theoretical-best ties.** A tie keeps the first lap in population order, which is not defined within one run.
- **Focus-area speed units.** The app never sets the unit, so minimum-speed areas use the recording's own units.

### Not to port

Telemetry never has video. These parts of this repository are for Overlays only:

**Video and overlays**
- **Video features of the analysis window:** the lap-detail video and its follow mode, and the comparison video column ([`ComparisonVideoPane.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/ComparisonVideoPane.qml), `AnalysisControllerComparisonVideo.cpp`). "Lap A here…" and coasting episodes keep their cursor jump; only the video follow goes.
- **Video synchronization:** `SyncTransform` and the video↔telemetry time transforms in `TelemetrySession.h`, and the per-run `sync` written at import.
- **Overlay presentation rules:** value hold, smoothing and GoPro GPMF ([telemetry semantics](telemetry-semantics.md#overlay-presentation)). Also the export pipeline, widgets, templates and video fingerprints.

**`TelemetrySyncEngine`**
- It is the video sync engine, but `RecordingAlignment` reuses its correlation core and thresholds. Port that core only if Telemetry aligns recordings.

**Desktop and Qt mechanics**
- The desktop session lock (`GuiSessionLock`), the Qt storage migration (`LegacyStorageMigration`), and the cache's locking and reservations. These are mechanics, not behaviour.

## Invariants to carry over

These come from this repository's [`AGENTS.md`](../AGENTS.md). They apply to the
new app whatever its architecture.

**Parsing and data**
- Bound untrusted input (recordings, JSON documents, metadata) before large
  allocations or recursion.
- Test every parser change, including malformed input.
- Parser output timestamps are strictly monotonic.
- Public boundaries never expose `NaN` or infinity. Values outside the range and
  missing values are "no data"; gaps are never bridged.
- Heart rate comes from the imported VBO/RCZ recording; there is no separate
  heart-rate source.
- Never invent brake telemetry, and never substitute another channel silently.
- Analysis never depends on video or frame rate; all timing is time based.

**Documents**
- Saves are atomic. New, open and quit respect unsaved changes. Opening validates
  and commits a document as one transaction.
- A document and its recordings are separate. A missing or moved recording never
  prevents a valid document from opening. Relative references are preferred, and a
  recording is never accepted only because its pathname matches.
- The saved document is the authoritative clean state. Recovery data is separate,
  represents unsaved changes, and is never silently marked clean. Discard removes
  it. Recovery is offered only when it is newer than the saved document.

**Background work**
- Results of background parsing and analysis are guarded by a generation number and
  the source identity; a stale result never changes committed state.
- Long operations are cooperatively cancellable. Generation checks and
  cancellation are both required.

**Display**
- Static geometry, such as the track map, is not rebuilt on cursor or time updates.
  Moving markers update separately.
- A chart distinguishes "no data in this range" from a failure. Series keep their
  segments, so telemetry gaps stay visibly disconnected.

**Process**
- One active Jira task at a time, a focused commit, and recorded build and test
  evidence. Jira content is in English.
- Documentation is part of every iteration.
- Never claim that something works without running it.
- Report real-recording results separately from synthetic tests.
- No secrets, user paths, generated media or build output in Git.
- Check the version, maintenance and licence of every new dependency.

## Analysis feature checklist (the maturity bar)

[KAN-169] uses this list to decide when Overlays can drop its analysis window. The
owner has the final say.

The checklist lists capabilities, not screens; the mobile interface will differ.
Each line names the QML view and the controller entry point. KAN-166 step 4 deleted the
analysis QML from `main`, so the view links point at commit `7eae6cd`, the last one with
the full analysis (KAN-169):

- **A:** `AnalysisController` ([header](../native/src/app/AnalysisController.h));
- **D:** `DocumentController` ([header](../native/src/app/DocumentController.h)).

User-guide pages are under `user-guide/pages/`. Video features are listed
separately because Telemetry never has video.

**Import a day**

- [ ] **Start an outing.**
    - **Flow:** the user names it and adds VBO/RCZ files or a folder (optionally with subfolders), or drops files. Progress has Cancel and import notes.
    - **Code:** [`AnalysisStartPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/AnalysisStartPanel.qml); D `importAnalysisRuns`, `importAnalysisFolder`, `importAnalysisSources`, `cancelBatchImport`.
    - **Guide:** [importing](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/docs/user-guide/pages/importing.html#runs).
- [ ] **Pairing and naming.**
    - **Behaviour:** the VBO and RCZ of one session become one run. Runs are named "Session N" in recording-time order.
    - **Code:** [`DocumentControllerImport.cpp`](../native/src/app/DocumentControllerImport.cpp).
- [ ] **Reviewed import.**
    - **Behaviour:** per file, Import, Skip or "Same run as"; the user chooses a new event or appending.
    - **Code:** [`BatchImportDialog.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/BatchImportDialog.qml); D `confirmBatchImport`.

**Day results** ([`OutingLapPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/OutingLapPanel.qml); [analysis guide](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/docs/user-guide/pages/analysis.html))

- [ ] **Compatibility groups:** an automatic choice, a picker, and a card per group with its best lap and the best lap of each run. A `outingCompatibilityGroups`, `selectOutingComparisonGroup`.
- [ ] **Best of the day:** "Best day · 1:49.898 · Session 5 · LAP 2" opens that lap. A `outingRanking`, `selectOutingLapReference`.
- [ ] **Lap list:**
    - **Columns:** chronological order; OUT / LAP n / IN / UNKNOWN; best-of-day, best-of-run, excluded and GPS notes; duration; eligibility.
    - **Code:** A `outingLaps`.
- [ ] **Ranking details:** best by run, and the exclusions applied. A `outingRanking`.
- [ ] **Correct the grouping:**
    - **Actions:** set the layout name and direction, apply them to matching routes, inspect the GPS trace, or use the detected route.
    - **Code:** A `runTrackConfiguration`, `confirmRunTrackConfiguration`.
- [ ] **Analysis status:** notices and a retry. A `outingAnalysisStatus`, `retryOutingAnalysis`.

**Lap detail** ([`OutingLapDetailPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/OutingLapDetailPanel.qml), [`TrackMapPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/TrackMapPanel.qml), [`AnalysisPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/AnalysisPanel.qml))

- [ ] **Lap header:** "LAP 2 · 1:49.898" with the run name; back to all laps. A `selectedOutingLap`, `closeOutingLap`.
- [ ] **Exclude or restore a lap** with a reason. A `setOutingLapExcluded`.
- [ ] **Track map** with a position cursor; GPS gaps stay open. A `outingLapTrack`, `outingLapTrackPoint`.
- [ ] **Charts:**
    - **Behaviour:** up to four channels on a time axis (defaults: speed, lateral G, longitudinal G), remembered. Values at the cursor, zoom and reset, a section time slider.
    - **Code:** A `outingLapSeries`, `outingLapChannels`, `outingLapCursor`.
- [ ] **Coasting:**
    - **Behaviour:** a summary per lap and per segment, measured from the pedals or inferred. Episodes are shown on the map and move the cursor.
    - **Code:** [`CoastingPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/CoastingPanel.qml); A `outingLapCoasting`.

**Segments** ([`SegmentReviewPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/SegmentReviewPanel.qml); [segments guide](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/docs/user-guide/pages/segments.html))

- [ ] **Automatic segments** from the day's best lap. A `setAutomaticSegments`. Overlays switched this on until KAN-166 step 2 (October 2026); it no longer does, so only Telemetry writes automatic segments (see [the shared contract](#the-shared-contract-fetproject)).
- [ ] **Proposals:**
    - **Shown:** state, turn angle, boundaries with tolerance, apex, uncertainty notes.
    - **Actions:** approve, reject, edit, approve all, recompute.
    - **Code:** A `requestSegmentReview`, `approveSegmentProposal`, `segmentReviewMapLayers`.
- [ ] **Edit approved segments:**
    - **Actions:** move a boundary (pick on the map; adjoining segments follow), split, merge with the next, revoke, undo and redo.
    - **Code:** A `editApprovedSegment`, `splitApprovedSegment`, `mergeApprovedSegments`, `undoSegmentEdit`.

**A/B comparison** ([comparison guide](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/docs/user-guide/pages/comparison.html))

- [ ] **Choose laps:**
    - **Rule:** A and B come from the same compatibility group. The user can swap them, or set B to the best of A's run or the best of the group.
    - **Code:** [`ComparisonLapDialog.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/ComparisonLapDialog.qml); A `selectComparisonLap`, `useBestComparisonLap`.
- [ ] **Charts:**
    - **Behaviour:** on a shared track-position axis, up to four channels that both laps recorded, plus Δ time; values for A, B and A−B; zoom and pan. The pair, the zoom and the channels are saved in the document.
    - **Code:** [`ComparisonDetailPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/ComparisonDetailPanel.qml); A `comparisonChannelSeriesByProgress`, `comparisonDeltaSeriesByProgress`.
- [ ] **Overlay map:**
    - **Behaviour:** both traces to scale with A/B markers. Colour layers: speed, Δ time, lateral and longitudinal G, throttle, measured brake, recorded temperatures.
    - **Code:** A `comparisonMapLayerOptions`, `comparisonMapLayer`.
- [ ] **Corner Analyzer:**
    - **Behaviour:** per segment, A/B/Δ sector time, speeds, braking point, throttle pickup, heart rate and trail braking.
    - **Code:** [`ComparisonSegmentPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/ComparisonSegmentPanel.qml); A `comparisonSegmentMetrics`, `comparisonTrailBraking`, `comparisonHeartRate`.
- [ ] **G-G:** a scatter over the zoomed range, with peaks and sample counts. [`ComparisonGgPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/ComparisonGgPanel.qml); A `comparisonGgScatter`.

**Day views** ([day report guide](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/docs/user-guide/pages/day-report.html))

- [ ] **Theoretical best:**
    - **Behaviour:** best lap, theoretical best, laps today and the time available. A map coloured by loss per segment. "Where the time is" opens the Corner Analyzer. Variability.
    - **Code:** [`TheoreticalBestDialog.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/TheoreticalBestDialog.qml); A `requestOutingTheoreticalBest`, `openTheoreticalBestSector`.
- [ ] **Ranked time losses:** each loss opens its comparison. [`TimeLossDialog.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/TimeLossDialog.qml); A `outingTimeLossRanking`, `openTimeLoss`.
- [ ] **Progression:**
    - **Laps by run:** run cards, distributions, change against the previous run, notes, conditions and setup. [`OutingProgressionDialog.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/OutingProgressionDialog.qml); A `outingProgression`.
    - **By section:** a section × session grid. [`SectionProgressionView.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/SectionProgressionView.qml); A `outingSectorProgression`.
    - **Car and driver:** heart-rate and temperature trends, and temperature against lap time. [`CarDriverView.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/CarDriverView.qml); A `outingChannelSummaries`, `outingTemperatureAssociations`.
- [ ] **Day report:**
    - **Cards:** best lap and what is left, where to look next, losses, sessions, consistency, car, heart rate. Each card opens its evidence.
    - **Code:** [`DayReportDialog.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/DayReportDialog.qml); A `outingDayReport`, `openFocusArea`.
- [ ] **Run details:**
    - **Fields:** name, notes, conditions, setup changes.
    - **Recordings:** attach, make primary, check the clock, fuse.
    - **Code:** [`RunDetailsDialog.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/RunDetailsDialog.qml); A `updateRunMetadata`; D `attachRunRecording`, `reviewRunFusion`.

**Owner to decide**

- [ ] **Single-recording mode:** one recording without an event, shown as map, charts and a lap-timing table ([`LapTimingPanel.qml`](https://github.com/FlappedEar/Overlay/blob/7eae6cd36bea97e96b29854b5cdc21ecbee6a32a/native/qml/LapTimingPanel.qml)). It runs on the editor's controller, not on `AnalysisController`. Whether Telemetry needs it is open.

**Computed but not shown.** The controller exposes `outingLapSectorTimes`, `outingLapCornerSpeeds`, `outingLapBrakingMetrics`, `outingLapExitMetrics`, `comparisonTimeLossObservations` and `approveCertainSegmentProposals`, but no QML calls them.

[`TelemetryController.h`](../native/src/app/TelemetryController.h) already combines the document and analysis controllers without video or an editor (Qt Core only). It is the closest existing model of the Telemetry app's scope.

## Reference data and figures

**Fixtures in this repository.** They are synthetic data files and may be copied; they are not code.

| File | Content |
| --- | --- |
| [`basic.vbo`](../native/tests/fixtures/basic.vbo) | 411 bytes: a header with `coordinate units = degrees`, then three samples with speed, rpm, throttle, brake, heart rate, G, and an unknown column |
| [`event-laps.vbo`](../native/tests/fixtures/event-laps.vbo) | 414 bytes: a short recording for lap tests |
| [`event-demo.fetproject`](../native/tests/fixtures/event-demo.fetproject) | 1,135 bytes: an event document |

Most tests build their recordings and documents in code: [`RczFixture.h`](../native/tests/RczFixture.h) builds RCZ archives, and [`EventProjectFixture.h`](../native/tests/EventProjectFixture.h) builds event documents.

**The private real day.** The recordings are in the private repository `FlappedEar/refdata`. Never copy them into a public repository or into the app's repository: they contain GPS traces and heart rate.

- **Dataset:** six VBO recordings from the Jastrząb circuit, 33.6 MiB in total, with 25 timed laps. `FlappedEar/refdata` holds no RCZ files; RCZ support was developed against one private RaceChrono Pro 10.2.4 recording that is not there ([RCZ format](rcz-format.md)).
- **Last reference figures** ([handover](../handover.md), "Real data"):
    * best lap 1:49.898 against a theoretical best of 1:47.905, so 1.993 s available;
    * focus areas: Corners 9–16 (+0.619 s against Session 6 · LAP 3); Corners 2–3 lost in 5 of 5 session bests; the braking point for Corners 5–6 spread over 20.6 m;
    * oil up to 128;
    * heart rate 119–136 bpm per session.
- **Measured performance** ([testing](testing.md), KAN-77; 27 September 2026, Mac mini M4, Release build):

  | Phase | Time | Resident / peak memory |
  | --- | --- | --- |
  | Import | 1.76–1.82 s | 85 / 89 MiB |
  | Lap derivation | 1.44–1.45 s | 103 / 116 MiB |
  | Open a lap | 0.27 s | 104 MiB |
  | Choose an A/B pair | 0.60–0.61 s | 96 MiB |

  These figures are a starting point for the phone memory budget (KAN-129).
- **Channels on the owner's car** (RaceChrono Pro with OBD):
    * `accelerator_pos-obd` is the pedal, 0–100 %;
    * `throttle_pos-obd` is the throttle plate: 13.3 % at idle and 80.4 % fully open. It is fully open from about 70 % pedal, and blips on downshifts with the pedal at 0;
    * speed is `velocity` in km/h;
    * brake is `brake_pos-obd`.

**Reproducing the figures here.** Build this repository (see [testing](testing.md)), then run:

```bash
# analyzesPrivateTrackDayCorners exists up to Overlays commit 7eae6cd (KAN-169); KAN-166 step 4 removed it.
git checkout 7eae6cd && FLAPPEDEAR_REAL_DAY=/path/to/day ./build-native/native/tests/flappedear_native_tests analyzesPrivateTrackDayCorners
FLAPPEDEAR_REAL_DAY=/path/to/day ./build-native/native/tests/flappedear_telemetry_app_tests measuresAPrivateFullDay
```

## Reading list

1. [Product vision](product-vision.md) and the
   [product split plan](product-split-plan.md): "Why", "Documents" and
   "Mobile specifics".
2. [Event projects](event-project-format.md) and
   [project format](project-format.md): the shared contract.
3. [Telemetry semantics](telemetry-semantics.md): timestamps, missing values, lap
   timing, segments, driving states, map layers, temperatures, clock alignment and
   channel fusion.
4. [RCZ format](rcz-format.md) and [batch import](batch-import.md).
5. The user guide pages under `docs/user-guide/pages/`, especially
   `telemetry-data.html`, `importing.html`, `analysis.html`, `comparison.html`,
   `segments.html` and `day-report.html`. These show what the driver sees today.
6. [Testing](testing.md): one section per feature ticket, recording what was
   verified and how. It is the most detailed behavioural record.
7. [Event analysis plan](event-analysis-plan.md) and the acceptance records
   (`docs/kan28-m1-acceptance.md` to `docs/kan79-full-day-acceptance.md`).

## Cross-references

| Jira | Meaning for the new app |
| --- | --- |
| [FET] | The new Jira space, "FlappedEar Telemetry", for all Telemetry work |
| [KAN-165] | Separation epic; stage 2 is this app |
| [KAN-167] | Decision: Flutter, a blank page, no shared code |
| [KAN-168] | This handover |
| [KAN-170] | The shared `.fetproject` contract; create a twin ticket in the new project |
| [KAN-169] | Maturity gate; the new project's parity milestone should link to it |
| [KAN-166] | Removal of the Overlays analysis window and day import; done 5 October 2026 |
| [KAN-126]–[KAN-130] | Mobile targets, import, touch screens, memory budget and on-track acceptance; re-create in the new project |
| [KAN-122] | Decision 5: iOS 15 and Android 8.0 to start with; desktop open. Decision 1 (store licence) was about Qt and does not apply to a Flutter app without Qt |
| [KAN-162] | Windows: code changes resumed, CI and packaging paused |

## Open decisions

- The licence of the new repository (owner; deferred).
- Desktop minimum OS versions (owner and architect).
- Where the shared fixtures live, and who owns schema changes ([KAN-170]).
- Whether desktop or mobile ships first (owner and architect).
- [Event projects](event-project-format.md) calls the schema developmental, with
  "no promise to maintain future migrations". With two apps and the no-data-loss
  condition, [KAN-170] should decide whether that still holds.

[FET]: https://kozucharkadiusz.atlassian.net/browse/FET
[KAN-122]: https://kozucharkadiusz.atlassian.net/browse/KAN-122
[KAN-126]: https://kozucharkadiusz.atlassian.net/browse/KAN-126
[KAN-130]: https://kozucharkadiusz.atlassian.net/browse/KAN-130
[KAN-162]: https://kozucharkadiusz.atlassian.net/browse/KAN-162
[KAN-165]: https://kozucharkadiusz.atlassian.net/browse/KAN-165
[KAN-166]: https://kozucharkadiusz.atlassian.net/browse/KAN-166
[KAN-167]: https://kozucharkadiusz.atlassian.net/browse/KAN-167
[KAN-168]: https://kozucharkadiusz.atlassian.net/browse/KAN-168
[KAN-169]: https://kozucharkadiusz.atlassian.net/browse/KAN-169
[KAN-170]: https://kozucharkadiusz.atlassian.net/browse/KAN-170
