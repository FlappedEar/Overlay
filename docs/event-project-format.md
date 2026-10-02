# Event projects (development v3)

The event project format is shared by the current desktop application and the planned applications (desktop overlay editor on macOS and Windows, Flapped Ear Telemetry on iOS and Android; see [product split plan](product-split-plan.md)).
The project schema is developmental: the owner confirmed that no existing user
data needs migration protection. v3 gives events a single authoritative source
model; the small existing v2 read/write path remains for single-recording files.
This is not a frozen schema or a promise to maintain future migrations.

## Ownership

An event owns an ordered list of runs and an `activeRunId`. Each run owns:

- Stable `id`, display `name` and `primaryTelemetrySourceId`.
- `sources.telemetry`: 1–8 source entries, each with an event-wide unique `id`
  and a `reference` containing relative/absolute paths and an optional fingerprint.
- Optional `sources.telemetry[].contentSha256`: exactly 64 lowercase hex
  characters identifying the complete recording, independent of its pathname.
  New imports write it; matching legacy import provenance also supplies this
  identity. Untouched legacy documents are not rewritten during loading.
- Optional `sources.video` reference and its `sync.offset`/`sync.timeScale`.
- `trackConfiguration` on new imports: explicit layout/direction/gate identity,
  bound to the primary source ID and fingerprint (details below).
- Optional `fusion`: an approved source-fusion decision (KAN-103, details below).
- Optional metadata such as `notes`; bounded unknown fields survive round trips.

Event/run/source IDs are document identities, not filenames or import proposal
hashes. Import confirmation will allocate them once; relinking does not replace
these IDs. Alternate exports are retained, but only the explicit primary source
is loaded. There is no RCZ-over-VBO priority. Channels of one alternative join
the run's analysis only through an approved `fusion` decision.

Laps remain derived from the selected source and source gates, not serialized
sample arrays or cached lap times. Published lap summaries include `runId` and a
portable `reference` object. Display lap numbers and row indices are not durable
annotation/bookmark identities; use the derivation-bound reference below.

The widget scene, analysis channel selection and export/map settings are shared
by the document. Per-run scenes and per-run export ranges are not implemented.
Only one run's telemetry and video are loaded at a time. Selection clears prior
loaded data and starts cancellable, generation-guarded source loading; a missing
primary stays missing even if an alternative exists. Missing external assets do
not invalidate an otherwise valid event document.

## Track configuration and derivation identity (KAN-19)

New imports persist the following run-local configuration. Layout IDs are opaque
identifiers assigned by supported geometry matching or manual correction, not
filenames. Reuse a manual ID only for the same physical layout. Direction is
independent of a gate's crossing sign. Unset manual fields remain `unknown` while
automatic evidence supplies the effective grouping configuration.

```json
"trackConfiguration": {
  "layoutId": null,
  "direction": "unknown",
  "gateRevision": null,
  "sourceId": "primary-source-id",
  "sourceFingerprint": {}
}
```

`layoutId` is null or a nonblank ID up to 128 characters. Direction is `unknown`,
`clockwise` or `counterclockwise`. `gateRevision` is null or `gates-v1:` followed by
64 lowercase SHA-256 hex characters. Import derives a revision from the ordered
source gate types and endpoints, normalizing explicit west-positive longitude to
east-positive for identity only. Names/descriptions do not change the revision;
endpoint, gate type/order or gate count changes do. Missing/ambiguous start gates,
unknown gate types, invalid coordinates or more than 128 gates remain unresolved.
A known revision identifies geometry; it does not establish lap eligibility.

`sourceId` must name this run's primary source; `sourceFingerprint` must equal
that source reference's fingerprint. Foreign, alternative or stale bindings are
rejected before project commit. Legacy v3 runs without the object read as unknown;
opening them alone does not assert a layout, direction or gate revision.

