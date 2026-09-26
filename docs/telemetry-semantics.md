# Telemetry semantics

This document is the contract for public telemetry lookup and VBO parsing. Native RCZ uses the same session model; its archive, channel and gap rules are specified in [rcz-format.md](rcz-format.md).

## VBO timestamps

The parser recognizes time text before numeric conversion:

- `HH:MM:SS[.fraction]`, for example `01:02:03.500`.
- `HHMMSS[.fraction]`, for example `010203.500`.
- Plain finite seconds, for example `3723.5`.

Clock hours must be `0..23`, minutes `0..59`, and seconds `0 <= seconds < 60`. A six-digit integer component is interpreted as compact clock syntax, so `003059.500` means `00:30:59.500`, not 3,059.5 relative seconds. Invalid timestamps skip their row with a warning.

Clock timestamps can cross midnight when a previous clock value is at or after 23:00 and the next is at or before 01:00. The parser adds 24 hours for that rollover. Later duplicate timestamps are skipped; any other backward timestamp is skipped. Emitted timestamps are checked to be strictly monotonic. Absolute times, rollover additions, origin subtraction and duration must remain finite and strictly inside the signed 64-bit microsecond conversion range required by project fingerprints. Unsafe numeric ranges or elapsed-time precision collapse reject the parse. UTC date rollover and integer addition are also checked.

Parser warnings are capped at 200 stored messages; additional warnings are summarized in one final message.

VBO input is treated as untrusted. Parsing is cooperatively cancellable and rejects files above 128 MiB, more than 1,000,000 lines or 500,000 data rows, more than 512 columns, lines above 1 MiB, and fields above 64 KiB. Resource-limit failures and cancellation are distinct from invalid VBO syntax.

GoPro GPMF input is likewise bounded independently by packet count, aggregate metadata bytes, parsed KLV-header work, and container depth. The KLV counter includes structural/container and non-GPS sensor headers as well as GPS records; it is not a GPS sample count. Limit failures report the reached count, configured limit, packet, and parse context, while cancellation remains a distinct outcome.

## Missing values and lookup

The parser may retain non-finite numeric values internally as placeholders so channel rows remain aligned. The public `TelemetrySession::valueAt()` API never returns `NaN` or infinity: it returns no data instead.

- A time outside a channel's timestamp range is no data.
- An exact sample whose value is missing is no data.
- Linear interpolation requires two adjacent finite samples.
- Previous returns only the immediately preceding sample; it does not search backward across a gap.
- Nearest returns the nearest sample even when that nearest value is missing; it does not substitute a farther finite value.
- No mode bridges a missing gap automatically.

For example, with samples `0 s = 10`, `1 s = missing`, and `2 s = 30`, a lookup at `1 s` is no data in every mode. A linear lookup at `0.5 s` and `1.5 s` is also no data because one adjacent endpoint is missing.

Missing telemetry is not numeric zero. A finite zero remains a valid measurement, while widgets expose unavailable or stale values as no data (`—`). Geometry may use an internal minimum/zero fallback only when a separate validity flag prevents that fallback from being presented as measured telemetry.

## Overlay presentation

Preview and export share `TelemetryRenderContext`, which applies presentation filtering without modifying `TelemetrySession` or its raw lookup API. Ordinary finite samples are timestamp-interpolated (gear uses previous-value semantics). A missing value or the end of a channel may hold the most recent finite value for a bounded stale interval: 750 ms for ordinary channels and 2 seconds for heart rate. After that, the value is no data.

Light, recency-weighted trailing smoothing uses finite values only, with older samples contributing progressively less. The centralized starting windows are:

- speed and RPM: 150 ms;
- lateral/longitudinal G-force: 200 ms;
- throttle and brake: 100 ms;
- heart rate: 250 ms;
- other continuous channels: 150 ms;
- gear: no smoothing.

These short windows reduce frame-to-frame jitter without delaying pedal events with a large average. Missing channels remain unavailable; in particular, absent G-force channels hide the moving dot instead of placing it at fake `0 g`.

For a finite timestamp jump, three times the channel's median positive sample interval defines the normal-cadence tolerance. The interval statistic is cached with the immutable channel after its first use, so repeated presentation lookups do not rescan timestamps or allocate. Overlay presentation uses the larger of that tolerance and its stale interval. Across a larger jump it stops interpolation, briefly holds the preceding value, then becomes stale. This distinguishes ordinary sparse sampling from a real gap deterministically.

## Analysis ranges

Analysis reads actual raw channel samples, not presentation-filtered values. Non-finite values start a new segment. A timestamp jump greater than three times the median positive channel interval also starts a new segment, so the renderer issues a new path rather than drawing across a real gap.

