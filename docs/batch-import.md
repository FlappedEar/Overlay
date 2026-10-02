# Flapped Ear Telemetry: outings and multi-file import

Product scope and delivery status: [vision](product-vision.md),
[audit and milestones](product-delivery.md).

Lap quality: start-to-start measured times remain visible even with incomplete
GPS. Such laps carry a GPS warning, are excluded from spatial references and do
not show a numeric delta or BEST badge. Best-of-run badges use GPS-continuous
laps only; they do not establish cross-run layout/conditions compatibility or
automatically detect pit visits. Event-wide best laps across compatible runs are shown in Day results.

## Which format should I export from RaceChrono?

**Use VBO for analysis with RaceChrono's calculated lateral and longitudinal G.**
Export the complete run with `latacc-calc` and `longacc-calc` enabled. Flapped Ear
Telemetry uses these columns for automatic G-force channel selection when present.

RCZ preserves the recorded GPS, OBD, heart-rate and device sensor channels at their
original sampling rates. It is useful as the source archive, but the currently
supported RCZ reader does not reconstruct RaceChrono's calculated acceleration.
In the inspected RCZ/VBO pair, those calculated columns exist only in the VBO
export; the VBO has a common 10 Hz timeline. That rate is specific to this export,
not a limit imposed by Flapped Ear Telemetry.

Importing both formats does not combine their channels. Lap Analysis groups uniquely
matching dated exports automatically. In advanced review, to keep both in one run,
leave VBO as **Import as a run** and set the RCZ to **Same run as: [that VBO]**.
Analysis then uses VBO; RCZ remains attached as an alternative source.

## Lap Analysis: the whole day

1. Launch the application and choose **Lap Analysis** on the welcome screen.
2. Enter an **Outing name** (for example, a track and day).
3. Choose **Add RCZ / VBO files…** and select the recordings for the day.
4. **All laps** automatically lists recorded sections from every run, in UTC
   chronological order. Columns show start time, **OUT / LAP / IN**, the source
   run and section duration. No video or run selection is required.
   Runs are named **Session 1, Session 2, …** in recording-time order, not
   after their files (KAN-119). A recording without a clock follows the dated
   ones in import order. The names are stored in the project, so they never
   renumber. Rename a run and see its source file in **Run details…**.
   Projects imported before this change keep their existing names.
5. **Add files…** extends the outing and continues the session numbering. Save the project to retain its source
   references; reopening rebuilds the entire list from verified sources.

Click any row (or focus it with Tab and press Enter/Space) to open that section.
The detail view shows its run, type and duration, a map of the selected section,
and recorded speed/lateral/longitudinal acceleration charts when those channels
exist. Move across a chart or drag the section-time slider to inspect values and
the matching point on the map. **← All laps** or Escape returns to the same list
and scroll position. OUT, IN and UNKNOWN sections can be inspected too.

The detail cursor uses source telemetry time within the selected boundaries.
Opening a lap from another run does not change the editor's active run, playback,
sync transform or project dirty state. One cancellable detail worker verifies the
primary source fingerprint and loads it independently of video. A later selection,
Back or a changed document/source invalidates old results. Missing/changed files
produce an explicit error with a working Back action. Missing channels and GPS
remain no data; no channels or brake signal are invented. Charts retain gap
segments, and cursor movement does not rebuild static map/chart geometry.

OUT is the recording start through its first accepted start/finish crossing;
LAP is a complete interval between crossings; IN is the final crossing through
recording end. Zero-length fragments are omitted. A recording without reliable
crossings is marked UNKNOWN, rather than inventing a lap type. This is boundary
classification, not detection of a pit lane or every pause within a recording.

RCZ supplies absolute timestamps. A recognized RaceChrono VBO supplies UTC through
its valid creation date and first clock sample, including a midnight transition.
Files without an established date/time remain visible after the chronological
records, in import order, with an explicit explanation. No recording dates are
inferred from filenames or file modification time. Sorting never joins raw channel
clocks, changes synchronization or depends on FPS.

