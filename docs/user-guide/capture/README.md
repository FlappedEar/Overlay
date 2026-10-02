# User-guide screenshot capture

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
