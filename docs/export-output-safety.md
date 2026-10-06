# Export output transaction guarantees

The selected user target is never passed to FFmpeg. Each export creates an empty staging file with
`QIODevice::NewOnly` in the target directory and records that exact path in the live
`ExportOutputTransaction`. FFmpeg writes only to that staging file. Cancellation, worker failure,
validation failure, forced worker termination, and controller destruction remove only paths in that
ownership record. Filename shape alone is never treated as proof of ownership.

An export reaches transaction commit only after final media validation. Exact success and the narrow
`SuccessWithWarning` terminal-deficit case use this same staging/commit path; a warning is eligible
only for one through ten missing terminal frames with all other media, staging, progress, and exact
contiguous-CFR timing checks passing. A surplus or any other validation failure leaves staging owned
for normal cleanup and never replaces the selected target.

For a new target, the staging file is renamed to the target on the same filesystem after final media
validation. The transaction refuses to commit if another file appeared at the target while encoding.

Existing symbolic links and Windows reparse points are rejected during preparation. The transaction
does not follow a link to its referent and does not replace a link while describing that action as an
overwrite of the referent. Existing targets must be explicit regular files.

**Sources are never a target (KAN-76).** When an event is open, the
controller protects:
- the loaded recording;
- every run's recordings, including alternative sources;
- every run's video;
- the project file.

Every chapter of a chaptered video is protected too, whether or not an
event is open (KAN-106).

Preparation refuses a target that is any of these. Paths are compared
after canonical resolution, so a symlinked folder or different letter case
(on macOS, where the file system ignores case) still matches. An existing
target is also compared by device and file id against every protected
file and the input video, because a hard link is another name for a
source that no path comparison can see. The refusal holds even with
overwrite consent.

`TelemetryTests::protectsEveryDaySourceFromExport` tests this through
`AppController::startExport`. The day it uses has two runs, an
alternative recording, a video, approved segments and a lap exclusion,
and it is saved. The test tries every source and the project by its own
path, through a symlinked folder, by a hard link and by different letter
case. Each export is refused and no file changes. An unrelated existing
file still asks for overwrite consent.

For an existing regular-file target, replacement requires the controller's explicit
`overwriteAllowed` flag. At that approved preparation, `ExportTargetIdentity` captures native file
identity plus size and high-resolution modification state without reading or hashing file contents:

- macOS/Unix uses `lstat(2)` device, inode, size, and native nanosecond modification time;
- Windows opens the pathname for attributes without following a reparse point and records volume
  serial, file index, size, and native last-write time.

Immediately before replacement, the transaction captures the pathname again. A different native
identity detects replacement; changed size or modification state detects in-place modification. A
missing, linked, non-regular, changed, or unverifiable target refuses commit. Disappearance is not
reinterpreted as permission for a new-target export. The external pathname is never removed by this
failure path, and the FlappedEar-owned staging file remains governed by normal transaction/manifest
cleanup. The resulting controller and persistent-log error explicitly says encoding finished but the
destination changed and was not overwritten.

For an identity-matched existing target, the existing atomic replacement remains unchanged:

- macOS (and other POSIX builds): `rename(2)` replaces the directory entry atomically. A crash exposes
  either the old complete file or the new complete file; there is no delete-then-rename window.
- Windows: `ReplaceFileW` with `REPLACEFILE_WRITE_THROUGH` replaces the existing file as one system
  operation. If Windows rejects the replacement, the existing target remains in place and the staged
  file remains owned for cleanup.
- Other platforms: the fallback renames the target to a transaction-owned backup, installs staging,
  and rolls the backup back if installation fails. This preserves the old file on ordinary operation
  failure, but it does not claim crash atomicity between the two renames.

Portable pathname replacement still has a residual race between the final native identity check and
`rename(2)` / `ReplaceFileW`. The check is placed directly before that operation, with no logging or
other work between them, reducing exposure from the complete export duration to this small final
commit interval. Eliminating it completely would require more invasive platform-specific directory-
handle or conditional-rename mechanisms and is outside the current transaction design.

Input video, VBO/RCZ telemetry, and known transaction paths are compared using cleaned absolute paths and canonical
paths where Qt can resolve them, including canonicalized parent directories. This collision comparison
is separate from the explicit rejection of a linked output target.

Free-space checks do not require a staging, temporary, or final artifact to exist. Each intended path
is resolved upward to its nearest existing filesystem ancestor, while diagnostics retain the original
intended path.

## Full and filling destinations (KAN-75)

