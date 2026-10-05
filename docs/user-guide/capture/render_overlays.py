"""Render overlay widgets and built-in templates with the production QML.

Writes PNG files into docs/user-guide/assets/screens/. Run with a Python that
has PySide6 6.8 installed, passing a real RaceChrono VBO recording:

    FET_CAPTURE_VBO=/path/to/session.vbo python docs/user-guide/capture/render_overlays.py
"""

from __future__ import annotations

import os
import sys
import tempfile
import time
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import QRect, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickView
from PySide6.QtQuickControls2 import QQuickStyle

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness  # noqa: E402
import realdata  # noqa: E402

OUT = harness.REPO / "docs" / "user-guide" / "assets" / "screens"
WIDTH, HEIGHT = 1920, 1080

# Moment shown: seconds into the recording's best lap.
MOMENT_IN_BEST_LAP = 27.0

# (catalog type, file name, geometry overrides, setting overrides)
GALLERY = [
    ("speed", "widget-speed", {}, {}),
    ("heartRate", "widget-heart-rate", {}, {}),
    ("pedals", "widget-pedals", {}, {}),
    ("f1GForceRadar", "widget-f1-radar", {}, {}),
    ("gForceMagnitudeBar", "widget-gforce-bar", {}, {}),
    ("tyres", "widget-tyres", {}, {}),
    ("retroCustomValue", "widget-retro-custom", {"height": 0.06}, {"source": "coolant_temp-obd", "label": "COOLANT", "unit": "°C", "decimals": 0}),
    ("lapCurrent", "widget-lap-current", {}, {}),
    ("retroTachometer", "widget-retro-rpm", {}, {}),
]


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    # The application uses the Basic Quick Controls style (native/src/main.cpp).
    QQuickStyle.setStyle("Basic")
    app = QGuiApplication(sys.argv)
    work = Path(tempfile.mkdtemp(prefix="fet-capture-"))
    harness.stage_module(work)
    harness.register_branding(work)

    model = harness.SampleWidgetModel()
    recording = realdata.load()
    context = harness.real_context(recording, recording.best_lap()["start"] + MOMENT_IN_BEST_LAP)

    def make_view(source):
        view = QQuickView()
        view.engine().addImportPath(str(work))
        view.setInitialProperties({"renderContext": context, "widgetModel": model})
        view.setResizeMode(QQuickView.SizeRootObjectToView)
        view.resize(WIDTH, HEIGHT)
        if isinstance(source, Path):
            view.setSource(QUrl.fromLocalFile(str(source)))
        else:
            view.loadFromModule("FlappedEar", source)
        if view.status() != QQuickView.Ready:
            for error in view.errors():
                print(error.toString())
            raise SystemExit(f"Could not load {source}")
        view.show()
        return view

    def render(view, widgets):
        model.set_widgets(widgets)
        # Canvas-based widgets paint asynchronously; let them settle.
        for _ in range(3):
            deadline = time.monotonic() + 0.25
            while time.monotonic() < deadline:
                app.processEvents()
                time.sleep(0.02)
            context.advance()
        return view.grabWindow()

    stage = make_view(Path(__file__).resolve().parent / "qml" / "WidgetStage.qml")

    for widget_type, name, geometry, settings in GALLERY:
        width, height = harness.DEFAULT_SIZES.get(widget_type, (0.15, 0.16))
        width = geometry.get("width", width)
        height = geometry.get("height", height)
        widget = harness.make_widget(widget_type, x=0.5 - width / 2, y=0.62 - height / 2,
                                     width=width, height=height, settings=settings)
        image = render(stage, [widget])
        margin = 28
        rect = QRect(int(widget["x"] * WIDTH) - margin, int(widget["y"] * HEIGHT) - margin,
                     int(width * WIDTH) + 2 * margin, int(height * HEIGHT) + 2 * margin)
        image.copy(rect).save(str(OUT / f"{name}.png"))
        print("wrote", name)

    stage.close()
    backdrop = make_view("VisualSmokeScene")
    for template in harness.CATALOG["templates"]:
        image = render(backdrop, harness.template_widgets(template["id"]))
        image.scaledToWidth(1280).save(str(OUT / f"template-{template['id']}.png"))
        print("wrote template", template["id"])


if __name__ == "__main__":
    main()
