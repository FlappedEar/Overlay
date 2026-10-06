# Native RaceChrono RCZ import

The editor and export worker share `TelemetrySource::load`, which dispatches VBO or RCZ
by extension. RCZ files work through ordinary telemetry import, asynchronous source
loading, project reopen/relink/recovery, analysis, preview, and export. The project
schema is unchanged; its telemetry source reference stores the original RCZ path.
The internal worker key `vboPath` is retained for compatibility.

## Supported scope

The initial reader accepts flat ZIP32 shared sessions with `session.json` and
`sessionfragment.json`, both version 1 and with the same first timestamp. Stored and
deflated members are supported. It was developed against one private RaceChrono Pro
10.2.4 RCZ and its matching 10 Hz VBO export. This is observed format support, not a
claim to cover every RaceChrono version or device.

Resumed sessions, nested/multiple-session backups, ZIP64, encryption, unsupported
encodings, missing primary GPS channels, and ambiguous semantic sources fail explicitly.
The fragment selects the primary GPS. Additional accelerometer/gyro devices retain
separate channel names. Unknown recorded channels produce a warning and are omitted.
External video/sync-point references in the archive are not automatically imported;
video selection and synchronization remain under the editor's existing controls.

## Verified binary interpretation

Names have the shape `channel_<kind>_<device>_<group>_<id>_<storage>` or `channel2_…`.
All numbers are little endian. Channel ID 1/storage 1 contains signed 64-bit epoch
milliseconds. ID 2 is distance, not time. Position ID 3/storage 1 contains paired signed
32-bit latitude/longitude in 1/6,000,000 degree units. Native RCZ longitude is positive
East; the verified RaceChrono VBO parser retains west-positive source longitude
and records `gpsLongitudeConvention=west-positive`. Shared map projection honors
that metadata for both track outlines and playback markers, including lap detail.
Source samples and timing gates retain their original coordinate convention.

| Recorded source | IDs | Encoding and output |
| --- | --- | --- |
| GPS (kind 1) | 4 | int32 mm/s → km/h |
| GPS | 5, 6, 46, 30007 | int32 / 1000 → altitude m, heading degrees, battery %, accuracy m |
| GPS | 30002, 30003 | int32 satellite count / fix type |
| Accelerometer (kind 2) | 9–11 | int32 / 10000 → device-axis g |
| Gyroscope (kind 3) | 12–14 | int32 / 1000 → degrees/s |
| Magnetometer (kind 8) | 28–30 | int32 / 1000 → µT |
| Heart rate (kind 6) | 41 | int32 / 1000 → bpm |
| OBD (kind 5, channel2/storage 3) | 10024 | double → rpm |
| OBD | 1002, 10025, 10071 | double → brake, throttle, accelerator % |
| OBD | 1005, 10026, 10029, 10066 | double → gearbox, coolant, intake, oil °C |
| OBD | 4 | double m/s → km/h |
| CAN-bus (kind 12, channel2/storage 3) | (position << 15) \| 1013 | double → `tyre_temp_<rr/rl/fr/fl>-canbus` °C, positions 3–6 = RR, RL, FR, FL (KAN-203) |
| CAN-bus | (position << 15) \| 1014 | double → `tyre_pressure_<…>-canbus` kPa |

The CAN-bus tyre layout was verified on the owner's Silesia Ring session of 27 September 2026: all eight channels match RaceChrono's own VBO export of that session sample for sample (median difference 0). Other CAN-bus channels are left out with a warning.

Each group retains its own timestamp array relative to the single session origin.
There is no resampling to video FPS or independent per-channel zeroing. Integer
sentinels, nonfinite doubles and invalid positions become internal missing values;
public lookups never expose nonfinite values. Intervals above three times the channel's
median cadence receive missing-data boundaries so analysis and interpolation cannot
bridge a recording gap. Ordinary slow OBD/HR sampling remains interpolable.

Only actual GPS speed, RPM, throttle, brake and HR channels receive those semantic
aliases. Device-axis acceleration is not assigned to vehicle lateral/longitudinal G.
RaceChrono-derived G/lean channels are absent from the verified archive and are not
synthesized. Import details explain this distinction and name the available raw
device-axis channels (`x_acc-acc`, `y_acc-acc`, `z_acc-acc`, when recorded).
The VBO reader recognizes `latacc` and `longacc` as lateral and longitudinal
acceleration. Their presence in a VBO export does not establish that equivalent
calculated channels are stored in its source RCZ archive.
When a VBO contains `latacc-calc` / `longacc-calc`, these explicitly calculated
channels supply the acceleration aliases ahead of generic `latacc` / `longacc`.
The original columns remain independently selectable. Missing calculated samples
remain missing; the aliases never fall back to a generic zero at those timestamps.
G ball and radar use braking-up / acceleration-down presentation. Numeric data is
unchanged (positive acceleration, negative braking). The manual longitudinal-axis
inversion still reverses presentation; previously saved inversion settings are
retained, so disable a previously enabled workaround to use the corrected direction.

