# User-guide screenshot capture

## Native capture (the current pictures)

The pictures in `assets/screens/` come from the application itself: its own
`AppController` and production QML, driven step by step by the opt-in test
`TelemetryTests::capturesUserGuideScreens` (`native/tests/UserGuideCapture.cpp`),
with a real onboard video and its recording. The capture imports the day
through **Import runs**, opens the video, runs **Auto Sync**, exports a short
clip for real, and walks through the editor, the templates, every widget and
Lap Analysis. `template-save-popup.png` is the only picture still made by the
QML-only scripts below.

The media is private. It is read in place, never copied or committed:

```bash
cmake --build build-native --parallel
FLAPPEDEAR_GUIDE_CAPTURE_DIR=/path/to/output \
FLAPPEDEAR_GUIDE_VIDEO=/path/to/GX010089.MP4 \
FLAPPEDEAR_GUIDE_VBO=/path/to/session_20260829_160659_jastrząb_kaizenvtec.vbo \
FLAPPEDEAR_GUIDE_DAY=/path/to/folder/with/that/days/vbos \
FLAPPEDEAR_GUIDE_CHAPTERS=/path/to/GX010091.MP4,/path/to/GX020091.MP4 \
QTEST_FUNCTION_TIMEOUT=3600000 \
build-native/native/tests/flappedear_native_tests capturesUserGuideScreens
cp /path/to/output/*.png docs/user-guide/assets/screens/
python3 docs/user-guide/build.py
```

- `FLAPPEDEAR_GUIDE_VIDEO` and `FLAPPEDEAR_GUIDE_VBO` are a matching GoPro clip
  and VBO. The clip needs GoPro GPS for Auto Sync.
- `FLAPPEDEAR_GUIDE_DAY` (optional) is the folder of that day's VBOs. They are
  imported as one event; the recording's run becomes the active one.
- `FLAPPEDEAR_GUIDE_CHAPTERS` (optional) is a GoPro recording in chapters, for
  the Video chapters dialog.
- It runs on macOS with a display, in about two and a half minutes. The test
  export goes to `/tmp/FlappedEar/` and is deleted afterwards.
- The pictures show the moment 38 s into the recording's best lap. Captions
  quote values from the pictures: when the media or the application changes,
  check the captions too.

Capture notes:

- The day is imported first, and the video is opened only after the
  automatic segments exist. Opening it earlier currently loses them.
- A paused player on macOS shows no frame after a seek, so the lap view
  plays the moment for two seconds before its picture.
- The test executable hands `--export-worker` to the application's own
  worker, so a controller export in a test runs for real.

## QML-only capture (no application build or media)

These scripts render the user-guide pictures from the application's own QML
(`native/qml`) with a real RaceChrono recording. The C++ controllers are replaced
by small stand-ins, filled from the recording. They live in `harness.py`,
`realdata.py` and `qml/Mock*.qml`. The application never uses these files.

The recording is private and must never be committed. Pass its path with
`FET_CAPTURE_VBO`. Only RaceChrono Pro 10.2.4 VBO files are supported, because
`realdata.py` reads their coordinate units and timing gate.

## What is real and what is not

- **Real:**
    - Telemetry values, the track outline and lap times.
    - Lap sections, the UTC clock, direction, eligibility, chart traces and channel names.
    - These are computed from the recording with the application's documented rules: the Start gate crossing, the reference-position match within the ±18 % window, gap-preserving min/max decimation, and the 12 m off-line rule.
- **Placeholder:** the video picture is the neutral backdrop from `VisualSmokeScene.qml`, encoded as a still clip as long as the recording. Telemetry time equals video time, so the synchronization offset is 0.
- **Not shown:** values that only exist at runtime, such as export progress, auto-sync results, comparison metrics and the day report. No screenshot shows them, rather than inventing numbers.

Lap detection in `realdata.py` mirrors the application closely, but it is not the application's parser. Compare its lap times with the application when you change capture data.

## Requirements

- Python 3.11 with `PySide6==6.8.3` (the Qt version CI uses) and `imageio-ffmpeg`.
- The Inter font. `Helvetica Neue` is aliased to Inter through fontconfig on Linux.
- Video frames in the editor need an OpenGL scene graph:
    - Xvfb with Mesa, plus a PulseAudio null sink. Without an audio sink, Qt Multimedia's FFmpeg backend does not advance playback.
    - Widget and Analysis captures run with the offscreen platform and need neither.

```bash
python3 -m venv /tmp/fet-capture && /tmp/fet-capture/bin/pip install PySide6==6.8.3 imageio-ffmpeg
export FET_CAPTURE_VBO=/path/to/session.vbo
/tmp/fet-capture/bin/python docs/user-guide/capture/render_overlays.py
/tmp/fet-capture/bin/python docs/user-guide/capture/render_analysis.py
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl \
  xvfb-run -a -s "-screen 0 2400x1400x24" /tmp/fet-capture/bin/python docs/user-guide/capture/render_editor.py
python3 docs/user-guide/build.py
```

Output goes to `docs/user-guide/assets/screens/`. Re-run the scripts whenever the QML changes, so the guide matches the application.

## Capture notes

- **Video priming:** the editor primes video playback at `previewInitialPositionMilliseconds()`, so the capture starts the clip at the chosen moment.
- **Seeking:** with Qt Multimedia's FFmpeg backend, a seek re-reports `LoadedMedia`. Main.qml's priming handler then returns to the initial position. AVFoundation on macOS does not behave this way; the full product review (KAN-141) should check other backends.
- **Retro speed arc:** the clipped segments in its picture are real. Its default geometry cuts them off (KAN-140).