Save, Save As, recovery and run switching retain the configuration. Same-content
relinking preserves it. Replacing source content clears its asserted fields to
unknown and binds them to the replacement fingerprint. The controller API
`setRunTrackConfiguration(runId, layoutId, direction)` supports validated explicit
assignment or clearing (empty layout ID / `unknown` direction), records a normal
persistent edit, and leaves the loaded editor source intact. Step 012 exposes this
through **Correct grouping…**; ordinary imports use automatic GPS route/direction
inference, described below. Manual values override automatic evidence.

`lapDerivationKey` combines a version tag, run ID, primary source ID/fingerprint
and configuration. Names, notes, video synchronization and source path spelling
are excluded. Outing-analysis requests include this identity: changing layout,
direction, gate revision or source cancels/rejects old work and closes stale lap
detail. Analysis also checks asserted gate revisions against the loaded recording
before publishing rows. Existing source fingerprint and generation checks remain.
Compatibility groups are derived in memory as described below. Portable lap
references bind to this derivation identity. Unknown identities never establish that two
runs are compatible merely because their unknown values match.

## Stable lap references (KAN-20)

Every published OUT/LAP/IN/UNKNOWN section has a portable JSON `reference` with:

| Field | Contract |
| --- | --- |
| `version` | Numeric `1`; this schema has exactly these ten fields |
| `algorithm` | `source-laps-v1`; bump when lap detection/section semantics change |
| `eventId`, `runId`, `sourceId` | Event/run/primary source identities, each a nonblank string up to 128 characters |
| `sourceRevision` | Full-file SHA-256, 64 lowercase hex characters |
| `derivationKey` | Step 009 run/source/configuration key, 64 lowercase hex characters |
| `type` | `OUT`, `LAP`, `IN` or `UNKNOWN` |
| `startTime`, `endTime` | Exact finite telemetry seconds; `0 <= startTime < endTime` |

Store the whole object alongside a future annotation or selection. JSON number
round trips preserve its bounds. Reopening the same event/source/derivation,
Save As, same-content relocation, renaming a run and reordering display rows do
not change the reference. Source replacement, gate/configuration edits or a new
algorithm cannot silently rebind it to a matching lap number or nearby time.
References to partial/unknown sections identify those sections; they do not make
them eligible for rankings. No annotation/bookmark storage UI is added here.

`resolveOutingLapReference(reference)` returns a `state` and, only when resolved,
a current `index`. Failure states include a reason:

| State | Meaning |
| --- | --- |
| `resolved` | Exactly one current derived row matches every reference field |
| `invalid` | Malformed/unsupported schema, identity, digest, section or bounds |
| `stale` | Different event/run/source/configuration/algorithm, changed content/bounds or ambiguous matches |
| `loading` | Current derivation is not ready, including edits before the refresh timer runs |
| `unavailable` | Referenced run exists but its recording has no available derivation |

Resolution describes the current derived snapshot. `selectOutingLapReference`
opens only resolved references; keyboard, mouse and accessibility row actions use
this API. Invalid/stale/unavailable references never select a substitute. The
index API also rejects outdated snapshots before a queued refresh can run.

The worker computes full content identity before and after lap derivation, using
the shared bounded/cancellable SHA-256 reader (128 MiB/file, 64 KiB blocks; existing
256 MiB outing/batch limits remain). Detail loading checks the referenced digest
again before parsing and verifies it afterward. An external edit since the last
snapshot produces an explicit stale-reference error, with no old track/series
published. The resolver then marks references to that run stale until a fresh
derivation is published; a missing source produces an unavailable recording error. This closes
the gap left by sampled fingerprints for same-size edits outside sampled blocks.
The existing faster project fingerprint/relink policy remains in place. Full
hashing adds bounded sequential reads; native/private-file performance acceptance
is separate from synthetic correctness tests.

## Persistence and limits