Display decimation divides the requested range into time buckets and retains each bucket's minimum and maximum in timestamp order. This preserves short braking, RPM, throttle, and acceleration extrema where practical. Returned data is bounded to at most twice the requested bucket count; pathological high-gap input may omit some runs to respect that bound, but retained runs are still separate and never connected across a gap. Analysis never inserts zero, interpolates a replacement sample, or applies overlay smoothing.

## GPS tracks

Track construction ignores non-finite latitude/longitude values, latitudes outside `-90..90`, and longitudes outside `-180..180`. If there are no usable coordinate pairs, the track is unavailable. A current track position is unavailable when either latitude or longitude has no telemetry value at the requested time.

The normalized track outline is static for the lifetime of an assigned geometry and is cached for QML rendering. Time changes update only the independently rendered current-position marker. Replacing or clearing geometry invalidates the cached outline and marker together; this rendering lifecycle does not alter GPS lookup or missing-data semantics.

VBO coordinate units are resolved once per file from explicit evidence, before either samples or timing gates are interpreted. Numeric magnitude never selects a unit. The same unit applies to both axes and every gate coordinate; normalized session coordinates are degrees. Track geometry and the current marker only validate and project these degrees.

| Evidence | Source coordinate unit | Scope |
| --- | --- | --- |
| Exact, case-insensitive `[comments]` line `Generated by RaceChrono Pro v10.2.4` | Signed total arc-minutes, divided by 60 even near zero | This version only; based on the paired RCZ/VBO verification recorded in [testing.md](testing.md#racechrono-vbo-gate-conversion) |
| `[header]` entry `coordinate units = degrees` | Decimal degrees | Explicit FlappedEar import extension for custom/synthetic exports |
| `[header]` entry `coordinate units = arc-minutes` | Signed total arc-minutes | Explicit FlappedEar import extension for custom/synthetic exports |

The extension accepts `:` or `=`, trimmed values and case-insensitive keys/values. It is not claimed to be a standard exporter field. Repeated identical declarations are allowed; unsupported values, conflicting units (including a conflict with the verified exporter), or conflicting `Generated by` declarations leave units unresolved. Other exporters/versions without an explicit declaration are unresolved even if values look like degrees or arc-minutes. For example, `15` could mean 15 degrees or 0.25 degrees; neither is selected by plausibility. Latitude/longitude sign is preserved. Verified RaceChrono retains its existing west-positive longitude metadata for RCZ pairing.

Unresolved units produce a bounded warning when GPS columns or gates exist. GPS channels and timing gates are withheld; valid timestamps and other telemetry still import. Supply a known-unit export or the documented declaration to resolve the source. Unit declarations cannot enable an unverified RaceChrono gate geometry: such gates remain omitted with the existing exporter-version warning. `gpsCoordinateUnit` records the source unit or `unresolved`; `gpsCoordinateEvidence` records the resolver result. Input metadata cannot override these derived fields or the derived gate/longitude conventions.

After explicit conversion, non-finite or out-of-range coordinates are missing data (invalid gates are omitted with warnings). A declared latitude of 91 degrees is invalid; it is never reinterpreted as minutes. This iteration uses synthetic boundary/ambiguity regressions and the previously recorded exporter evidence; it does not claim a new private-recording acceptance run.

## Lap timing and comparison values

VBO `[laptiming]` records are parsed as bounded source telemetry metadata. Identified RaceChrono Pro 10.2.4 exports encode centre plus a backward-travel vector whose length is the full width; the parser rotates that vector and uses half-width endpoints. Generic VBO files with resolved coordinate units retain endpoint geometry. Other identified RaceChrono versions omit gates with a warning until validated. Malformed gates add a bounded warning but do not make otherwise valid channel data fail. Source order is retained, but the current derivation proceeds only when exactly one valid Start gate is available.

`LapTiming` operates on aligned raw latitude/longitude samples and raw telemetry timestamps. It does not use video frames, export cadence, overlay smoothing, or QML interpolation. A finite-segment corridor groups nearby samples into one candidate passage; ground speed, motion normal to the gate, direction, source gaps, re-arming, cluster duration, and a refractory interval filter invalid or duplicate candidates. Complete laps exist only between consecutive accepted same-direction passages. Fastest-lap selection and deltas use unrounded durations, and lap-start seeking applies the central inverse synchronization transform in C++.

`TelemetryRenderContext::lapTiming` is the presentation boundary for the live tiles. It compares the current GPS position with a bounded time-local search of the best completed lap trace. Current and reference speed use the same presentation interpolation, stale-gap handling, and 150 ms smoothing as the ordinary Speed widget; the underlying passage times and lap durations remain raw. An active gate cluster is finalized at telemetry EOF, and the Current state begins after the first accepted Start passage even before a completed reference lap exists.

Analysis navigation publishes only synchronized, video-overlapping fragments: Out lap is `[video frame 0, first measured-lap start]`, every measured lap spans its raw Start-passage pair, and In lap is `[last measured-lap end, last actual video frame]`. Clicking a fragment seeks its start through the primary player; QML does not invert synchronization itself.

The Export dialog's **Single lap · hotlap** range is similarly C++ owned. It takes one completed lap and a selectable 5–8 second handle on each side, clamps to the source frame domain, then returns inclusive SMPTE IN/OUT timecodes for the existing frame-addressed exporter. Handles are presentation-time selection inputs only; the accepted export remains the exact inclusive integer frame range parsed from those C++ timecodes.

Known audit limitation: best-lap reference traces still need explicit GPS-gap segment preservation; do not treat a displayed comparison across a recording gap as validated. Raw missing-data semantics above do not establish correctness of that derived comparison path.


## Track segments: proposals and the approved revision

Automatic straight/corner proposals, their boundary uncertainty and the
geometric apex are review input only. They are never persisted as segments
and never consumed by a metric. Only segments a user has approved are stored
in a run's `trackSegments`, each tagged with the track-configuration
(compatibility-group) reference it was approved for. Segments approved for
another configuration are never applied to the current one.

Any result derived from segments (sector times, theoretical lap, reports)
must use `approvedSegmentation(run.trackSegments, configuration)`, record its
`revision` (`track-segments-v1:<sha256>`) together with the configuration
reference, and treat itself as stale once `segmentationResultCurrent` fails.
With no approved segments there is no revision and no segment-based result.

Editing an approved segment (KAN-49) keeps its ID; a split keeps the ID on the
first part and a merge keeps the earlier segment's ID. Every edit, split,
merge, approval or revocation changes the revision, so dependent results must
be recomputed. Approved segments never overlap and are never empty.

Results record `segmentationResultStamp(approved, calculationAlgorithm)`
(configuration reference, segment revision and their own algorithm tag) and
persist it with `segmentationResultStampToJson`. A layout, direction or
timing-gate change alters the configuration reference, so previously approved
segments stop applying and every stamped result becomes stale. Review
rejections are persisted in `trackSegmentReview` and never change the
revision.

Sector times (KAN-51) interpolate boundary crossings on the lap's projected
progress and use the lap's timed start and end at the gate. A sector without
continuous projected coverage has no numeric time. A sector that crosses the
gate is timed within the lap as the part after its start plus the part before
its end (KAN-120). For a complete partition the sector times sum to the lap time within
1 ms.

The sector theoretical best (KAN-56) is the sum of the fastest recorded time
for each approved sector across the eligible laps of one compatibility group.
Each sector names its donor lap. It is a sum of separately recorded fragments
and does not show that the whole lap can be driven that fast. If any sector
has no timed lap, no total is reported. All laps are timed on one shared axis
built from the canonical run (the lowest run ID with approved segments).
When the Corner Analyzer is opened from a theoretical-best sector, it
compares the donor lap with the actual best using that canonical
segmentation, and says so.

Driving variability (KAN-63) summarizes each corner's braking point, apex,
minimum and exit speed, throttle pickup and line offset at the apex across
the eligible laps. Measured and inferred values are never mixed. The line
spread is reported with the recording's typical GPS accuracy and is called
resolvable only when it exceeds that accuracy.

G-G pairs (KAN-65) use the longitudinal channel's clock. Lateral values are
interpolated only between samples within the lateral channel's gap threshold,
and never across a gap. The recorded signs are kept: longitudinal + when
accelerating, lateral + toward the left. Values are in g (m/s² converted,
other units rejected), and values beyond ±4 g are excluded.

Timing consistency (KAN-62) reports the median ("typical") and the
interquartile range ("spread", seconds, the middle half of the laps) with the
sample count. It uses the ranking's eligibility and needs at least 3 samples.
There is no percentage score.

Time-loss observations (KAN-59) use one window per approved segment. A
window's increment is A's time through it minus B's (positive: A lost time
there). The running A-minus-B delta at its entry and exit is reported
separately. Approved segments never overlap, so no stretch of track is summed
in two windows. A straight that starts where a corner ends is marked as that
corner's continuation, so a loss carried onto the straight is not counted in
the corner.

