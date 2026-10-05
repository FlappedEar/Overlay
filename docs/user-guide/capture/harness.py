"""Shared helpers for rendering the application's own QML with sample data.

The user-guide screenshots are produced by loading the production QML files
from native/qml with PySide6 (Qt 6.8). The C++ controllers are replaced by the
small objects in this module, filled from a real recording (see realdata.py).
Nothing here is used by the application itself.
"""

from __future__ import annotations

import json
import math
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

from PySide6.QtCore import (QAbstractListModel, QModelIndex, QObject, Property, QResource,
                            Qt, Signal, Slot)

REPO = Path(__file__).resolve().parents[3]
NATIVE = REPO / "native"
CATALOG = json.loads((NATIVE / "resources" / "widget-templates.json").read_text(encoding="utf-8"))

DEFAULT_SIZES = {
    "heartRate": (0.12, 0.13), "pedals": (0.25, 0.13), "f1GForceRadar": (0.15, 0.20),
    "gForceMagnitudeBar": (0.24, 0.10), "retroCustomValue": (0.20, 0.13), "lapCurrent": (0.17, 0.14),
    "retroTachometer": (0.25, 0.36), "tyres": (0.17, 0.20), "designed": (0.20, 0.13),
}


def stage_module(target: Path) -> Path:
    """Copy native/qml into an importable "FlappedEar" module directory."""
    module = target / "FlappedEar"
    if module.exists():
        shutil.rmtree(module)
    shutil.copytree(NATIVE / "qml", module)
    lines = ["module FlappedEar"]
    for path in sorted(module.rglob("*.qml")):
        lines.append(f"{path.stem} 1.0 {path.relative_to(module).as_posix()}")
    (module / "qmldir").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return target


def register_branding(work: Path) -> None:
    """Expose the logo at the same qrc path the application uses."""
    qrc = work / "branding.qrc"
    logo = NATIVE / "resources" / "branding" / "app-logo.png"
    qrc.write_text(
        '<RCC><qresource prefix="/flappedear">'
        f'<file alias="resources/branding/app-logo.png">{logo}</file>'
        "</qresource></RCC>", encoding="utf-8")
    rcc_binary = work / "branding.rcc"
    rcc = Path(sys.executable).with_name("pyside6-rcc")
    subprocess.run([str(rcc), "--binary", str(qrc), "-o", str(rcc_binary)], check=True)
    QResource.registerResource(str(rcc_binary))
    # The editor's bundled fonts (docs/ui-theme.md), as the application registers them.
    from PySide6.QtGui import QFontDatabase
    for font in sorted((NATIVE / "resources" / "fonts").glob("*.ttf")):
        QFontDatabase.addApplicationFont(str(font))


def default_settings(widget_type: str) -> dict:
    defaults = CATALOG["widgetDefaults"]
    result = dict(defaults["common"])
    result.update(defaults.get(widget_type, {}))
    result["name"] = widget_type
    return result


def make_widget(widget_type: str, index: int = 0, **geometry) -> dict:
    width, height = DEFAULT_SIZES.get(widget_type, (0.15, 0.16))
    settings = default_settings(widget_type)
    settings.update(geometry.pop("settings", {}))
    widget = {
        "id": str(uuid.uuid4()), "type": widget_type,
        "x": 0.04 + (index % 3) * 0.20, "y": 0.05 + (index // 3) * 0.22,
        "width": width, "height": height, "scale": 1.0, "rotation": 0.0, "opacity": 1.0,
        "visible": True, "settings": settings, "cues": [], "groupId": "",
    }
    widget.update(geometry)
    return widget


def template_widgets(template_id: str) -> list[dict]:
    template = next(t for t in CATALOG["templates"] if t["id"] == template_id)
    widgets = []
    for index, item in enumerate(template["widgets"]):
        geometry = {key: item[key] for key in ("x", "y", "width", "height", "scale", "rotation", "opacity")
                    if key in item}
        widget = make_widget(item["type"], index, settings=item.get("settings", {}), **geometry)
        widget["visible"] = item.get("visible", True)
        widget["cues"] = item.get("cues", [])
        widget["groupId"] = item.get("groupId", "")
        widgets.append(widget)
    return widgets


ROLES = ["widgetId", "widgetType", "widgetX", "widgetY", "widgetWidth", "widgetHeight", "widgetScale",
         "widgetRotation", "widgetOpacity", "widgetVisible", "widgetSettings", "widgetCues", "widgetGroupId"]
KEYS = ["id", "type", "x", "y", "width", "height", "scale", "rotation", "opacity", "visible", "settings",
        "cues", "groupId"]


class SampleWidgetModel(QAbstractListModel):
    """Read-only stand-in for WidgetModel with the same roles and properties."""

    countChanged = Signal()
    revisionChanged = Signal()
    templatesChanged = Signal()
    lastErrorChanged = Signal()

    def __init__(self, widgets: list[dict] | None = None, custom_templates: list[dict] | None = None):
        super().__init__()
        self._widgets = widgets or []
        self._custom = custom_templates or []

    def set_widgets(self, widgets: list[dict]) -> None:
        self.beginResetModel()
        self._widgets = widgets
        self.endResetModel()
        self.countChanged.emit()
        self.revisionChanged.emit()

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self._widgets)

    def roleNames(self):
        return {Qt.UserRole + 1 + i: name.encode() for i, name in enumerate(ROLES)}

    def data(self, index, role):
        if not index.isValid():
            return None
        offset = role - Qt.UserRole - 1
        if 0 <= offset < len(KEYS):
            return self._widgets[index.row()][KEYS[offset]]
        return None

    @Property(int, notify=countChanged)
    def count(self):
        return len(self._widgets)

    @Property(int, notify=revisionChanged)
    def revision(self):
        return 1

    @Property("QVariantList", notify=templatesChanged)
    def templates(self):
        result = [{"id": t["id"], "name": t["name"], "description": t["description"],
                   "widgetCount": len(t["widgets"]), "builtIn": True} for t in CATALOG["templates"]]
        result += [dict(t, builtIn=False) for t in self._custom]
        return result

    @Property(str, notify=lastErrorChanged)
    def lastError(self):
        return ""

    @Slot(int, result="QVariantMap")
    def widget(self, index):
        return dict(self._widgets[index]) if 0 <= index < len(self._widgets) else {}

    @Slot(int, result="QVariantList")
    def groupMembers(self, index):
        return [index]

    # Editing calls are accepted and ignored: screenshots never edit.
    @Slot(str, result=int)
    def addWidget(self, _type):
        return -1

    @Slot(int, str, "QVariant")
    def setSetting(self, *_):
        pass

    @Slot(int, str, "QVariant")
    def setWidgetProperty(self, *_):
        pass

    @Slot(int, float, float)
    def moveWidget(self, *_):
        pass

    @Slot(int, float, float)
    def resizeWidget(self, *_):
        pass