v3 forbids root `sources`, `sync`, `videoPath` and `vboPath`. A temporary single-run
editor projection feeds existing source loading, preview and export; it is never
stored alongside the event. v2 with an `event` object is rejected rather than
silently dropping the event on save. A previous app that only knows v2 rejects v3.

An event contains 1–64 runs, at most 8 telemetry sources per run and at most 128
telemetry sources overall. IDs are nonblank strings up to 128 characters; names
up to 160. The common 4 MiB project/recovery payload, depth, string and scene
limits still apply. Active/primary references, unique identities, reference
shapes and finite positive time scales are validated before controller commit.
Dates and track configurations are not inferred from filenames or file times.

Save and recovery use the existing atomic writers and logical document revision.
Selecting a different run is a persistent edit: it keeps the document identity,
dirty edits and last-saved revision; it does not reopen the project as clean.
It preserves the open Analysis window and current template provenance. Selection
is blocked during project loading, export, startup recovery decisions and dirty
new/open/quit decisions. Late results cannot restore the previously selected run.

Save As rebases all source references, including inactive runs, alternatives and
missing video/telemetry. Resolution prefers an existing relative location, then
the absolute fallback. If neither exists, the previous document's relative
location is retained as the intended location. An old relative spelling is never
reused against an unrelated Save As directory. Existing directory symlinks are
resolved before rebasing a missing source, so Save As through a linked folder
does not introduce a dependency on the old link name. Existing fingerprint checks and
explicit mismatch confirmation still apply on relink.
Export target protection includes all event references, including inactive runs
and alternative exports; overwrite consent cannot turn a source into an output.

## Try this slice on macOS

1. Build the development branch using the normal native build instructions.
2. Open `native/tests/fixtures/event-demo.fetproject` using Open Project, then
   open the Telemetry Analysis window. This fixture has two deliberately synthetic,
   independent runs; it is not a real track day or performance comparison.
3. Use **Active run** to switch between GPS lap data and the basic channel fixture.
   No video is required. This slice does not add cross-run comparison or change
   the existing video-dependent lap navigation list.
4. Change sync on one run, switch away/back, and verify the value is run-local.
   Use **Save As** outside the fixture directory and reopen that saved project.
   Keep the original source fixtures available. The scene is initially empty;
   apply a widget template if testing an overlay with your own video binding.

Use **File → Import telemetry runs…** to create an event from multiple VBO/RCZ
files, or add runs to the current event. See [batch import review](batch-import.md).
The separate single-file **Open telemetry…** picker still replaces the active
run's primary source; it does **not** add another run.

Automated coverage: `flappedear_event_project_tests` validates the schema,
resource bounds, reference rebasing and recovery round trip; controller tests
cover run selection, laps, dirty state, Save As, relink/fingerprint policy,
recovery, stale worker rejection and the actual Analysis QML selector. Existing
native, import, RCZ, render/export and startup suites remain required. Hosted
checks do not replace interactive macOS or private-recording validation.

## User lap exclusions (step 011)

An optional `event.lapExclusions` array stores `{reference, reason}` entries.
`reference` is the step-010 portable LAP reference: event, run, source, complete
source SHA-256, derivation key, algorithm version and exact time bounds. Reasons
must contain text, contain no NUL, and fit within 256 characters. Duplicate
references and more than 20,000 entries are rejected, alongside the existing
project byte limit. Legacy documents without this array include every otherwise
eligible lap.

In lap detail, enter a reason (for example Traffic or Cooldown) and choose
**Exclude lap**. **Restore lap** removes the exclusion. The lap, map, charts and
cursor remain inspectable. Excluded rows show their reason. Save, Save As and
unsaved-document recovery preserve exclusions through the ordinary project
transaction. Source or derivation changes never transfer a reason by lap number
or nearby time: unmatched entries remain saved and a notice identifies the count.
The controller API also permits explicit removal of a historical reference.