Corner speeds (KAN-52) are read only from the recorded speed channel: entry
and exit at the segment boundaries, the apex speed at the geometric apex and
the minimum where the lap was slowest. The apex is never taken as the minimum,
and no speed is derived from GPS positions.

Braking metrics (KAN-53) use shared-axis progress with an explicit interval
(200 m before the segment start through its end, but never earlier than the
end of the previous approved corner; KAN-63). Measured and inferred braking
points keep their provenance and are never compared with each other; distance
and deceleration are reported only with continuous coverage of the braking
episode. An interval bound on the gate (an approach clipped at the gate, or a
segment ending at the lap length) uses the lap's timed start or end, because a
lap's projection never lands on the gate exactly. A missing brake and
deceleration channel is reported (`noBrakeOrDecelerationChannel`) before
coverage is checked.

The `throttle` alias means the driver's input (KAN-118). When a recording
has an accelerator-pedal channel with numeric data (RaceChrono OBD
`accelerator_pos`), the alias refers to it. The throttle plate
(`throttle_pos`) stays available under its own name, because it shows what
the ECU did: on the owner's car it reads 13.3 % at idle and 80.4 % fully
open, is fully open from about 70 % pedal, and opens for downshift rev-match
blips while the pedal is released. Aliases are not part of recording
fingerprints, so saved projects are unaffected.

