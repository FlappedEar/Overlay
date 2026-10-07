# Telemetry semantics

This document is the contract for public telemetry lookup and VBO parsing. Native RCZ uses the same session model; its archive, channel and gap rules are specified in [rcz-format.md](rcz-format.md).

## VBO timestamps

The parser recognizes time text before numeric conversion:

- `HH:MM:SS[.fraction]`, for example `01:02:03.500`.
- `HHMMSS[.fraction]`, for example `010203.500`.
- Plain finite seconds, for example `3723.5`.

Clock hours must be `0..23`, minutes `0..59`, and seconds `0 <= seconds < 60`. A six-digit integer component is interpreted as compact clock syntax, so `003059.500` means `00:30:59.500`, not 3,059.5 relative seconds. Invalid timestamps skip their row with a warning.

Clock timestamps can cross midnight: when a clock value is earlier than the previous one and, read as the next day, lies at most 3 hours after it, the parser adds 24 hours for that rollover (KAN-233, matching FlappedEar Telemetry FET-211). So a dropout from 22:50 to 01:10 is a rollover, while a small step back such as a daylight-saving hour is not. A clock reset within 3 hours before midnight cannot be told from a rollover. Later duplicate timestamps are skipped; any other backward timestamp is skipped. Emitted timestamps are checked to be strictly monotonic. Absolute times, rollover additions, origin subtraction and duration must remain finite and strictly inside the signed 64-bit microsecond conversion range required by project fingerprints. Unsafe numeric ranges or elapsed-time precision collapse reject the parse. UTC date rollover and integer addition are also checked.

Parser warnings are capped at 200 stored messages; additional warnings are summarized in one final message.

VBO input is treated as untrusted. Parsing is cooperatively cancellable and rejects files above 128 MiB, more than 1,000,000 lines or 500,000 data rows, more than 512 columns, lines above 1 MiB, and fields above 64 KiB. Resource-limit failures and cancellation are distinct from invalid VBO syntax.

Before any value vector grows, the parser also bounds the following (KAN-147):

- **Header.** Section names are limited to 256 characters. Header metadata is limited to 10,000 entries and 1 Mi characters.
- **Decoded values.** Rows × columns is limited to 40,000,000. The parser is the one place every load path decodes through: the editor, batch import planning and attaching a recording.

The parser also normalises these header cases:

- **Duplicate and empty names.** Every column gets a unique, non-empty name. An empty header cell becomes `column N`, and a generated `name (n)` never takes a name the header itself uses.
- **Repeated sections.** A repeated header of the same data or column-names section continues it. Two differently named ones are rejected rather than chosen by hash order.
- **Line endings.** A file is read exactly as its text is parsed, CRLF included.
- **Out-of-range values.** A value beyond float range is no data.

GoPro GPMF input is likewise bounded independently by packet count, bytes per packet, aggregate metadata bytes, parsed KLV-header work, and container depth. Nested containers are parsed in place, so depth never multiplies memory (KAN-197). The KLV counter includes structural/container and non-GPS sensor headers as well as GPS records; it is not a GPS sample count. Limit failures report the reached count, configured limit, packet, and parse context, while cancellation remains a distinct outcome.

## Missing values and lookup

The parser may retain non-finite numeric values internally as placeholders so channel rows remain aligned. The public `TelemetrySession::valueAt()` API never returns `NaN` or infinity: it returns no data instead.

A `TelemetryChannel` holds finite, strictly increasing timestamps with exactly one value each (KAN-209). Its samples are read through `timestamps()` and `values()` and change only through `setSamples`, `appendSample`, `setValue` and `clear`, which throw `std::invalid_argument` for anything else, so readers never meet a mismatched or unordered channel. Only values may be `NaN`.

Heart rate outside 30 to 230 bpm is no data (KAN-222). The VBO and RCZ parsers turn it into `NaN` (`markImplausibleHeartRate`): RaceChrono writes 0 bpm where its monitor has no reading, as in the final row of the 29 August 14:37 VBO export, whose RCZ reads 133 there.

