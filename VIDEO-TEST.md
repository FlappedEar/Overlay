# Steering widget: video test

Paste everything below the line into the Overlays project, after the steering prompt (PROMPT.md). Files in this folder: `steering-widget-test.mp4` and `steering-widget-test.vbo`.

---

A test for the steering wheel overlay with a known answer at every moment.

## Files
- `steering-widget-test.mp4`: 1920x1080, 30 fps, 39.0 s. It shows a drawn steering wheel (red mark at 12 o'clock when straight) and, in big text, what the widget must show at that moment ("90° L", "45° R", "0°"), plus the raw `obd_steering_angle` value and the VBO time.
- `steering-widget-test.vbo`: 976 rows at 25 Hz (14:19:54.110 to 14:20:33.110), the same format as `jastrzab-frs-steering-test.vbo`. Only `obd_steering_angle` is scripted; the GPS, speed, brake and rpm columns are the Jastrząb test lap, unchanged.

## Sync
Video frame 0 = the first VBO data row (time 141954.110). Offset 0. Every frame prints "video X s / VBO time HH:MM:SS.sss" so a wrong offset is visible.

## Script (+ = left / anticlockwise, - = right)
Each change is a 1.5 s smooth ramp, then a 2.5 s hold:

| Video time | Widget must show | obd_steering_angle |
|---|---|---|
| 0.0-2.0 s | 0° | 0 |
| 3.5-6.0 s | 90° L | +90 |
| 7.5-10.0 s | 0° | 0 |
| 11.5-14.0 s | 45° R | -45 |
| 15.5-18.0 s | 0° | 0 |
| 19.5-22.0 s | 180° L | +180 |
| 23.5-26.0 s | 90° R | -90 |
| 27.5-30.0 s | 360° L (one full turn, wheel looks straight, text says 360° L) | +360 |
| 31.5-34.0 s | 450° R (full lock) | -450 |
| 35.5-39.0 s | 0° | 0 |

## Acceptance
Render the overlay on this video with this VBO, with the steering widget next to the drawn wheel. It passes when:
1. During every hold the widget's text matches the big text (rounded to whole degrees, L for positive, R for negative, plain "0°" at zero).
2. The widget's wheel graphic turns the same way as the drawn wheel: anticlockwise for L, clockwise for R. Its top mark is at 9 o'clock at 90° L and at 3 o'clock at 90° R.
3. Angles over 360° are not wrapped in the text: 450° R stays "450° R", not "90° R".
4. During the ramps the widget moves smoothly with the drawn wheel, no jumps or lag (the VBO is 25 Hz, the video 30 fps: interpolate between rows).
