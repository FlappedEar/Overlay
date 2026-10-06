# KAN-141 code review, 6 October 2026

Review of `main` at `32ba970` across four areas: project and asynchronous document handling,
editor QML, export, and telemetry parsing. Each finding was checked against the code before it was
filed. Severity is the reviewer's estimate of user impact.

## Findings

| ID | Severity | Finding | Outcome |
|----|----------|---------|---------|
| P1 | Medium | A failed or rejected Open left the current project's in-flight video and telemetry loads idle. | Fixed, KAN-195 (#181) |
| P2 | Medium | Alternative-recording results were not guarded by source generation or cancelled by Open/New. | Fixed, KAN-196 (#183) |
| P3 | Low–medium | Save was allowed while a project open was in flight. | Fixed, KAN-195 (#181) |
| P4 | Low | The degraded-recovery flag was never cleared after Save, New or Open. | Fixed, KAN-195 (#181) |
| P5 | Medium (policy) | A source reference without a fingerprint is accepted by path and the fingerprint is stamped. | Kept by owner decision, KAN-200 |
| P6 | Low | A version 1 recovery snapshot skipped the revision check and could restore as clean. | Fixed, KAN-195 (#181) |
| Q1 | High | The very verbose export log never updated while open. | Fixed, KAN-194 (#180) |
| Q2 | Medium | The verbose log view drifted when the bounded log trimmed its head. | Fixed, KAN-194 (#180) |
| Q3 | Medium | The widget editor draws designed-widget panels itself instead of using `TelemetryPanel`. | Fixed, KAN-199 (#185) |
| Q4 | Low–medium | Canvas gauges repaint their static face on every telemetry tick. | Fixed, KAN-199 (#185) |
| Q5 | Low | Editor QML uses literal font sizes, colours and radii instead of Theme tokens. | Fixed, KAN-199 (#189) |
| E1 | Medium | HDR was detected only from the transfer tag; HDR side data or BT.2020 without a transfer passed as unknown. | Fixed, KAN-198 (#184) |
| E2 | Low | The export worker trusted output and temporary-overlay paths from its configuration. | Fixed, KAN-198 (#184) |
| E3 | Low (latent) | The frame packer accepted straight RGBA as premultiplied. | Fixed, KAN-198 (#184) |
| T1 | Medium | Channel fusion maps RCZ gap markers through the offset, producing equal timestamps. | KAN-188 |
| T2 | Medium | A merged fused channel recomputes its gap threshold over mixed rates and can bridge real gaps. | KAN-188 |
| T3 | Medium (policy) | Overlays hold the last value for up to 0.75 s (heart rate 2 s) into gaps and past the end. | Kept by owner decision, KAN-200 |
| T4 | Medium–high | GoPro GPMF parsing copied nested containers, multiplying memory, with no packet cap. | Fixed, KAN-197 (#182) |
| T5 | Low | ffprobe output is buffered without a bound before the size cap is checked. | Fixed, KAN-201 (#188) |
| T6 | Low (latent) | Driving distance integrates speed across telemetry gaps. | Fixed, KAN-201 (#188) |

Areas found sound: atomic saves, dirty-state prompts, missing media handling, generation guards
outside recordings, cancellation, the chapters timeline, sidebar scrolling, shortcut gating, preview
geometry, preview and export sharing `TelemetryScene`, export overwrite safety, the staged export
architecture, alpha paths, backpressure, rational frame rates, frame ranges and the absence of a
resolution cap.

## Owner decisions (KAN-200)

On 6 October 2026 the owner chose to keep both policy behaviours. AGENTS.md records them as
allowed exceptions:

- Overlays hold the last sample for a short, bounded time into a gap or past the end.
- A source reference saved without a fingerprint is accepted by path, and its fingerprint is
  recorded from then on.

## Real recordings (Linux, not a supported platform)

Run against the owner's Jastrząb day of 29 August 2026 from `FlappedEar/refdata`:

- `measuresAPrivateFullDay`: 25 laps, 14 segments, as in
  [kan79-full-day-acceptance.md](kan79-full-day-acceptance.md).
- `automaticallyGroupsPrivateTrackDay`: passes.
- `derivesOptionalRealVboLaps`, session 14:37:25: 5 laps, fastest 113.277 s.
- RCZ against VBO pair: speed median difference 0.038, maximum 0.34; RPM median 2.24, maximum 50;
  brake maximum 0.88; lap times within 0.0098 s. Heart rate has a median difference of 0 but a
  maximum of 133, recorded as an observation and not investigated further.