Throttle pickup (KAN-54) is measured only from the recorded throttle channel;
without one, a positive longitudinal-acceleration onset is reported and
labelled inferred. Exit effects are compared over an explicit interval (the
adjoining approved straight, or 200 m after the segment) and no cause is
attributed to a difference. A segment or interval ending at the gate ends at
the lap's timed end. A segment starting exactly at the gate is not yet bounded
by the lap's timed start and reports `incompleteCoverage` for pickup.

## Synchronization transforms and numeric bounds

`videoToTelemetryTime(video, sync)` computes `video * timeScale + offset`;
`telemetryToVideoTime(telemetry, sync)` computes `(telemetry - offset) / timeScale`.
Both return an optional finite time. Non-finite inputs, non-positive scales and
non-finite derived results return no data. There is no clamping to zero or a nearby
sample, and no arbitrary cap on finite saved manual offsets/scales. Underflow to a
finite value follows ordinary double arithmetic; these helpers do not claim an
exact mathematical round trip at extreme precision limits.

| Consumer | Transformation and unavailable behavior |
| --- | --- |
| Preview values and static-analysis queries | AppController uses the checked forward transform; invalid times/range endpoints return empty values/series or `—`. Finite but overflowing chart spans are rejected by sampledSegments. |
| Preview and offscreen export widgets | Shared TelemetryRenderContext uses the checked forward transform. Its QML time/value is an invalid QVariant on overflow, the track marker is empty, and lap timing is unavailable. |
| Export worker progress | Uses the same forward helper; an unavailable transformed time is explicit JSON null and the export details display `—`, including when formatting milliseconds would overflow. Source video/frame scheduling continues independently. |
| Lap seeking, analysis navigation and hotlap ranges | Use the checked inverse helper. Invalid/outside-video times are unavailable; millisecond conversion additionally rejects values at or above 2^63 before rounding. |
| Event project persistence | EventProjectCodec already requires numeric finite offsets and positive finite scales. It preserves valid finite values, including extremes; consumers validate the actual time queried. The v3 schema is unchanged. |

### Automatic synchronization

KAN-17 closes demonstrated boundary gaps: the former forward expression could
return infinity (for example `2 * DBL_MAX`), the search read first/last timestamps
before checking empty/mismatched channels, and floating increments could stall
(for example `1e16 + 0.1 == 1e16`). Confidence values outside finite `0..1` and
invalid candidate transforms cannot qualify for automatic application.

The search validates aligned speed channels with at least 20 samples, finite
strictly increasing timestamps and at most 1,000,000 source samples per channel.
Coarse (1 Hz) and fine (10 Hz) searches use bounded integer grids. Each phase is
limited to 1,000,000 resampled times and 100,001 offsets; their combined budget is
50,000,000 sample-pair evaluations. Counts are checked before integer conversion
and allocation. Non-finite ranges or a grid whose timestamps cannot advance at
the requested resolution fail explicitly. These are search resource/precision
limits, not recording import or manual synchronization limits. Cancellation is
checked during validation, each offset, sampling and correlation.

The midpoint uses the standard overflow-safe operation; integer conversion must
be range checked as specified by the [C++ numeric midpoint contract](https://eel.is/c++draft/numeric.ops.midpoint)
and [floating-to-integer conversion rules](https://eel.is/c++draft/conv.fpint).
The existing global ambiguity and minimum-overlap evidence still bound fine-search
confidence. An ambiguous result remains reviewable without changing the confirmed
transform. Source identity and timing-edit revision guards still reject stale
results. Search failure likewise leaves the confirmed transform in place.
