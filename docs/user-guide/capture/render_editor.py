"""Capture the editor window from the production Main.qml with sample data.

Run with a Python that has PySide6 6.8 and imageio-ffmpeg installed, passing a
real RaceChrono VBO recording:

    FET_CAPTURE_VBO=/path/to/session.vbo python docs/user-guide/capture/render_editor.py
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent, QQmlExpression, QQmlEngine
from PySide6.QtQuick import QQuickView
from PySide6.QtQuickControls2 import QQuickStyle

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness  # noqa: E402
import realdata  # noqa: E402

OUT = harness.REPO / "docs" / "user-guide" / "assets" / "screens"
MOMENT_IN_BEST_LAP = 27.0
HERE = Path(__file__).resolve().parent


def settle(app, seconds=0.8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        app.processEvents()
        time.sleep(0.02)


def placeholder_clip(app, work: Path, seconds: float) -> Path:
    """A still-image clip as long as the recording. No onboard video was supplied for the
    capture, so the picture is the neutral backdrop and telemetry time equals video time."""
    cache = Path.home() / ".cache" / "fet-capture"
    cache.mkdir(parents=True, exist_ok=True)
    clip = cache / f"placeholder-g30-{seconds:.3f}.mp4"
    if clip.exists():
        return clip
    view = QQuickView()
    view.engine().addImportPath(str(work))
    # Keep Python-owned objects alive for the view's lifetime.
    view._keep = (harness.SampleRenderContext(), harness.SampleWidgetModel())
    view.setInitialProperties({"renderContext": view._keep[0], "widgetModel": view._keep[1]})
    view.setResizeMode(QQuickView.SizeRootObjectToView)
    view.resize(1920, 1080)
    view.loadFromModule("FlappedEar", "VisualSmokeScene")
    view.show()
    settle(app, 0.5)
    still = work / "backdrop.png"
    view.grabWindow().save(str(still))
    view.close()
    import imageio_ffmpeg
    subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-loglevel", "error", "-y", "-loop", "1", "-i", str(still),
                    "-t", f"{seconds:.3f}", "-r", "30", "-g", "30", "-c:v", "libx264", "-preset", "veryfast",
                    "-pix_fmt", "yuv420p", str(clip)], check=True)
    return clip


def timecode(seconds: float, fps: int = 30) -> str:
    frames = round(seconds * fps)
    s, f = divmod(frames, fps)
    return f"{s // 3600:02d}:{s // 60 % 60:02d}:{s % 60:02d}:{f:02d}"


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    # The application uses the Basic Quick Controls style (native/src/main.cpp).
    QQuickStyle.setStyle("Basic")
    app = QGuiApplication(sys.argv)
    app.setApplicationDisplayName("FlappedEar Overlays")
    work = Path(tempfile.mkdtemp(prefix="fet-editor-"))
    harness.stage_module(work)
    harness.register_branding(work)
    recording = realdata.load()
    best = recording.best_lap()
    moment = best["start"] + MOMENT_IN_BEST_LAP
    duration = recording.times[-1]
    clip = placeholder_clip(app, work, duration)

    model = harness.SampleWidgetModel()
    model.set_widgets(harness.template_widgets("motorsport-broadcast-smoke"))
    context = harness.real_context(recording, moment)
    handle = 6.0
    lap_in, lap_out = best["start"] - handle, best["end"] + handle
    channel_names = list(recording.app_channels())
    data = {
        "videoName": "placeholder.mp4",
        # Uploaded copies carry an 8-hex-digit prefix; show the recording's own file name.
        "telemetryName": re.sub(r"^[0-9a-f]{8}-", "", Path(os.environ["FET_CAPTURE_VBO"]).name),
        "channelNames": channel_names,
        # Same text as TelemetryController reports after a successful open.
        "statusText": f"Telemetry opened: {len(recording.times)} samples, {len(channel_names)} numeric channels.",
        "liveValues": {name: recording.app_value(name, moment) for name in channel_names},
        "sampleCount": len(recording.times),
        "telemetryDuration": duration,
        "currentLapNumber": recording.lap_at(moment)["number"],
        "lapSummaries": [{key: lap[key] for key in ("number", "durationSeconds", "isBest", "hasDelta",
                                                    "deltaToBestSeconds", "referenceEligible")}
                         for lap in recording.laps],
        "outingRanking": {"bestOfDay": {"lapNumber": best["number"], "runId": "", "runName": "",
                                        "durationSeconds": best["durationSeconds"]}},
        "previewEndPositionMilliseconds": int((duration - 1 / 30) * 1000),
        "previewEndTimecode": timecode(duration - 1 / 30),
        "initialPositionMilliseconds": int(moment * 1000),
        "exportSourceInfo": {"width": 1920, "height": 1080, "frameRateText": "30/1 (30.000 fps)",
                             "videoCodec": "h264", "videoCodecProfile": "High", "bitDepth": 8,
                             "audioCodecs": [], "duration": duration},
        "formatOptions": {"sizes": [{"width": 1920, "height": 1080, "label": "1920×1080 (Source)"},
                                    {"width": 1280, "height": 720, "label": "1280×720"}],
                          "rates": [{"numerator": 30, "denominator": 1, "label": "30.00 fps (Source)"},
                                    {"numerator": 15, "denominator": 1, "label": "15.00 fps"}]},
        "lapRange": {"valid": True, "lapNumber": best["number"], "inTimecode": timecode(lap_in),
                     "outTimecode": timecode(lap_out), "durationSeconds": lap_out - lap_in, "handleSeconds": handle},
    }

    engine = QQmlApplicationEngine()
    engine.addImportPath(str(work))
    component = QQmlComponent(engine, QUrl.fromLocalFile(str(HERE / "qml" / "MockAppController.qml")))
    controller = component.createWithInitialProperties(
        dict(data, widgetModel=model, renderContext=context, videoSource=QUrl.fromLocalFile(str(clip))))
    if controller is None:
        raise SystemExit(component.errorString())
    engine.rootContext().setContextProperty("appController", controller)
    engine.loadFromModule("FlappedEar", "Main")
    if not engine.rootObjects():
        raise SystemExit("Main.qml did not load")
    window = engine.rootObjects()[0]
    window.resize(1440, 900)
    settle(app, 2.0)

    def js(expression: str):
        expr = QQmlExpression(QQmlEngine.contextForObject(window), window, expression)
        result = expr.evaluate()
        if expr.hasError():
            print("JS error:", expr.error().toString())
        # PySide returns (value, valueIsUndefined).
        return result[0] if isinstance(result, tuple) else result

    def wait_for(expression: str, timeout: float = 60.0) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if js(expression):
                return True
            settle(app, 0.2)
        print("timed out waiting for", expression)
        return False

    def shot(name: str, seconds=0.8, crop=None):
        context.advance()
        settle(app, seconds)
        image = window.grabWindow()
        x, y, w, h = crop if crop is not None else (0, 0, image.width(), image.height())
        top = max(y, menu_height)
        image = image.copy(x, top, w, h - (top - y))
        image.save(str(OUT / f"{name}.png"))
        print("wrote", name)

    # Park the playhead on the moment the overlay values come from.
    target = int(moment * 1000)
    wait_for(f"Math.abs(window.timelinePosition - {target}) < 200 && !window.previewPrimeFramePending", 60)
    js("mediaPlayer.pause()")
    settle(app, 1.0)
    print("playhead ms:", js("window.timelinePosition"))
    # macOS shows the menus in the system menu bar, not inside the window.
    menu_height = int(js("window.menuBar ? window.menuBar.height : 0") or 0)

    # Editor with a selected widget and the WIDGET tab.
    js("window.welcomeVisible = false")
    js("window.selectWidget(0, false)")
    shot("editor-window", 1.5)

    js("inspector.currentTab = 1")
    shot("editor-inspector-data", crop=(1440 - 350, 0, 350, 900))
    js("inspector.currentTab = 2")
    shot("editor-inspector-cues", crop=(1440 - 350, 0, 350, 900))
    js("inspector.currentTab = 0")

    # Welcome screen.
    js("window.welcomeVisible = true")
    shot("welcome")
    js("window.welcomeVisible = false")

    # Export dialog, single-lap mode. The dialog is taller than a 900 px window.
    window.resize(1440, 1080)
    settle(app, 0.5)
    js("exportDialog.open()")
    settle(app, 0.5)
    js("exportDialog.outputFile = 'file:///Users/you/Movies/Jastrzab-lap-4.mp4'")
    js("exportRangeMode.currentIndex = 2")
    shot("export-dialog")
    js("exportDialog.close()")
    window.resize(1440, 900)
    settle(app, 0.5)

    # Save-as-new template popup.
    js("templateSavePopup.open()")
    shot("template-save-popup")
    js("templateSavePopup.close()")


if __name__ == "__main__":
    main()
