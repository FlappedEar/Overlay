# KAN-252 real-footage check: extra video, picture in picture, camera switching

Run on macOS (Debug build of `main` at 247fddb) on 9 October 2026 with the owner's footage. Nothing
from the footage is committed or published.

## Inputs

- Main video: GoPro `GX010091.MP4`, 3840×2160 `60000/1001`, 25 min, with GPMF.
- Telemetry: the matching RaceChrono VBO (`session_20260829_172004`, session 6 of the day).
- Second camera: a DJI Osmo Nano export, 3840×2160 `60/1`, 8 min.
  **It is not from the same moment as the GoPro file.** Its creation time is 07:15 UTC and the GoPro's
  is 15:20 UTC, so the two cannot be synchronised by sound or by time. The check therefore aligned
  them by hand at a fixed point (extra second 20 at main second lap-2 start + 5). Alignment accuracy
  between two cameras is **not** verified by this run.

## Results

| Check | Result |
| --- | --- |
| GoPro/VBO auto-sync on the real pair (KAN-80) | Offset 7.817 s, correlation 0.998, confidence 0.860; lap 1–3 map to video 435.6 s, 551.0 s, 667.3 s. |
| Extra video added, probed, ready (KAN-131) | Pass. The video is kept as a source, not a target. |
| Picture in picture, no cuts | Pass. The helmet camera shows in the top-right box, overlay widgets over it. |
| Hard cuts (to the helmet camera at +8 s, back to main at +16 s) | Pass. Frames at 7.5 s, 8.5 s, 12 s and 16.5 s show the right camera filling the frame and the other in the box. |
| Crossfade (1 s) | Pass. A blended frame is seen at the cut (frame at 12 s is the full second camera, at 16.5 s the blend towards main). |
| Side by side | Pass. Main left, second camera right, both letterboxed, widgets over the whole frame. |
| Main audio only (KAN-131) | Pass. One AAC stream; correlation 0.997 (lag 4 samples at 8 kHz) with the GoPro's own audio for the same 24 s; the crossfade export without audio has none. |
| Export validation | Each 24.04 s, 1441-frame 1920×1080 HEVC export passed final validation (frame count, A/V start delta 0, audio duration within one frame). |

## Not covered

- Real-time preview and mouse/keyboard use of the editor on real footage: this run drove the
  controllers through an automated test, not the window. The owner should try the DATA tab and the
  1–4 camera keys on the real files.
- Simultaneous footage (no helmet recording of the same outing exists), so the offset search for
  two cameras on real data, and cut timing against what happens on track.
- Full-length and chaptered (`GX020091`) exports (KAN-81): only one 24 s range per layout was exported.

## Observation, not changed

The default box (top right) lies under the default templates' map and heart-rate widgets, so the
second camera is partly covered until the box is moved. The owner's planned editable picture-in-picture
box would make this a user choice.