- A time outside a channel's timestamp range is no data.
- An exact sample whose value is missing is no data.
- Linear interpolation requires two adjacent finite samples.
- Previous returns only the immediately preceding sample; it does not search backward across a gap.
- Nearest returns the nearest sample even when that nearest value is missing; it does not substitute a farther finite value.
- No mode bridges a missing gap automatically.
- **Time gaps (KAN-157, KAN-221).** Two adjacent samples enclose a gap when they are more than three median sample intervals of the whole channel apart (`telemetryGapThreshold`) and also more than three times the local cadence: the median of up to eight intervals on each side, the slower side counting (`telemetryIsGap`). A stretch logged at a slower rate (10 Hz after 100 Hz, 1 Hz periods in a 10 Hz channel) is therefore not a run of gaps, while a pause in steady logging still is. A lookup strictly between them is no data in every mode, even though both samples are finite. This covers decoders that drop samples rather than mark them: a GoPro loss of GPS fix, or a VBO logger pause. The rule lives in `telemetryValueAt()`, which `TelemetrySession::valueAt()` and auto-sync both use; The median interval is cached per channel (`ChannelCadenceCache`). The cache is locked, so a session can be read from several threads straight from a parser, and every change to a channel's samples resets it (KAN-209). `TelemetrySource::load` and `GoProTelemetrySource::load` still compute it up front so the first lookups are fast. Every consumer uses `telemetryIsGap` except channel fusion, which compares each source's own threshold.

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

Display decimation divides the requested range into time buckets and retains each bucket's minimum and maximum in timestamp order. This preserves short braking, RPM, throttle, and acceleration extrema where practical. Returned data is bounded to at most twice the requested bucket count; when the bound is exceeded every run keeps at least its first sample (KAN-210). Only when there are more separate runs than the bound allows points is a uniform choice of runs kept, one sample each; the lookup then reports `SegmentsTruncated` and the chart series carries `truncated: true`. Retained runs are still separate and never connected across a gap. Analysis never inserts zero, interpolates a replacement sample, or applies overlay smoothing.

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

After explicit conversion, non-finite or out-of-range coordinates are missing data (invalid gates are omitted with warnings). A declared latitude of 91 degrees is invalid; it is never reinterpreted as minutes. A RaceChrono centre-and-direction gate whose converted end passes ±180° is wrapped back into -180..180 rather than refused, and projections and gate midpoints take longitude the short way round, so a circuit across the antimeridian times like any other (KAN-235). This iteration uses synthetic boundary/ambiguity regressions and the previously recorded exporter evidence; it does not claim a new private-recording acceptance run.

## Lap timing and comparison values

VBO `[laptiming]` records are parsed as bounded source telemetry metadata. Identified RaceChrono Pro 10.2.4 exports encode centre plus a backward-travel vector whose length is the full width; the parser rotates that vector and uses half-width endpoints. Generic VBO files with resolved coordinate units retain endpoint geometry. Other identified RaceChrono versions omit gates with a warning until validated. Malformed gates add a bounded warning but do not make otherwise valid channel data fail. Source order is retained, but the current derivation proceeds only when exactly one valid Start gate is available.

`LapTiming` operates on aligned raw latitude/longitude samples and raw telemetry timestamps. It does not use video frames, export cadence, overlay smoothing, or QML interpolation. A finite-segment corridor groups nearby samples into one candidate passage; ground speed, motion normal to the gate, direction, source gaps, re-arming, cluster duration, and a refractory interval filter invalid or duplicate candidates. A candidate must also really cross the gate line (KAN-205, the port of Telemetry FET-198): it starts strictly on one side, ends on the line or the other side, and reaches the line within the gate span widened by the inner corridor at each end; a pass that comes close and leaves on the same side counts as `rejectedNotCrossingClusters`. Complete laps exist only between consecutive accepted same-direction passages. A lap that cannot be a lap of the circuit is marked `ImplausibleLap` (KAN-225, the port of Telemetry FET-199): under `minimumLapSeconds` (3 s) or over `maximumLapSeconds` (1 h), a GPS path under `minimumLapDistanceMeters` (200 m), an average speed over `maximumAverageSpeedMetersPerSecond` (100 m/s), or a path under `minimumLapDistanceRatio` (80 %) of the median path of the recording's laps within those limits (from at least two laps). It stays listed but is not ranked, used for statistics or as a reference. Across a day, `markShortLapsOfGroups` also gives `layoutIssue` `implausible-lap` to an eligible lap shorter than 80 % of its compatibility group's median path, taken from at least two recordings. Fastest-lap selection and deltas use unrounded durations, and lap-start seeking applies the central inverse synchronization transform in C++.