class SampleRenderContext(QObject):
    """Stand-in for TelemetryRenderContext. Built from a real recording by real_context()."""

    timeChanged = Signal()
    trackGeometryChanged = Signal()

    def __init__(self, values: dict | None = None, points: list | None = None, current: dict | None = None,
                 lap_timing: dict | None = None, fixed_lap: dict | None = None, tyres: dict | None = None,
                 time: float = 0.0):
        super().__init__()
        self._values = dict(values or {})
        self._points = points or []
        self._current = current or {}
        self._lap = lap_timing or {"available": False}
        self._fixed = fixed_lap or {}
        self._tyres = tyres or {"available": False, "corners": []}
        self._time = time
        self._jitter = 0.0

    def advance(self) -> None:
        """Mimic a playback tick so time-driven bindings and canvases refresh."""
        self._jitter = 1e-6 if self._jitter == 0.0 else 0.0
        self.timeChanged.emit()

    @Property(float, notify=timeChanged)
    def time(self):
        return self._time

    @Property("QVariantList", notify=trackGeometryChanged)
    def trackPoints(self):
        return self._points

    @Property(int, notify=trackGeometryChanged)
    def trackRevision(self):
        return 1

    @Property("QVariantMap", notify=timeChanged)
    def currentTrackPoint(self):
        return self._current

    @Property("QVariantMap", notify=timeChanged)
    def lapTiming(self):
        return self._lap

    @Slot(str, result="QVariant")
    def telemetryValue(self, source):
        value = self._values.get(source)
        return None if value is None else value + self._jitter

    @Slot(int, result="QVariantMap")
    def fixedLapTiming(self, _lap):
        return self._fixed

    @Slot(result="QVariantMap")
    def tyreValues(self):
        return self._tyres


# Application channel aliases for a RaceChrono Pro 10.2.4 VBO (see VboParser.cpp and
# TelemetrySession.cpp: an accelerator-position channel with data becomes throttle).
ALIASES = {"speed": "velocity", "rpm": "rpm-obd", "throttle": "accelerator_pos-obd", "brake": "brake_pos-obd",
           "heartRate": "heart_rate-hrm", "lateralAcceleration": "latacc-calc",
           "longitudinalAcceleration": "longacc-calc"}


def real_context(rec, t: float) -> SampleRenderContext:
    """Render context for time t of a real recording (realdata.Recording)."""
    values = {}
    for column in rec.channels:
        value = rec.value(column, t)
        if value is not None and math.isfinite(value):
            values[column] = value
    for alias, column in ALIASES.items():
        if column in values:
            values[alias] = values[column]
    points = rec.track_points()
    current = rec.track_point_at(t)
    lap = rec.lap_at(t)
    timing = {"available": bool(rec.laps), "state": "waiting"}
    if lap is not None:
        completed = [l for l in rec.laps if l["end"] <= t]
        timing = {"available": True, "state": "running", "currentLapNumber": lap["number"],
                  "currentElapsedSeconds": t - lap["start"], "currentSpeedKmh": values.get("speed")}
        if completed:
            best = min(completed, key=lambda l: l["durationSeconds"])
            last = completed[-1]
            timing.update({"bestLapNumber": best["number"], "bestLapSeconds": best["durationSeconds"],
                           "lastLapNumber": last["number"], "lastLapSeconds": last["durationSeconds"]})
            matched = rec.reference_time_at_position(best, t)
            if matched is not None:
                ref_elapsed, ref_time = matched
                timing["liveDeltaSeconds"] = (t - lap["start"]) - ref_elapsed
                ref_speed = rec.value("velocity", ref_time)
                timing["referenceSpeedKmh"] = ref_speed
                if values.get("speed") is not None:
                    timing["speedDeltaKmh"] = values["speed"] - ref_speed
    best_lap = rec.best_lap()
    fixed = {"lapNumber": best_lap["number"], "durationSeconds": best_lap["durationSeconds"], "isBest": True}
    if t < best_lap["start"]:
        fixed.update({"state": "before", "elapsedSeconds": 0.0})
    elif t < best_lap["end"]:
        fixed.update({"state": "running", "elapsedSeconds": t - best_lap["start"]})
    else:
        fixed.update({"state": "finished", "elapsedSeconds": best_lap["durationSeconds"]})
    return SampleRenderContext(values, points, current, timing, fixed, None, t)