`TimedLap::referenceEligible()` combines GPS quality with user exclusion.
`eligibleLapIndices()` is the shared input policy for ranking and future
potential/statistics calculations; those later analysis features are not introduced
by this step. Recomputing best/deltas retains all measured laps and raw traces;
restoring a GPS-ineligible lap does not make it a valid reference. An all-excluded
run has no best lap. Preview and the export worker use the same policy, and export
verifies the complete source revision before and after deriving laps.

Validation includes malformed documents, exact-reference invalidation, all-excluded
ranking, shared render-context results, keyboard-operated production QML controls,
and actual save/reopen/recovery flows. Use the local macOS Debug build/test gate;
Cloud CI remains paused. Private recordings and manual acceptance remain separate.

## Automatic compatibility groups and corrections (steps 012/016)

Importing sufficiently supported recordings automatically groups matching routes
and directions, activates a group and populates rankings and progression. No
track names, per-run confirmation or group-selection click is required. Multiple
groups show separate best-day summaries and retain separate progression results.
An explicit saved group choice takes priority; an unavailable explicit choice is
retained without substituting another group.

Use **Correct grouping…** only for an incorrect or ambiguous match. Choose a
recording, enter a layout name and correct the travel direction. The name is the
layout ID: leading/trailing whitespace is removed, but spelling and case must
match to join the same layout. The decision applies to all laps in that recording
and uses the existing persisted source-bound configuration. **Apply to recordings
with matching GPS routes** applies one atomic correction to verified spatial
matches. **Use detected route** removes the manual override. An outdated dialog
cannot overwrite a changed derivation. Cancel leaves unresolved fields unchanged.
Gate revisions come from verified source geometry, never from a user override.
**Inspect GPS trace…** opens this recording's lap map and telemetry through the
existing verified detail loader, including partial sections when no timed lap exists.
A recording without a verified gate revision remains unresolved even after its
layout/direction are confirmed. Configuration changes can leave prior lap
exclusions unmatched, as explained in the dialog.

A resolved group requires an exact match of layout ID, direction and timing-gate
revision. Its stable `compatibility-v1:` SHA-256 ID hashes only those fields and a
version, so identical configurations can group different recordings. Unknown or
malformed fields never compare equal for this purpose: unresolved recordings
remain separate, visible entries and cannot be chosen for comparison.

All recorded sections remain visible and inspectable. Ambiguity is explained once
per run, rather than repeating layout/direction prompts beneath every lap. OUT/IN
sections are ordinary untimed sections, not errors. Eligibility reasons remain independent:
different layout, opposite direction, different/unresolved gates, unresolved
layout/direction, incomplete/invalid GPS, user exclusion, stale source and
incomplete timed section. Several can apply to the same row. Tooltips expose
reasons when the row label is too narrow.

`outingCompatibilityGroups` supplies all LAP members and the eligible member
references separately; OUT/IN/UNKNOWN sections never become comparison candidates.
GPS/user exclusions do not redefine physical compatibility or hide group members.
An available group is active automatically unless the document has an explicit
choice. `comparisonEligible` requires the active group and no blocking reasons.
The explicitly selected group and corrected
run configuration survive save/reopen/recovery (step 016 below). Missing, changed
or in-flight source/derivation generations cannot serve stale group selections. Group membership
and reasons are rebuilt from validated current lap references; they are not saved
as an independent cache. This step does not add lap alignment, potential estimates
or judgments about representative performance.

Regression coverage verifies exact grouping boundaries, unknown-to-unknown
rejection, simultaneous GPS/user/compatibility reasons, durable explicit decisions,
generation invalidation, and keyboard interaction with the production QML dialog
and group selector. Native verification uses the local macOS Debug gate; Cloud CI
is paused. Manual and private-recording acceptance remain separate.

## Compatible run/day rankings (step 013)