Lap times are shown through one C++ formatter, `formatLapTime`. The tiles reach it through the render context, so preview and export show the same text; the analysis views reach it through the controller. It rounds the time to the display precision (0–3 decimals) before splitting minutes, so 59.96 s at one decimal is `1:00.0`, never `0:60.0` (KAN-149).

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
has an accelerator-pedal channel (RaceChrono OBD `accelerator_pos`) with at
least half the throttle channel's finite samples, the alias refers to it
(KAN-230, Telemetry FET-207); a pedal with only a few valid samples does not
replace a full throttle. The throttle plate
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

## Areas to inspect next (KAN-73)

`selectFocusAreas` (`telemetry/FocusAreas`, `focus-areas-v1`) turns computed
observations into at most three areas, with at most one per segment:

| Kind | Observation | Threshold | Evidence pair |
| --- | --- | --- | --- |
| Best lap against the fastest sector | the best lap's time through a sector minus the fastest recorded time there (theoretical-best source) | ≥ 0.05 s | best lap / source lap |
| Repeated loss | per segment, the losses of each session's fastest lap against the best lap; median and count | ≥ 0.05 s each, on ≥ 3 laps | the lap nearest the median / best lap |
| Braking-point spread | interquartile range of the **measured** braking point (inferred points are never mixed in) | ≥ 10 m, ≥ 3 laps | earliest / latest braking lap |
| Lowest-speed spread | interquartile range of the minimum speed | ≥ 5% of the median, ≥ 3 laps | slowest / fastest lap |

Selection takes the strongest area of each kind in that order, then fills
any remaining places round-robin, each kind by score (seconds, median ×
count, metres, relative spread). Ties go by segment id.

Each area states an **observation**, a measured number with its sample
count, apart from a **hypothesis**, which says what may be worth comparing.
A hypothesis never claims a cause and never recommends a change as faster or
safe. The braking-spread hypothesis states explicitly that it does not show
whether earlier or later braking is faster or safe. Speeds are in the
recording's own units when none are declared.

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

**Search range and ranking (KAN-146).**

- **Range.** The coarse search covers every offset (telemetry = video + offset) that leaves at least 20 s of overlap, in both directions. The camera may start before the logger, stop after it, run about as long, or run much longer. If either recording is shorter than 20 s, the shorter one must lie fully inside the other.
- **Ranking.** Offsets with less than 20 s of overlap are ranked only when no offset has that much. A near-perfect correlation over a few samples is accidental.
- **Global choice.** The coarse search ranks offsets by the evidence for the match: Fisher z of the correlation times the square root of the sample count minus 3. A strong match over a long overlap beats a perfect one over a short stretch.
- **Refinement.** The ±5 s refinement at 10 Hz ranks by correlation alone, because its overlap is nearly constant.
- **Uniqueness.** It compares the best offset with the strongest competing offset anywhere in that full range: any offset at least 5 s away, or a nearer separate peak, where the correlation dips by at least 0.1 between it and the best (KAN-214). The best peak's own shoulders do not compete. Laps a whole lap apart, or a false peak 2 to 4 s away, therefore make the result ambiguous rather than automatic.
- **Time scale.** Automatic synchronization solves the offset only; the candidate's time scale is always 1.
- **Correlation floor.** A candidate is applied automatically only when the speed traces correlate at 0.8 or more.
- **Real check (private), Jastrząb day:**
  - GoPro GX010089 with Session 5 still syncs automatically: offset 74.976 s, correlation 1.000, confidence 0.82. Before the change it was 74.953 s at 0.81. The two offsets differ by less than one 0.1 s refinement step, because the coarse grid now starts at a different fractional point.
  - GX010091 with Session 6 syncs at the same offset as before, 7.817 s (correlation 0.998). Its confidence rose from 0.73 to 0.86, because the competing peak is now judged by its evidence. It is therefore applied automatically, where before it was only offered.

