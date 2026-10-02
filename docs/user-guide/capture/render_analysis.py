"""Capture the Lap Analysis window from the production QML with a real recording.

    FET_CAPTURE_VBO=/path/to/session.vbo python docs/user-guide/capture/render_analysis.py
"""

from __future__ import annotations

import os
import sys
import tempfile
import time
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import QObject, QUrl, Slot
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent
from PySide6.QtQuickControls2 import QQuickStyle

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness  # noqa: E402
import realdata  # noqa: E402

OUT = harness.REPO / "docs" / "user-guide" / "assets" / "screens"
HERE = Path(__file__).resolve().parent
MOMENT_IN_BEST_LAP = 27.0
RUN_ID, RUN_NAME = "run-1", "Session 1"  # The application names a single imported run "Session 1".


def settle(app, seconds=0.8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        app.processEvents()
        time.sleep(0.02)


class AnalysisData(QObject):
    """Series, values and map points computed from the recording for the open lap."""

    def __init__(self, recording, lap):
        super().__init__()
        self._rec = recording
        self._track, self._point_at = realdata.normalized_track(recording, lap["start"], lap["end"])

    def track(self):
        return [self._track]

    @Slot(str, float, float, int, result="QVariantMap")
    def series(self, channel, start, end, points):
        return realdata.series(self._rec, channel, start, end, points)

    @Slot(str, float, result=str)
    def valueText(self, channel, t):
        value = self._rec.app_value(channel, t)
        return "—" if value is None else f"{value:.2f}"

    @Slot(float, result="QVariantMap")
    def trackPointAt(self, t):
        return self._point_at(t)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    QQuickStyle.setStyle("Basic")
    app = QGuiApplication(sys.argv)
    app.setApplicationDisplayName("Flapped Ear Telemetry")
    work = Path(tempfile.mkdtemp(prefix="fet-analysis-"))
    harness.stage_module(work)
    harness.register_branding(work)

    rec = realdata.load()
    best = rec.best_lap()
    direction = realdata.direction(rec, best)
    deviations = realdata.line_deviations(rec)
    group_label = f"Group 1 · Detected route · {'Clockwise' if direction == 'clockwise' else 'Counterclockwise'}"
    rows = []
    for index, section in enumerate(realdata.sections(rec)):
        is_lap = section["type"] == "LAP"
        eligible = is_lap and deviations.get(section["lapNumber"], 0.0) <= 12.0
        rows.append({
            "type": section["type"], "lapNumber": section["lapNumber"], "runId": RUN_ID, "runName": RUN_NAME,
            "clock": realdata.utc_text(rec, section["start"]), "durationSeconds": section["durationSeconds"],
            "startTime": section["start"], "endTime": section["end"],
            "reference": {"runId": RUN_ID, "sectionIndex": index}, "compatibilityResolved": is_lap,
            "compatibilityGroupLabel": group_label if is_lap else "", "comparisonEligible": eligible,
            "layoutIssue": is_lap and not eligible, "referenceIssue": "", "excluded": False,
            "bestOfDay": is_lap and section["lapNumber"] == best["number"], "bestOfRun": False,
        })
    eligible_count = sum(1 for row in rows if row["comparisonEligible"])
    lap_count = sum(1 for row in rows if row["type"] == "LAP")
    best_of_day = {"durationSeconds": best["durationSeconds"], "runName": RUN_NAME, "lapNumber": best["number"],
                   "reference": {"runId": RUN_ID}}
    channels = list(rec.app_channels())
    lap_row = next(row for row in rows if row["type"] == "LAP" and row["lapNumber"] == best["number"])
    data = AnalysisData(rec, best)

    engine = QQmlApplicationEngine()
    engine.addImportPath(str(work))
    mock = QQmlComponent(engine, QUrl.fromLocalFile(str(HERE / "qml" / "MockAnalysisController.qml")))
    controller = mock.createWithInitialProperties({"data": data})
    if controller is None:
        raise SystemExit(mock.errorString())
    engine.rootContext().setContextProperty("appController", controller)
    component = QQmlComponent(engine)
    component.loadFromModule("FlappedEar", "AnalysisWindow")
    window = component.createWithInitialProperties({"videoSource": QUrl(), "playbackPosition": 0.0,
                                                    "playbackRunning": False, "mediaDuration": 0.0})
    if window is None:
        raise SystemExit(component.errorString())
    window.resize(1440, 900)
    window.show()
    settle(app, 1.5)

    def shot(name):
        settle(app, 1.0)
        window.grabWindow().save(str(OUT / f"{name}.png"))
        print("wrote", name)

    shot("analysis-start")

    for key, value in {
        "eventName": rec.name, "eventRuns": [{"id": RUN_ID, "name": RUN_NAME}], "activeRunId": RUN_ID,
        "sampleCount": len(rec.times), "telemetryDuration": rec.times[-1], "channelNames": channels,
        "outingLaps": rows,
        "outingAnalysisStatus": {"state": "ready", "message": f"1 of 1 runs available · {len(rows)} recorded sections",
                                 "runs": [{"runId": RUN_ID, "runName": RUN_NAME, "state": "ready",
                                           "message": f"{len(rows)} recorded sections available.",
                                           "sectionCount": len(rows)}],
                                 "notices": [], "sectionCount": len(rows)},
        "outingCompatibilityGroups": [{"id": "group-1", "resolved": True, "available": True, "label": group_label,
                                       "summary": f"{group_label} · {eligible_count}/{lap_count} eligible laps",
                                       "ranking": {"bestOfDay": best_of_day}}],
        "outingComparisonGroupId": "group-1",
        "outingRanking": {"state": "available", "bestOfDay": best_of_day, "groupLabel": group_label},
    }.items():
        controller.setProperty(key, value)
    shot("analysis-day-results")

    for key, value in {
        "outingLapAvailableChannels": channels,
        "outingLapChannels": ["velocity", "latacc-calc", "longacc-calc"],
        "outingLapTrack": data.track(),
        "outingLapCursor": best["start"] + MOMENT_IN_BEST_LAP,
        "outingLapDetailState": "ready",
        "selectedOutingLap": lap_row,
    }.items():
        controller.setProperty(key, value)
    shot("analysis-lap-detail")


if __name__ == "__main__":
    main()