For the automatically active or explicitly chosen group, **Best day** opens its
fastest eligible lap. The matching lap row carries a **Best day in group** badge.
**Ranking details…** shows each run's best lap and an **Applied exclusions** tab;
results carry the run, lap, group and exact portable reference. Selecting a result
opens its source lap through the existing reference resolver and asynchronous
source-content verification. Excluded laps remain available for inspection.

Ranking includes complete LAP sections only, with the shared GPS/user eligibility
policy, a valid current lap reference and no known stale source. Faster laps from
other layouts/directions/gate groups never participate. Unknown compatibility is
not silently promoted. A group or run with no eligible lap explicitly reports
**No eligible lap**, with no winner or best-day badge. The details retain total and
eligible counts plus every applied exclusion's independent reasons and user text.
Historical unmatched exclusions remain covered by the existing outing notice;
they are not presented as applied to newly derived laps.

Timing comparisons use full source precision. Exactly equal durations prefer a
known earlier absolute lap-start timestamp; missing timestamps sort after known
ones. Further ties use stable run ID, telemetry start/end bounds and the canonical
portable reference. Display name, display lap number and input/list order do not
break ties. Equal-duration counts are exposed per run and group; equal displayed
milliseconds do not necessarily mean equal source-precision durations.

The selected group's rankings are derived in memory and recomputed on eligibility,
configuration and source changes. No independent ranking cache is saved. Loading
or outdated generations suppress results immediately, and clearing the project
removes its former winner. Existing same-run badges remain available outside a
selected day comparison; best-day results always identify their compatibility group.

Regression tests cover deterministic ties and input reversal, incompatible faster
laps, unknown groups, combined GPS/user exclusions, invalid references, stale
sources, resource bounds, all-excluded groups, restore/recompute behavior and
keyboard navigation from production QML day/run results. The ranking controls and
lap list are exercised at the analysis window's 760×480 minimum. The verbose
chronology hint is hidden at short heights and notices remain scrollable so the
lap list stays reachable. Native macOS Debug/Release CI is the build/test gate;
private recordings and physical-Mac acceptance remain separate.

## Run details editor (step 014)

Each run owns its display `name` (required, nonblank, at most 160 UTF-16 code
units) and optional plain-text `notes`, `conditions`, and `setupChanges` (at most
4096 UTF-16 code units each). Embedded NUL is rejected. Optional fields may be
absent or JSON null; empty legacy strings also remain readable. Clearing an
existing value saves null. Opening or saving unchanged legacy details does not
invent fields. Unknown conditions and setup stay blank, with no inference from
lap times, filenames or recording dates. Unrelated unknown fields are preserved.

**All laps → Run details…** opens a run selector and a scrollable editor. Save
applies the draft as one persistent edit; Cancel/Escape discards it. Choose
another run after saving or cancelling the current draft. Over-limit multiline
text remains visible for correction and disables Save; it is not silently cut.
Saving details marks the project dirty and uses the normal project Save, Save As
and recovery paths. The fixed action row remains available at the 760×480
minimum analysis-window size.

An edit token binds the draft to the current document and run object. A stale
edit or a project load/export/recovery/destructive operation cannot overwrite
current data. Invalid edits are transactional; unchanged edits do not advance
the document revision. Editing an inactive run does not select or reload it.

Names and annotations are display metadata: they do not change source identity,
lap references, derivation/cache keys, track configuration, exclusions or ranking
eligibility. Current names are applied when publishing cached or newly derived
rows, rankings, source diagnostics and selected-lap labels, without resetting the detail session,
track geometry or cursor.

## Within-day run progression (step 015)

**All laps → Progression…** summarizes the active compatibility
group. Each run shows its best eligible lap, eligible/complete sample counts,
and a five-number lap-time distribution: minimum, Q1, median, Q3 and maximum.
Quartiles use linear interpolation at `(n - 1) * fraction` in the sorted eligible
sample. For one lap all five values coincide; zero eligible laps produce null
statistics. Every eligible lap remains in the distribution, including slow laps;
there is no automatic outlier removal or traffic inference. The best-lap ranking
and progression share the same eligibility calculation (GPS, exact source/lap
identity, compatibility and explicit exclusions).