The midpoint uses the standard overflow-safe operation; integer conversion must
be range checked as specified by the [C++ numeric midpoint contract](https://eel.is/c++draft/numeric.ops.midpoint)
and [floating-to-integer conversion rules](https://eel.is/c++draft/conv.fpint).
The existing global ambiguity and minimum-overlap evidence still bound fine-search
confidence. An ambiguous result remains reviewable without changing the confirmed
transform. Source identity and timing-edit revision guards still reject stale
results. Search failure likewise leaves the confirmed transform in place.

## Driving states (KAN-91)

`classifyDrivingStates` (`telemetry/DrivingStates`, algorithm
`driving-states-v1`) says when a lap is braking, accelerating, cornering and
coasting, and how each was obtained.

| State | From | Thresholds (on / off) | Provenance |
|---|---|---|---|
| Braking | the `brake` channel | 10 / 5 % | measured |
| | otherwise negative longitudinal G | 0.15 / 0.08 g | inferred |
| Accelerating | the `throttle` channel (the accelerator pedal when recorded, KAN-118) | 15 / 8 % | measured |
| | otherwise positive longitudinal G | 0.10 / 0.05 g | inferred |
| Cornering | abs(lateral G) | 0.30 / 0.20 g | measured; *calculated* for RaceChrono's GPS-derived `-calc` channel |
| Coasting | moving (at least 10 km/h) with both pedal states known and neither active | — | measured only when both pedals are measured, otherwise inferred |

Rules:
- An episode must last at least 0.2 s; shorter ones are counted as spikes.
- A state is never bridged across a gap or a missing sample. Each state
  reports the spans where it is *known*, so the time between them is
  unknown.
- A pedal channel that is present is never replaced by acceleration, even
  where its data is missing.
- A channel whose declared unit differs from its threshold's (a brake in bar,
  say) leaves the state unknown with `unitMismatch`. It is never rescaled.
- States overlap where driving does:
  - cornering overlaps braking (trail braking), accelerating or coasting;
  - braking and accelerating overlap only when both pedals are measured
    (left-foot braking). One acceleration channel cannot show both;
  - coasting never overlaps either pedal.
- Inferred pedal activity is labelled `inferred` and is never presented as
  a measurement.

**On the Jastrząb day** (each session's best lap, opt-in
`DrivingStatesTests::classifiesPrivateBestLaps`):
- all states except cornering are measured, and cornering is calculated;
- the states are known for 99.8–99.9 % of each lap;
- coasting falls from 21 % of the lap in Session 1 to 8–14 % in the faster
  later sessions;
- time on the accelerator rises from 59 % to 63–67 %.

## Coasting (KAN-92)

`summarizeCoasting` (`telemetry/CoastingAnalysis`, algorithm `coasting-v1`)
turns the coasting state into episodes. Each episode has a start and end
(time and lap progress), a duration and a distance (speed integrated over
the episode). The summary adds per-approved-segment totals (a segment with
none keeps a zero row) and lap totals, with the provenance of the coasting
state. The lap view's **Coasting** pane shows it:
- the lap total, and where the result comes from (recorded pedals,
  inferred from G, or why it cannot be told);
- a row per segment;
- each episode. Selecting one moves the lap cursor to it, so the map,
  charts and video follow.

The episodes are drawn in orange on the lap map. Segment rows and positions
need the lap's progress axis, which the pane requests.

Coasting is presented as an observation. A lift can settle the car or be
forced by traffic, so it is never called a mistake or a loss. A recording
without speed, or with neither pedals nor longitudinal G, shows why coasting
cannot be told rather than zero.

**On the Jastrząb day:** the best lap (Session 5 LAP 2, 1:49.898) coasts
14.8 s over 226 m in 14 episodes (13.5 % of the lap), from the recorded
pedals. Most of it is in Corners 9–16 (5.4 s) and Corners 5–6 (4.8 s).

## Trail braking (KAN-93)

The Corner Analyzer shows **Trail braking** for the selected segment: the
time each lap of the A/B pair spends braking while cornering. It is the
overlap of the KAN-91 braking and cornering states. For each lap,
`comparisonTrailBraking` maps the segment's progress range to that lap's
time range. A segment across start/finish is handled as the end of the lap
followed by its beginning. It then classifies the driving states over that
range and reports:
- the overlap time, and its distance (recorded speed integrated over the
  overlap, never across a missing sample);
- the total braking and cornering time in the segment;
- the braking and cornering provenance and the channels they came from;
- the braking, cornering and overlap intervals as fractions of the segment.

The row shows A and B overlap seconds and Δ (A−B). Under it, a strip per lap
shows braking in red, cornering in blue and both in violet along the segment.
The note gives the overlap distance for each lap and where the evidence comes
from:
- brake measured, or braking inferred from deceleration (no brake channel);
- lateral G measured, or calculated from GPS (a RaceChrono `-calc` channel).

The ⎍ button adds the brake and lateral-G channels to the charts. A lap
whose braking or cornering state is unknown, or whose progress does not
cover the segment, shows "—" rather than zero. Longer overlap is not
automatically better or safer, and the note says so.

**On the Jastrząb day:** in Corners 2–3 (174 m), Session 2 LAP 1 (A) brakes
while cornering for 1.8 s over 35 m. The best lap, Session 5 LAP 2 (B), does
so for 2.9 s over 65 m. Both use the measured brake and calculated lateral G.

## Map layers (KAN-97)

The A/B map can colour one lap's racing line by a value. **Line: A / B**
(the default) shows the two laps in their own colours. The other choices
are:
- **Speed**;
- **Δ time (A−B)**;
- **Lateral G** and **Longitudinal G**;
- **Throttle** and **Brake (measured)**;
- each recorded temperature channel (a name containing "temp").

The **A**/**B** buttons choose the lap. The other lap stays visible,
dimmed, and the hover markers of both laps still follow the charts.

A layer is built in two steps (`telemetry/MapLayers`, algorithm
`map-layer-v1`):
1. `channelAlongProgress` samples the channel at up to 800 evenly spaced
   positions on the pair's shared progress axis, through the lap's own time
   at that progress. The value comes from the two neighbouring samples. It
   is unavailable when either sample is missing, non-finite or implausible,
   or when the samples are further apart than the channel's gap threshold.
   Temperatures use the KAN-67 policy: −40 to 250 °C are plausible, and an
   exact zero is a placeholder on a channel that is typically warm.
2. `placeOnMap` puts each value at the lap's position on the shared map.

A missing progress, time, position or value ends the line, so gaps stay
open. The Δ layer uses the pair's delta series (positive: A behind) drawn
where the chosen lap was.

Colour scales:
- Speed, pedals and temperatures use one blue hue, dim to bright, over the
  lap's own range.
- The Δ and G layers use a diverging scale, symmetric around zero: blue,
  grey at zero, amber. Δ is labelled "A ahead" to "A behind", and
  longitudinal G "braking" to "accelerating". Lateral G keeps the logger's
  sign.

The legend shows the range, the layer, the lap, and the unit when the
recording declares one. G from a RaceChrono `-calc` channel and the delta
are marked **calculated**. A channel neither lap recorded is listed as "not
recorded" and draws nothing: the brake layer is never inferred from
deceleration. When only the other lap has the channel, the map says which
lap lacks it. The layer is static geometry: it is rebuilt when the layer,
lap or pair changes, never on hover.

**On the Jastrząb day:** the best lap (Session 5 LAP 2, lap B of the largest
loss) has every layer:
- speed 33–121 and brake 0–57 %, both recorded;
- lateral G −0.98 to 0.95 and longitudinal G −0.87 to 0.39, both calculated;
- four recorded temperatures: coolant 97–101 °C, engine oil 114–121 °C,
  gearbox 98–104 °C and intake 30–33 °C.

The logger declares no units for speed and the pedals, so none are shown.

## Temperatures and lap performance (KAN-100)

**Progression → Car & driver** now shows, under each recorded temperature,
how that temperature moved together with lap performance over the day. The
metrics are lap time and **strong acceleration**, the 90th percentile of the
lap's positive longitudinal G samples. A percentile is used rather than the
peak so that one noisy sample does not decide it. Strong acceleration needs
the `longitudinalAcceleration` channel, in g or undeclared units, with at
least 20 positive samples.

**Which laps count.** The population is the comparison group's eligible
laps, as for consistency (KAN-62): same layout and direction, and not
excluded, stale or invalid. A lap's temperature is its time-weighted mean
under the KAN-67 plausibility policy. It is used only when the sensor
covered at least 80 % of the lap. Laps left out for low coverage, or for
lacking a valid reading, are counted and shown.

**How the association is measured.** The measure (`telemetry/
TemperatureAssociation`, `spearman-rank-v1`) is Spearman's rank correlation
ρ: both series are ranked, tied values share their average rank, and the
Pearson correlation of the ranks is reported.
- It needs at least 8 laps, and both series must vary. Otherwise the line
  says why and claims nothing.
- |ρ| below 0.3 is "weak", below 0.6 "moderate", and anything higher
  "strong".
- Each line gives ρ, its strength, the number of laps and what the sign
  meant here, for example "hotter laps were quicker".

**Time of day.** Temperature is also correlated with the order of laps
through the day: runs in recording order, laps in run order. When |ρ| with
that order is at least 0.6, the card warns that the association cannot be
told apart from everything else that changed over the day: the driver,
tyres, track and fuel.

The card also has:
- a scatter of every lap (temperature across, quicker laps up);
- **Show laps**, which lists each lap with its temperature, lap time and
  strong acceleration.

The note under the scatter states that the result describes how the two
moved together on this day. It does not establish a critical temperature or
a cause.

**On the Jastrząb day:** 23 eligible laps, all with full sensor coverage.

| Temperature | ρ with lap time | ρ with strong acceleration | ρ with lap order |
| --- | --- | --- | --- |
| Coolant | −0.84 | +0.84 | +0.86 |
| Engine oil | −0.77 | +0.79 | +0.83 |
| Gearbox | −0.77 | +0.80 | +0.81 |
| Intake | −0.65 | +0.75 | +0.82 |

Every temperature rose through the day while the laps got quicker, so all
four are flagged: the hotter laps are the later, faster ones, and this data
cannot separate temperature from the day's progression.

## Recording clock alignment (KAN-101)

A run can hold alternative recordings beside its primary (KAN-90). Before
any channel could be fused, their clocks must line up. **Run details →
Recordings → Check clock** describes how an alternative lines up with the
primary. It shows the result and changes nothing: no offset is stored or
applied, and both recordings stay usable on their own.

The convention is **primary time = alternative time + offset + drift ×
alternative time**. `alignRecordings` (`telemetry/RecordingAlignment`,
algorithm `recording-alignment-v1`) keeps two kinds of evidence apart:
- **Declared:** each logger's start timestamp (`firstTimestampMilliseconds`).
  RCZ always states one; a RaceChrono VBO states one when it records "File
  created on". The declared offset is the difference between the two.
- **Measured:** the central sync engine cross-correlates the two speed
  traces over the whole overlap. It then repeats this in up to 8 windows of
  at least 60 s along the overlap, each searched within ±10 s of the whole
  result. A window counts when its correlation is at least 0.9. The offset
  and drift (in ppm) are a least-squares line through the counted windows,
  and drift is reported with three or more. The uncertainty is the
  windows' residual, at least 0.05 s (the engine's 10 Hz grid).

The status is never "aligned" by default:
- **insufficient:** a recording has no speed channel, the engine cannot run
  (too few samples), or the overlap is shorter than 20 s.
- **ambiguous:** reported in five cases:
  - the whole-overlap correlation is below 0.9;
  - fewer than two windows count;
  - the windows disagree by more than 0.3 s;
  - the drift exceeds 1000 ppm, beyond what a logger clock plausibly does;
  - **repeated match:** the match is not unique (engine confidence below
    0.75) and no declared clock is available.

  Laps repeat, so two speed traces usually also match about one lap away.
  The speed match alone cannot tell which lap is right.
- **conflicting:** a declared offset differs from the measured one by more
  than 2 s (or three uncertainties). One logger's clock may be wrong or in
  another time zone. Both values are shown and neither is chosen.
- **aligned:** the measured match is unique on its own, or it repeats but
  the declared clock agrees and so chooses between the matches. The result
  says which of the two it was. The offset shown is always the measured
  one.

**Tested on synthetic data only.** The core tests use a synthetic track day
(a lap shape, slow drift across laps, pit exit, a yellow-flag lap and an
in-lap):
- a 123.4 s offset is measured within 0.1 s;
- 400 ppm drift is recovered within 120 ppm;
- identical cycles and constant speed are never approved;
- a declared clock one minute off is a conflict.

**Real day (4 October 2026).** The private Jastrząb day of 29 August
2026 (FlappedEar/refdata) has a VBO and an RCZ for each of its six
sessions. Overlays' own `RecordingAlignment`, run on each pair through
FlappedEar Telemetry's `cpp_fusion_dump --pairs`, aligned all six by speed
alone: offsets −0.133 to −0.150 s, uncertainty 0.05 s, no drift, and the
declared clocks agree within 0.01 s.

## Channel fusion policy (KAN-102)

`fuseChannels` (`telemetry/ChannelFusion`, algorithm `channel-fusion-v1`)
defines how channels from a run's alternative recordings join its primary
recording. The fusion review in Run details (KAN-103, below) applies it.

Every output channel says:
- which source each stretch of samples came from (its source ID);
- the clock transform that source was put through;
- the channel's unit;
- the rule that selected it: `primary`, `added`, `fillGaps`,
  `preferAlternative` or `unresolvedConflict`.

A **stretch** (segment) is a run of samples from one source without a gap.
Each stretch also gives that source's own median sample spacing, so a 1 Hz
OBD channel never claims the primary's 10 Hz resolution.

**Which sources can join.** Only a source whose clock alignment (KAN-101)
was "aligned" is used. Any other status, a non-finite clock, or a drift
that would reverse time refuses the whole source, and the result lists it.
Times move onto the primary clock as **primary time = source time + offset
+ drift × source time**.

**Matching channels.** A channel is matched by its alias when it has one
(for example `speed`), otherwise by its name:
- **Only the alternative has it** (an RCZ's OBD coolant, say): it is added
  on the primary clock.
- **Both have it:** the primary's samples are kept. At each of the
  alternative's samples, the value is compared with the primary's value at
  the same moment. The overlap conflicts when at least 10 samples compare
  and their median difference exceeds a per-unit tolerance: km/h 2, % 3,
  g 0.05, °C 2, rpm 100, otherwise 5 % of the primary's range.
- **A conflict without a rule** is reported as unresolved. The channel
  still carries the primary's samples, so nothing is replaced silently.
- **Rules** are chosen per channel and alternative source:
  - `PrimaryOnly` resolves the conflict and keeps the primary; the
    disagreement is still reported.
  - `FillGaps` keeps the primary and uses the alternative only outside the
    primary's recorded stretches.
  - `PreferAlternative` does the reverse.
- **Units** must match exactly. A mismatch (for example m/s against km/h)
  is listed, and that channel is neither compared nor fused. Nothing is
  rescaled.
- **A unit only one side declares** (VBO channels declare none, RCZ
  channels do; KAN-184) counts as the declared unit only when at least 10
  samples compare and agree within that unit's tolerance. Otherwise the
  channel is listed as a unit mismatch, so values in another scale are
  never fused. The output keeps the primary's unit.

**No resampling.** Every output sample is a real sample of one source with
its timestamp transformed; nothing is interpolated. Gaps that neither
source covers stay gaps. When both have a sample at the same instant, the
preferred source's sample is kept, so timestamps stay strictly increasing.

**Gap markers (KAN-188).** An RCZ marks a gap with two NaN samples one
step inside it. Moved onto the primary clock, such a marker can round onto
its real neighbour; it is kept one step inside the gap instead, so no two
timestamps are equal and no real sample is replaced by its marker. A
merged channel (`FillGaps`, `PreferAlternative`) mixes two cadences, so the
gap threshold read back from it can be longer than either source's: a 1 Hz
primary filled by a 10 Hz alternative reads as 1 Hz and would bridge a 2 s
gap in the alternative. Wherever two neighbouring real samples of a merged
channel are farther apart than the larger of their sources' gap thresholds,
fusion writes the same two NaN markers, so every lookup sees the gap. A
marker belongs to no source's stretch. On the private Jastrząb day (six
VBO/RCZ pairs, `fillGaps` on all 31 shared channels each) the fused output
is unchanged.

**Real day (4 October 2026, KAN-184).** On the same six VBO/RCZ pairs,
with no rules, no channel is added, 31 of the 34 shared channels are
compared and none conflict; 3 are unit mismatches (longitude and two gyro
channels, whose values do not agree within their tolerance). Before
KAN-184 only 2 were compared, because VBO channels declare no unit.
FlappedEar Telemetry's port gives identical results on all six pairs.

### Reviewing and approving a fusion (KAN-103)

In **Run details → Recordings**, **Fuse…** on an alternative recording
loads both recordings, checking that each is still the content that was
attached. It then aligns their clocks and previews the fusion with no rules.
The review shows:
- the clock alignment, in the same words as Check clock;
- every resulting channel: the ones added, with how much of the run they
  cover and their own sample spacing, and each channel both recordings
  have, with its median difference over the compared samples;
- the channels left out because their units differ, or because one
  recording declares no unit and the values do not agree.

**Approving.**
- For a channel both recordings have, you can choose to keep the primary,
  fill the primary's gaps, or prefer the alternative.
- A conflicting channel must have one of these rules before **Approve
  fusion** is enabled.
- The fusion cannot be approved unless the clocks are "aligned".
- Approving stores the decision in the run, bound to both recordings'
  content (see the event project format). The recording is then marked
  **Fused** and the run's analysis includes its channels.

A later change of either recording's content turns the mark into "Fusion
needs review", and the analysis goes back to the primary. The laps always
come from the primary. Heart rate is fused like any other recorded channel;
there is no separate heart-rate workflow.

Clock drift is reported only when it moves the offset across the overlap by
more than twice the windows' uncertainty. Otherwise the offset is the
windows' mean, and the review says that no drift is resolvable. Two
recordings of 3 minutes on the same clock therefore show no spurious drift.

## Automatic segments (KAN-136)

A track layout of the day that has **no approved segments on any run** gets
them automatically:
- the proposals of the day's best lap (its run's lap detail and segment
  proposals, as the segment review computes them) are all approved on that
  run;
- the status bar says how many, and from which lap;
- sector times, the theoretical best, time losses, the Corner Analyzer and
  the day report then work straight after import, with no review.

These are ordinary approved segments. The lap view's segment review edits,
splits, merges or revokes them, and **Approve all** approves every open
proposal of a lap, whatever its boundary uncertainty. The attempt is made
once per document, layout and best lap, so revoked segments do not come back
by themselves. The analysis controller's default is off, so a caller
chooses. FlappedEar Telemetry turns it on; Overlays did until KAN-166 step 2
(October 2026) and no longer creates segments itself.

Opening the run's video, or switching the active run, while the segments are
being created cancels that work; it is started again afterwards, so the
segments are not lost (KAN-142). The "could not be created" message is only
for a real failure.

A lap whose layout, direction or timing gate is not identified cannot store
segments. Its segment review says so and points to Correct grouping, instead
of offering proposals that cannot be approved.

**Silesia Ring, 27 September 2026 (private):**
- All three sessions now resolve to one clockwise layout (before the
  cross-track route matching, only Session 2 did).
- 16 segments are created automatically from Session 3 LAP 3 (2:13.774).
- Laps that leave the other laps' line by more than 12 m, such as the
  off-track Session 2 LAP 2, are excluded from ranking (KAN-137; see the
  route rules in `event-project-format.md`).
- The theoretical best is 2:11.541, and most time is in Corners 10–11
  (+0.514 s).


## Tyre temperature and pressure (KAN-132)

The tyres widget reads each corner's own tyre channels (`telemetry/TyreData`,
`tyre-data-v1`). Nothing is substituted. A corner without a channel is shown
as a dash, and so is a moment where its channel has no valid value.

- **Temperature is °C.** A channel that declares Fahrenheit is converted.
  Plausible values are −40 to 250 °C.
- **Pressure is converted to bar.** A declared unit (`bar`, `psi`, `kPa`)
  decides the conversion. Without one, the median of the channel's non-zero
  samples decides: 0.5–8 is bar, 8–100 psi, 100–1000 kPa. A channel outside
  those ranges is not treated as a tyre pressure, and that corner's pressure
  is unavailable. Plausible values are above 0 and up to 10 bar.
- **An exact 0 is a placeholder.** TPMS sensors report 0 until their first
  reading. A tyre at exactly 0 °C or 0 bar therefore shows as no data.
- Values are interpolated between the two neighbouring valid samples, never
  across a placeholder, an implausible sample or a gap in the channel. There is
  no smoothing and no stale hold, unlike the overlay channels above.

**Silesia Ring, 27 September 2026 (private):**
- RaceChrono records `tyre_temp_{fl,fr,rl,rr}-canbus` and
  `tyre_pressure_{fl,fr,rl,rr}-canbus` with no declared unit.
- The owner confirmed that temperatures are °C.
- The pressures, 182–308 in steps of about 3, classify as kPa: 214 is
  2.14 bar.
- Sampled every second, each corner has values 84–100% of the session.
  Temperatures are 24–53 °C and pressures 1.82–2.33 bar.
- One exception is Session 1 FR, which reads 3.08 bar for about three
  seconds. That is what was recorded, and it is shown as is.
- The Jastrząb day has no tyre channels, so the widget shows dashes there.

## Day results belong to their day (KAN-151)

The theoretical best (with time losses and sector progression) and the
channel summaries are computed in the background.

- **Another day opening** (another project, or a new or imported event):
  - every such request is given a new number, so a result still on its way is discarded;
  - the work is cancelled;
  - what is shown is cleared.
- **The same day's sources changing**, for example its video opening: results are kept.
- **Workers still running** count as work in progress, so they are cancelled before the application quits.
