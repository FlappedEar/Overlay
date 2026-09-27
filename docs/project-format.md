# Project format and external sources

`.fetproject` remains version 2. Source metadata is an optional extension of that format, so old v2 documents do not require a version bump or manual conversion. Unknown top-level and nested fields are retained when the application overlays known edits and saves.

## Resource limits

External documents are validated before editor models are populated. Projects and recovery snapshots are capped at 4 MiB; imported templates at 2 MiB; and the local template store at 8 MiB. A project has at most 256 widgets, 256 cues per widget (4,096 total), and 128 settings entries per widget. Templates use the same widget rules; the store holds at most 128 custom templates. JSON nesting is capped at 32 levels, ordinary strings at 4,096 characters, IDs at 128 characters, template names at 160 characters, and descriptions at 2,048 characters. Over-limit or malformed input is rejected with a clear load error; it is never silently truncated. Canonical saves run the same structural validation and reject a serialized project above the 4 MiB read limit before invoking the atomic writer. Recovery writes validate the embedded project, matching document identity/saved revision, and the final 4 MiB payload before replacing the snapshot.

Saved project files are parsed and structurally validated on the existing project-load worker. Only its generation- and revision-checked canonical result commits on the UI thread.

## Widget semantic normalization

`WidgetModel` is the single semantic boundary for inspector edits, scene import, template application, imported templates, and persisted-template reload. It clamps supported numeric settings to their editor contracts (including typography, decimal precision, G-force ranges, opacity, and geometry), preserves only finite geometry, restores the widget default for an invalid known color, and repairs invalid min/max pairs from defaults. Width, height, and scale are mutually bounded so the unrotated widget rectangle fits the normalized canvas; position is re-clamped after import, duplication, resizing, and scale changes. Cues always have finite `start >= 0`, `duration >= 0.1`, non-negative fades, and one of `fade`, `pop`, or `slideUp`; invalid cue values normalize to the safe fallback. Persisted widget IDs must be nonempty, bounded, and unique; duplicate or invalid IDs reject the incoming scene. Unknown compatible settings are retained for forward compatibility, except unsafe non-finite numeric values.

## Recovery discard and deletion residual

Projects saved by the current application include `documentState.id` and a decimal-string `documentState.savedRevision`. Recovery v2 records that identity plus its snapshot revision and last saved revision, and its embedded project payload must repeat the same identity and saved revision. On startup, a v2 recovery is **valid** only when its document identity matches an available structurally valid authority and its revision is newer; it is **stale** when that authority has already reached or passed its revision; malformed, newer-unknown, or identity-mismatched metadata is **invalid** and is never applied automatically. The identity stays with a document across Save As and portable moves, while unrelated projects never compare revisions as though they were the same document.

After an authoritative project save, deletion of the prior recovery snapshot is best-effort cleanup. A deletion failure is logged as cleanup debt but neither fails Save nor sets `recoveryDegraded` or a user warning. A later startup retries deletion after classifying the retained v2 snapshot as stale.

Discarding recovery for Quit, New, Open, or the startup recovery dialog has a stronger durable invariant. Before attempting snapshot deletion, the application atomically writes `<project-recovery.json>.discard` with `discardVersion`, `documentId`, and decimal-string `discardedThroughRevision`, using `QSaveFile` with direct-write fallback disabled. If that write succeeds, the requested destructive action continues even when physical snapshot deletion fails. On startup the tombstone suppresses only a well-formed recovery v2 snapshot with the same `documentId` and `revision <= discardedThroughRevision`; cleanup then retries snapshot deletion and removes the tombstone after success. A newer recovery revision or another document identity remains recoverable. If both tombstone persistence and snapshot deletion fail, discard is cancelled. Tombstone cleanup debt and deferred snapshot cleanup do not set `recoveryDegraded`. Legacy v1 snapshots have no logical identity, so they are never suppressed by a tombstone and remain conservatively recoverable when otherwise valid.

## Source representation

New saves use a `sources` object and remove the legacy top-level `videoPath` and `vboPath` fields. Each `video` or `telemetry` entry may contain:

```json
{
  "sources": {
    "video": {
      "relativePath": "media/camera.mp4",
      "absolutePath": "/fallback/location/camera.mp4",
      "fingerprint": { "kind": "video-v1" }
    },
    "telemetry": {
      "relativePath": "media/session.vbo",
      "absolutePath": "/fallback/location/session.vbo",
      "fingerprint": { "kind": "telemetry-v1" }
    }
  }
}
```

Relative paths use forward-slash JSON spelling and resolve only against the directory containing the `.fetproject`. Clean nested paths and reasonable parent-relative paths are supported. They never resolve against the process working directory, application directory, or home directory. Lookup tries the relative path first, then the stored absolute fallback, and performs no recursive filesystem search.

When a loaded source is in the project directory, or no more than two parent directories above it, Save/Save As writes a relative reference. The normalized absolute location is retained as a fallback. Moving a project folder with its `media` subdirectory therefore keeps the relative reference usable on another filesystem root or operating system.

## Source fingerprints

Fingerprints are deterministic identity metadata, not cryptographic proof of complete-file identity. Both source types store file size and a SHA-256 digest over at most three fixed 64 KiB regions: head, middle, and tail. Video additionally stores probed duration in microseconds, dimensions, exact rational frame rate, and codec. Newly modeled profile, pixel-format, bit-depth, orientation, bitrate, and color fields deliberately do not participate in the existing `video-v1` fingerprint, preserving compatibility with saved projects. Telemetry additionally stores parsed duration, sample count, and sorted channel name/unit/sample-count metadata.

The bounded byte sampling reads at most 192 KiB per source and is cheap relative to video probing or VBO/RCZ parsing. It detects common accidental substitutions, including size, media-metadata, telemetry-structure, and sampled-content changes. Changes confined to unsampled bytes can collide, so the value is deliberately called a source fingerprint rather than a content hash.

## Opening and relinking

The project document is valid independently of external assets. After JSON, scene, and synchronization validation, the document commits first. Video and telemetry source jobs start only when applying the widget scene succeeds; a rejected document cannot launch jobs that later mutate the prior document. After a successful document commit, sources resolve and load independently with `loading`, `ready`, `missing`, `mismatch`, or `error` state. Missing one or both assets does not replace the widget layout, synchronization, analysis settings, or other project fields.

Locate uses the ordinary bounded, cancellable, generation-guarded video probe or shared VBO/RCZ loader. A matching fingerprint is accepted. A candidate that parses successfully but clearly differs is not committed until the user confirms intentional replacement. An invalid candidate remains an error and cannot replace the current source. Projects without a fingerprint accept a compatible candidate and acquire identity metadata in memory for the next save.

A saved A/B pair whose lap belongs to a missing recording stays saved. The lap comes back into its slot as soon as the recording is relinked, without reopening the project (KAN-82). It is restored by its exact reference, never by lap number. An explicit clear, or choosing another lap, is never overridden.

Successful relinking or intentional source replacement updates document source metadata and marks the project dirty. Resolving the same persisted relative reference after moving the complete folder does not mark it dirty. Recovery snapshots serialize the same complete source objects and remain unsaved document state; they never replace the saved project as authoritative clean state.

## Analysis state

`analysis.channels` is project content and remains in canonical saves and recovery snapshots. Whether the floating Analysis window is open is transient UI state: startup, New, and Open always begin with it closed. Older v2 documents may contain `analysis.visible`; the loader safely ignores that field and the next canonical save removes it without discarding channel configuration or unknown sibling fields.

## Legacy v2 compatibility

When `sources.video` or `sources.telemetry` is absent, the loader reads the old absolute-only `videoPath` or `vboPath` field. Available sources load normally and acquire fingerprints in memory. Missing legacy sources become independently relinkable rather than failing project open. The next save writes the canonical `sources` form, removes the known legacy path fields, and preserves unrelated unknown fields.

## Current source and timing behavior

The telemetry reference may point directly to a supported `.rcz`; no project schema migration is needed. The historical `vboPath` worker setting remains a compatibility key. Simultaneous import/relink requests retain expected fingerprints and relink intent when restarted. Manual timing edits invalidate pending auto-sync results; these runtime revisions are not project schema fields.