Run cards use a common time scale for their min/max whiskers, middle-50% box and
median mark; numerical values and sample counts remain visible. The best-lap
button opens its exact source reference. Notes, observed conditions, setup
changes and applied exclusion context are shown alongside each distribution.
The first three exclusions are shown in each card; the existing Ranking details
→ Applied exclusions view retains the full list. Unknown context stays unknown.

Runs with recorded clocks are ordered by their earliest recorded section in UTC.
Unknown clocks follow in project import order and are explicitly labelled. The
best-time difference is against the **previous listed run**, never an inferred
chronological improvement; its previous run name is shown. A run without an
eligible best breaks that difference. Breaks do not create runs, laps, zero
values or interpolated samples. Compatible runs without available rows remain
listed with no recorded laps when the group has other available sections.

Progression is derived session state, not a new project field. Metadata edits and
exclusions refresh it without reloading telemetry; source/configuration changes
suppress stale results until fresh derivation. Group switches use that group's
samples only. A scrollable dialog with fixed close controls keeps the view
reachable at the 760×480 minimum. Synthetic core and production QML tests cover
statistics, eligibility, ordering/gaps, context, source invalidation and lap
navigation; private recordings and physical-Mac acceptance remain separate.

## Durable day-analysis decisions and invalidation (step 016 / KAN-26)

The optional event field stores the existing explicit compatibility choice:

```json
"analysisDecisions": {
  "comparisonGroupId": "compatibility-v1:<64 lowercase SHA-256 hex characters>"
}
```

The value is the exact stable group ID, never its display label or selector
index. An absent object, absent field or null group means automatic selection,
without inventing an explicit document decision. A malformed object, unsupported ID,
oversized string or non-string selection is rejected transactionally by the
ordinary project validator, including recovery reads. **Automatic** stores null and
can remove an unavailable decision. Actual changes mark the project dirty and
use atomic Save, Save As and recovery. Repeating a choice, repeating an exclusion
with the same reason, or loading/restoring decisions does not create an edit.

Choosing a comparison group defines the analysis reference context. Clicking a
lap, a run-best result or Best day only opens that lap for inspection. This
transient detail view is not a new reference-lap/comparison feature and is not
saved as a bookmark. Existing exclusions continue to use the complete portable
lap reference, including event/run/source IDs, full source hash, derivation key,
algorithm, section type and exact time bounds. No telemetry samples, rankings,
group membership lists or progression distributions are serialized.

Saved intent is applied only after current sources and their derivations have
been verified. The selector exposes `none`, `automatic`, `loading`, `applied` and `unavailable`
states. A missing recording or changed configuration leaves the exact decision
saved but unapplied, with **Saved group unavailable** and a retained-decision
notice. It never picks another group or nearby lap. A remaining verified run in
the same group can still supply eligible results. Missing recordings do not
prevent the document opening; verified identical-content relinking restores the
applicable decision without changing logical event/run/source identities.

Full-content verification complements the sampled fingerprint: changes anywhere
in a recording, including outside fingerprint blocks, prevent automatic source
acceptance and analysis reuse when a full identity is available. Legacy import
provenance is used only while its fingerprint still matches the source binding.
An explicit replacement records the new full identity and clears that run's
asserted track configuration, even if the sampled fingerprint stayed equal.
The verified replacement's actual gate revision then enables fresh inference.
Historical exclusions remain saved and unmatched; they are never transferred.
New track decisions on verified legacy recordings also capture the complete
source identity. Save As rebases paths, and moving a project with its relative
recordings preserves the same identities and decisions.