`TelemetryTests::keepsOutputSafeWhenTheDestinationFills` runs the real
export worker against a real volume that runs out of space. The volume is a
2.5 GiB sparse HFS+ disk image made with `hdiutil`; it is larger than the
2 GiB preflight reserve so an export can start. The volume already holds the
previous export at the target and an unrelated file. A filler reserves every
free block at once (`F_PREALLOCATE`) when the worker reports the stage:

| Case | What happens |
|---|---|
| Full before the export | Preflight refuses ("Required estimate …") before writing |
| Fills during Stage A (overlay on the volume) | FFmpeg fails with ENOSPC, or the storage monitor stops the export when the temporary volume falls below its reserve |
| Fills during Stage B (staging output on the volume) | FFmpeg fails with ENOSPC |
| Cancelled during Stage B | Cancellation is honoured in 29–569 ms (three runs) |
| Full at final publication | The export completed and validated. Commit is a same-volume rename, needs no new blocks and publishes the valid file |

In every case except the successful publication:
- the worker ends within the bound;
- the previous export and the unrelated file are unchanged;
- after the controller's cleanup (the manifest's owned paths, then the
  transaction), no staging output or overlay remains.

**Clear message when the disk fills.** When a failed export's diagnostics
show ENOSPC ("No space left on device"), the worker reports "The disk ran
out of space during the export. Nothing was written to the chosen file.
Free some space, or choose another destination, and export again."
(`describeExportFailure`). Previously the user saw the stage's technical
error, such as "FFmpeg composition failed." or a raw-frame pipe error. The
technical error is kept at the top of the diagnostics.

**Limitation:** a slow (throttled) destination is not simulated. macOS has
no throttled file system without admin rights. Cancellation is measured on
the disk image, which is fast.

## Active artifact manifests and recovery

Before a worker is started, the controller writes an atomic versioned JSON manifest in `export-manifests/` under the application's local data folder (KAN-164; older versions used the system temporary directory, which is still scanned at startup). It records the export UUID, creation time, worker PID, state, temporary FFV1 path, staging path, and final target for diagnostics. The manifest is the authorization record: only its validated overlay and staging paths may be removed automatically; the final target is never a cleanup candidate.

The separate per-export diagnostic log is stored beneath the application-data `exports` directory, never beside the executable or selected output. Its retention cleanup recognizes only the application's `export-*.log` filename convention inside that dedicated directory; it does not authorize removal of output, manifest, or arbitrary support files.

On normal success, cancellation, or failure the controller removes those owned artifacts and the manifest. Worker shutdown explicitly defers manifest-owned removal to that controller authority instead of reporting a false deletion failure. Cleanup is idempotent: an absent artifact or already-removed manifest is success, while a path that still exists and cannot be removed is a real failure and leaves the manifest for retry. At application startup the janitor reads only FlappedEar manifest files, rejects malformed records, skips a record whose PID or Unix process group is still active, and removes only paths proven by the valid manifest. Names such as `*.mkv` or `*.part.mp4` alone never authorize deletion.

PID reuse can conservatively cause an old manifest to be retained when an unrelated process has reused its recorded PID. That may leave recoverable temporary files behind, but it never broadens deletion authority.
# External input bounds

Ownership manifests are external JSON inputs even though the application creates them. Reads are limited to 64 KiB and must prove exact transaction ownership before cleanup. An oversized or malformed manifest is skipped rather than being used to authorize deletion.

## Process-tree completion before cleanup (KAN-13)

A direct worker exit does not authorize commit or deletion. The supervisor retains
its Unix process-group ID after that exit, sends TERM and then KILL under separate
bounded deadlines, and succeeds only when the group is gone. Windows retains the
kill-on-close Job Object and checks its active-process count. Unknown liveness is
treated as active. Nonpositive stop budgets mean no wait, not an infinite Qt wait.

The controller queues its finished handler so a blocking process wait cannot
synchronously destroy the supervisor on its own stack. It remains exporting until
finalization releases ownership. A failed shutdown retains transaction files and
retries finalization; destruction defers file deletion to manifest recovery. A
worker reporting success while descendants remain is treated as failed, preserving
the existing user target. Cancellation cleanup occurs only after writers stop.

These boundaries follow [QProcess wait/signal semantics](https://doc.qt.io/qt-6/qprocess.html#waitForFinished),
[Unix group signaling and existence checks](https://man7.org/linux/man-pages/man2/kill.2.html),
and [Windows Job Object active-process accounting](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_accounting_information).
Regressions and exact execution evidence are linked in
[KAN-13](https://kozucharkadiusz.atlassian.net/browse/KAN-13).