For the verified unidirectional type-3 Start trap, the stored coordinate is the centre,
width is millimetres, and bearing is the travel direction in thousandths of a degree.
The gate extends half its width on each side, perpendicular to travel. This interpretation
was inferred from the recording and verified against all six recorded Start passage
timestamps; the resulting five lap durations agree within 0.01 s. Unsupported gate types
are ignored with a warning; invalid optional gate metadata does not discard otherwise
valid telemetry. Archive integrity and resource-limit failures remain fatal. Laps are
derived through the existing native GPS/gate implementation, not copied from metadata.

**Library tracks (KAN-204).** When the session uses a track from RaceChrono's track
library, `trackId.json` holds only the track id (for example `{"id":32920}`) and no traps.
The start/finish line is then rebuilt from RaceChrono's lap boundaries in `session.json`,
which are line crossings: the GPS position at each lap start and finish timestamp, and the
direction of travel over the second around it. The gate is centred on the mean crossing,
across the mean direction of travel, and covers every crossing plus 5 m on each side (at
least RaceChrono's default 20 m). No gate is built from fewer than two crossings, from
crossings more than 25 m apart along the direction of travel or in different directions,
or wider than 200 m. The session warns that the line was rebuilt. Verified on the owner's
Silesia Ring session of 27 September 2026: 7 laps, each within 0.02 s of RaceChrono's own
lap times (the VBO export's laps agree within 0.012 s).

The matching RaceChrono Pro 10.2.4 VBO exporter uses centre plus a point backward
along travel, with vector length equal to the full gate width. The VBO parser identifies
that exact producer in `[comments]`, rotates the metric vector perpendicular to travel,
and uses half the width on either side of the centre. Generic VBO files retain endpoint
geometry. Other explicitly identified RaceChrono versions keep telemetry but omit gates
with a warning until their representation is verified. This is fixture-verified behavior,
not a universal VBO specification. Both native RCZ and corrected VBO derive five complete
laps from the supplied recording; maximum duration errors against stored RaceChrono
lap metadata are 0.0076 s and 0.0104 s respectively.

## Resource and integrity limits

The reader never extracts files. It validates central/local headers, paths, duplicate
names, offsets, overlapping members, sizes and CRCs for consumed members. Streaming
zlib inflation is capped before appending output, including streams whose advertised
uncompressed size is dishonest. Cancellation is checked during archive traversal,
inflation, timestamp/value decoding and gap insertion.

Limits: 128 MiB archive, 1,024 members, 32 MiB per expanded member, 256 MiB total declared
expanded data, 1 MiB per metadata document, JSON depth 24, 4,096 collection entries/string
characters, 2 million samples per clock, 8 million decoded channel values including gap
markers, 256 retained channels and a 24-hour session span. These limits intentionally
reject unusually large or unsupported recordings instead of partially loading them.

## Validation

`flappedear_rcz_tests` generates only synthetic fixtures: stored/deflated archives,
native timestamps and units, primary GPS, missing values and real gaps, invalid
schemas/lengths/encodings, ambiguous sources, malformed paths, archive corruption,
resource limits and cancellation. The application suite tests save/reopen/relink and
a short RCZ export through the production worker. The fixture selects `libx265`
through the worker's optional `encoder` setting and uses the normal output transaction
and ownership manifest through final validation/commit; ordinary editor exports retain
automatic selection. Both suites run in Cloud CI.

A separate private comparison can be run without uploading recordings:

```bash
FLAPPEDEAR_REAL_RCZ=/path/to/session.rcz \
FLAPPEDEAR_RCZ_REFERENCE_VBO=/path/to/matching.vbo \
FLAPPEDEAR_RCZ_REFERENCE_SESSION_JSON=/path/to/extracted/session.json \
  build-native/native/tests/flappedear_rcz_tests
```

The matching VBO is resampled and rounded by RaceChrono. Compare at absolute recording
times, not row indices: native channels have different rates and coverage. Median
absolute error thresholds are 0.2 km/h for speed, 5 rpm, 0.2 bpm and 0.2 percentage points
for brake, with at least half the reference samples covered. Complete lap counts and passage times are checked against the original RCZ
`session.json`; start times and each lap duration must differ by less than 0.1 s. This optional test expects
a recording with complete laps and those recorded channels; CI reports it as skipped. The private local Qt 6.8.3 run passed 30 tests, including
this recording comparison (34 native channels; 138,551 samples in the fastest channel).

## Dependency and interpretation provenance

The reader is original C++ code. Channel naming and candidate units were cross-checked
with [RaceChrono-to-CSV's channel reference](https://github.com/jLynx/RaceChrono-to-CSV/blob/42aab24de857d68e75f28771b292ce52cb4c7ddd/channel_ids.py)
and verified against the supplied archive/VBO pair. No external decoder is executed.
The ZIP inflater uses static [zlib 1.3.2](https://zlib.net/), pinned by source SHA-256 in
CMake; its permissive license is recorded in `THIRD_PARTY_NOTICES.md`.