Derived outing rows are cached per run in memory, bounded by the existing file,
batch and 20,000-section limits. Reuse requires the same document/run generation,
source and derivation dependencies, plus a fresh complete-file hash check. New
derivations verify content before and after parsing/derivation. Changing run A's
source, gates, layout or direction cancels/invalidates A's dependent detail and
rows; independent run B retains its verified detail session, geometry and cursor.
Aggregate rankings/progression are suppressed while verification is pending and
then rebuilt from current eligible inputs. Metadata-only changes update labels
and context without reloading telemetry or rebuilding detail geometry.

Outing workers retain cooperative cancellation and current-generation commit
checks. Detail workers use a document/run dependency key plus a request token;
an old completion cannot restore a cancelled selection. New/open/recovery
establish a new analysis generation, including when reopening the same document.
Save As and switching the editor's active run preserve unrelated detail state.

Local regression coverage includes two distinct runs, Save/reopen/Save As,
relative-folder relocation, missing/identical relinking, unsaved recovery,
no-op/clean loading, malformed/legacy fields, unsampled content changes,
independent detail/cache reuse, stale workers and production QML restoration.
Run the local macOS Debug build and CTest gate from
[the development workflow](development-workflow.md). Cloud CI and Windows work
remain paused; private-media, hardware and manual interaction evidence are
reported separately.

### GPS route evidence and tolerances (`gps-route-v1`)

Inference reuses `LapSession::lapTraces`: complete, GPS-eligible gate-to-gate
sections, excluding OUT, IN, unknown sections and gaps. It never uses filenames,
layout names, lap times or shared dates as route evidence. West-positive VBO
longitude is normalized for matching and direction only; source telemetry is
unchanged. At least two supported repetitions are required. Same-day context
does not authorize a match by itself.

Each candidate needs at least 12 points after dropping movements below 3 metres,
closure within 25 metres and a perimeter between 100 metres and 30 kilometres.
Signed enclosed area must exceed 0.5% of perimeter squared; degenerate or
self-cancelling winding is ambiguous. The sign establishes clockwise versus
counterclockwise travel. Each closed route is resampled to 256 points at equal
distance intervals. Cyclic ordered matching tolerates different trace starts and
sampling rates; it does not rotate, translate or reverse a route to force a match.
Perimeters must agree within 5%, pointwise separation must stay within 25 metres,
and RMS separation must not exceed 10 metres. These finite GPS tolerances cannot
distinguish physically separate routes that remain within that envelope; manual
correction remains available.

Separation is measured across the track, from each point to the other route's
path, not to its nearest resampled point (KAN-136). On a 3.6 km circuit the 256
points are about 14 m apart. Two laps on the same line can therefore be sampled
up to 7 m apart along the track, and the point-to-point measure rejected whole
sessions at Silesia Ring as "conflicting routes".

Only the cross-track distance decides whether routes differ. A detour, another
layout or the reverse direction still fails the same limits. The algorithm name
stays `gps-route-v1`: this only admits laps that were wrongly rejected, and
runs that matched before keep their layout identities and approved segments.

The route shape limits are loose enough for a lap that runs wide or cuts off
at a single corner. A second check (KAN-137) therefore compares the laps that
matched in one run against each other. For each point of a lap, it takes the
cross-track distance to the nearest point of any other lap in that run. The
lap's deviation is the largest of those distances. A lap whose deviation is
more than 12 metres leaves the line every other lap took: it went off track,
took a detour or used the pit lane. It is reported as `different-recorded-route`
and stays out of automatic ranking and progression. The check needs at least
three matching laps in the run and is cooperatively cancellable.

On the real days, normal laps deviate by at most 11.2 m. The laps flagged are
Jastrząb Session 2 LAP 4 (43 m) and Session 5 LAP 5 (19 m), and at Silesia
Ring five laps between 12.3 m and 16.0 m. One of those is Session 2 LAP 2,
where the driver went off track.