Exact file duplicates are skipped. A one-to-one VBO/RCZ pair is grouped only when
absolute starts differ by at most one second, durations agree within two seconds,
and the moving GPS traces agree at at least 29 of 32 sampled points within ten
metres. Dated comparisons use the same absolute instants across both exports.
Comparison evidence normalizes the validated RaceChrono VBO west-positive longitude
to RCZ’s east-positive convention; existing channel/gate values remain unchanged.
See the [VBOX format specification](https://racelogic.support/knowledge-bases/general-kb/vbo-files/).
VBO is primary and RCZ is retained as an alternative; channels are not fused.
Unknown clocks, different dates and ambiguous matches remain separate. When
appending, this pairing applies to new sources in the same selection; use the
advanced review for other grouping choices.

Failed files and duplicates are reported while valid files proceed. An all-failed
or all-duplicate append retains the document and reports the reason. Adding files
preserves the active run and unsaved timing edits. List construction parses sources
sequentially on a cancellable worker, checks persisted fingerprints and caps input
at 64 recordings / 256 MiB and output at 20,000 sections. Missing or changed sources
are reported individually; a valid project still opens. Stale results are guarded
by source generation, document identity, path and the full primary-source reference
set. Only derived rows are retained; the native project schema is unchanged.

## A folder of recordings (KAN-87)

"Add a folder…" on the start panel imports every VBO and RCZ recording in a
folder. "Include subfolders" (off by default) also takes the folders below.
In an open day, the header's "Add folder…" asks each time: "This folder
only…" or "Folder and subfolders…".

The scan (`scanTelemetryFolder`, telemetry core) runs off the interface
thread. The window shows "Looking for recordings in the folder…" and Cancel
stops it. The scan:
- takes `.vbo` and `.rcz` files, in any letter case, sorted by path, and
  reports how many other files it ignored;
- never follows a symbolic link, to a file or a folder, and says how many
  it skipped. A folder is visited once by its canonical path, so no link
  or mount can loop;
- skips hidden entries;
- stops at 8 subfolder levels and 20,000 inspected files and folders, and
  says so;
- refuses a folder holding more than 64 recordings, the batch limit, and
  asks for a smaller folder rather than silently taking some of them;
- reports "No VBO or RCZ recordings were found" when there are none, and
  whether subfolders were included.

The files it finds go through exactly the same review, duplicate and
source-group policy, and event transaction as picked files. Recordings
already in the outing are skipped when appending. A document or source
change during the scan cancels it, like any import.

## Drag and drop (KAN-88)

Recordings and folders can be dropped anywhere on the Lap Analysis window,
in any mix. They go through the same review as the pickers
(`scanTelemetrySources`):
- recordings are taken;
- folders are scanned as above;
- anything else ("photo.jpg: not a VBO or RCZ recording; not imported."),
  a missing path or a link is reported;
- a recording reached twice counts once.

On the start panel a dropped folder follows the "Include subfolders" box.
In an open day it is added without its subfolders, as the drop hint says;
the header menu offers both.

While an import, export, project load or unsaved-changes decision is under
way, the drop hint turns amber. The drop is refused with "Finish the current
import, export or decision first. Nothing was imported." and the project is
unchanged. A drop without an outing name gets the ordinary name error.

An automatic import (picker or drop) whose recordings are all already in
the outing now ends quietly: "Nothing new to add: every recording is already
in this outing." Previously it was an empty review rejected with "Select at
least one new run." If nothing could be read, it is an error with each
file's reason.

## Advanced import review

1. Choose **File → Import telemetry runs…** and select multiple VBO/RCZ files
   in the native file dialog (up to 64).
2. Review the file names, durations in minutes:seconds and complete-lap counts.
   Duplicates and failures remain visible. **Show file details and import warnings**
   reveals paths, parser diagnostics and possible same-run GPS matches.
3. Keep **Import as a run**, choose **Skip this file**, or **Same run as**
   another source. That other source must itself remain a separate primary run.
   Grouping is explicit and does not depend on filename, extension or a guessed
   recording date. Similar GPS traces are suggestions, not automatic merges.
4. Choose **Create a new event** and supply a name, or **Add runs to current
   event**. Confirm. The files are rechecked before the document changes.
5. Save the event. The outing’s **All laps** view includes all runs.

The lap timing panel displays all complete telemetry laps even without video or
when a lap falls outside video coverage. Video coverage only enables the seek
action; video in/out fragments remain available through the existing video navigation.
Import dropdowns use the application's explicit dark background and highlighted
text colors, independent of the native macOS control palette.

A new event starts without a video binding and with independent zero-offset,
unit-scale transforms; the current widget layout and global analysis/settings
are retained. It gets a new document identity and requires Save As. Creating it
is blocked if the current document has unsaved edits: cancel the review, save
those edits and import again. Existing saved projects remain on disk. Appending
keeps current unsaved edits, the active run, its video/sync and all existing IDs.

The multi-file dialog does not turn the existing **Open telemetry…** action
into an append action. Folders and drag and drop are handled by folder and
drop import (KAN-87, KAN-88). Event best times and cross-run lap comparison live
in the Analysis window, not in this dialog. Appending creates new runs;
attaching another export to an existing run is done in Run details (KAN-90),
not in this dialog. Import complementary exports together to group them.

## Source groups and duplicate policy

Only the selected primary supplies channels and derived laps. Alternatives are
persisted with independent references and fingerprints; no channel fusion or
cross-source clock transform is invented. Manual grouping two sources is a user
assertion; automatic grouping requires the evidence described above. Cycles, missing/skipped
primaries, duplicate choices and oversized source groups are rejected.

Identical same-format files within a batch appear as duplicates and produce one
proposal. Sources added through this workflow retain their full SHA-256, format
and original sampled project fingerprint in `importProvenance`. When appending,
an identical imported source whose saved reference fingerprint still matches
that provenance is skipped; a relink with a different fingerprint cannot inherit
the old digest identity. Older event sources without this provenance are not
automatically deduplicated: review them manually. Existing-source matches remain
available when creating a separate new event.

No dates or track configuration are inferred, and no event-wide performance
statistics are calculated from unconfirmed compatibility. A malformed file does
not hide other successful results. An all-failed/all-skipped batch cannot commit.
Links whose backing file has a different format extension are reported as errors:
project reopening resolves the backing path, so import a correctly named regular
copy instead. Ordinary same-format links retain the normal project behavior.

## Recordings of an existing run (KAN-90)

Run details lists a run's recordings, with the primary marked.

**Attach a recording…**
- Reads the new file and the run's primary off the interface thread.
- Shows the match evidence for review:
  - the formats;
  - whether the GPS traces agree: samples compared, largest separation,
    duration difference;
  - how far apart the recordings start.
- With no GPS match it says so, and adding it is the user's call.
- "Add as an alternative" rechecks the file's content before adding it.

An attached recording never becomes the primary by itself; there is no
automatic format preference. A recording already in the run is refused.
Attaching needs the primary present, so there is something to compare with.
A run holds at most 8 recordings.

**Make primary**
- Reads the chosen recording off the thread and checks that its content and
  fingerprint are the attached ones.
- Records the timing gates it contains; its layout and direction start
  unresolved.
- Derives the run's laps again from it. Every source and run keeps its
  identity.
- Results tied to the old laps (exclusions, comparison slots, segment work)
  are kept but no longer match. The day reports them as unmatched rather
  than applying them to different laps.
- For the run open in the editor, the editor reloads the new primary, so a
  save cannot write the old reference back.
- Changing the primary can change the telemetry clock, so check the run's
  video sync.

Alternatives are never merged. A VBO carries RaceChrono's calculated G; an
RCZ keeps the logger's own clock.

## Transaction, cancellation and limits

Preparation and final full-digest verification run off the GUI thread and check
cancellation between bounded reads. Progress counts processed files; preparation
also performs fingerprint checks and GPS candidate analysis before review opens.
Final verification covers only selected sources. The digest check detects
ordinary changes between review and commit; it is not an atomic filesystem
snapshot or a guarantee against adversarial concurrent mutation. Subsequent
source loading still enforces the saved project fingerprint.

No project, source selection, dirty state or recovery snapshot changes before
successful confirmation. Cancel (including Escape), validation failure or stale
results leave the current document intact. Import context includes document ID,
revision, source generation **and project path** (Save As invalidates a review).
Adding runs to an open day is the exception for the revision. It checks the
event's recordings instead: which runs exist, their primaries, and each
source's identity and reference. Edits that leave those alone do not make the
import stale. An example is the analysis recording its verified track
inference while the previous import's laps derive. The import then adds only
its new runs to the current project, so such edits are kept. A new-event
import still goes stale on any change, because it replaces the document.
Only one import worker may be outstanding; cancellation cannot start parallel
batches that exceed the retention budget. Closing the controller requests worker
cancellation without allowing worker callbacks to access destroyed GUI objects.

The existing ceilings remain: 64 input files, 128 MiB/file, 256 MiB/batch,
16 million retained channel samples, plus parser bounds. Fingerprinting and final
verification add bounded I/O, not additional admitted raw telemetry. Event commit
also validates 64 runs, 8 telemetry sources/run, 128 sources/event and the 4 MiB
project payload. Stable event/run/source IDs are allocated for the candidate
document and become authoritative only on successful commit.

## Validation

Controller tests cover six independent runs and partial file failure, exact
duplicates, append/reopen identity, explicit RCZ/VBO grouping, cycles, cancelled
preparation/confirmation, file changes after review, dirty-document protection,
Save As/source-generation invalidation and submission through the production QML
dialog. The import suite also verifies completed-file progress callbacks.

Run the normal native build and all five CTest registrations. Automatic-analysis
regressions cover name validation, partial failure, duplicates, append/reopen,
cancellation, source-generation invalidation, dirty-state protection, production
Analysis QML, OUT/LAP/IN boundaries, unknown classification, malformed UTC metadata,
chronological ordering, source identity rejection and stale list results. See [testing](testing.md) for actual local validation scope; native
picker, private recordings and hardware export results remain separate from CI.