Up to 64 evenly distributed complete traces establish a representative using
complete-link clustering. A cluster must contain at least two laps and 60% of
usable candidates. Conflicting routes remain unresolved. Every complete trace is
then checked against the supported route; an unmatched timed lap remains visible
but cannot enter automatic ranking/progression for that route. This keeps pit
detours and alternate-route sections out of representative results. Across runs,
every member must match every other member of its group, preventing a chain of
near matches from bridging incompatible routes. A recording that matches two
otherwise incompatible groups stays unresolved with one explanation instead of
being assigned by input order. Reverse traversal and alternate
layouts remain separate. Exact recorded gate revisions still partition timing
results even when route geometry matches; spatial similarity never equates
different timing definitions.

Optional `run.trackInference` records only `algorithm`, complete `sourceRevision`,
`gateRevision`, opaque `layoutId` and `direction`. Strings and identities are
validated and bounded. Source/gate/version bindings must match before a prior
layout ID can be reused. Matching runs share a stable assigned layout ID; an
unchanged run retains that ID if another recording disappears or changes. New
IDs bind the representative run and complete content identity. A saved ID claimed
by incompatible spatial clusters is not reused; those clusters receive distinct
IDs, leaving any old explicit comparison choice unavailable. Raw
GPS descriptors and lap membership classifications remain bounded in-memory data.
Manual `trackConfiguration` fields take precedence, and manual corrections retain
the existing GPS/exclusion safeguards.

Import/source-edit completion records changed inference provenance through the
normal dirty/recovery lifecycle; unchanged inference does not advance the document
revision. Clean reopening reconstructs and verifies results without dirtying the
document. Explicit Save also records available verified provenance. Legacy files
without it reconstruct deterministically without confirmation. Per-run caches
include the inference algorithm and source/derivation dependencies; inferred
grouping does not change portable lap references or transfer saved exclusions.

## Source-fusion decisions (KAN-103)

A run may carry one approved `fusion` object. It adds an alternative
recording's channels to that run's analysis:

```json
"fusion": {
  "algorithm": "channel-fusion-v1",
  "alternativeSourceId": "<a source of this run, not the primary>",
  "primarySourceRevision": "<64 hex>",
  "alternativeSourceRevision": "<64 hex>",
  "clock": {"offsetSeconds": 0.01, "driftPpm": 0.0, "uncertaintySeconds": 0.05,
            "alignmentAlgorithm": "recording-alignment-v1", "resolvedByDeclaredClock": false},
  "rules": [{"key": "speed", "rule": "fillGaps"}]
}
```

**Validation.** The codec validates the decision strictly:
- `alternativeSourceId` must be one of this run's own sources and not its
  primary.
- Both content revisions are 64 lowercase hex characters.
- The clock is finite: |offset| ≤ 86 400 s, |drift| ≤ 1000 ppm, uncertainty
  ≥ 0.
- At most 64 rules, one per channel key, each `primaryOnly`, `fillGaps` or
  `preferAlternative`.

The decision round-trips through save and recovery unchanged.

**Approval.** A decision is written only by approving a fusion review in
Run details:
- The clock alignment (KAN-101) must be "aligned".
- Every channel whose measurements conflict must have a rule.
- The review and approval are guarded by the document revision.

**When it applies.** It applies only while both sources' recorded content
revisions still equal the approved ones. Otherwise the run's analysis uses
its primary alone and Run details shows "Fusion needs review". Relinking a
recording to different content therefore requires a new review. A fused
file that changes on disk is caught when a lap opens: its content hash no
longer matches, so the lap is not opened with it. Changing the run's
primary removes the decision, and **Remove fusion** removes it explicitly.

**What it changes.** Laps remain derived from the primary alone. Fusion
changes only the channels the analysis sees: lap detail, A/B comparison,
channel summaries and theoretical best all load through
`loadOutingLapDetail`. Its session cache key includes the decision. The
overlay editor keeps loading the primary recording.

