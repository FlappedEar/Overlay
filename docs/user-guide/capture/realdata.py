"""Read a real RaceChrono Pro 10.2.4 VBO recording for user-guide screenshots.

The recording is private and is never committed. Pass its path with the
FET_CAPTURE_VBO environment variable. Lap detection mirrors the application's
rules closely (Start gate from [laptiming] as centre + backward vector, one
crossing direction, 1 s refractory period); it is a capture aid, not the
application's parser.
"""

from __future__ import annotations

import bisect
import math
import os
from dataclasses import dataclass, field
from pathlib import Path

EARTH = 6_371_000.0


def clock_seconds(text: str) -> float:
    value = float(text)
    hours, rest = divmod(value, 10000)
    minutes, seconds = divmod(rest, 100)
    return hours * 3600 + minutes * 60 + seconds


@dataclass
class Recording:
    name: str
    columns: list[str]
    times: list[float]
    channels: dict[str, list[float]]
    lat: list[float]
    lon: list[float]
    gate: tuple[tuple[float, float], tuple[float, float]]
    raw_columns: list[list[float]] = field(default_factory=list)
    crossings: list[float] = field(default_factory=list)
    laps: list[dict] = field(default_factory=list)

    # --- geometry -------------------------------------------------------
    def local(self, lat: float, lon: float, origin=None) -> tuple[float, float]:
        olat, olon = origin or (self.lat[0], self.lon[0])
        east = math.radians(lon - olon) * EARTH * math.cos(math.radians(olat))
        north = math.radians(lat - olat) * EARTH
        return east, north

    def track_points(self, start: float | None = None, end: float | None = None, step: int = 8) -> list[dict]:
        """Normalized outline like TrackGeometry: x east, y south, uniform scale, centred at 0.5."""
        indices = range(0, len(self.times), step)
        if start is not None:
            indices = [i for i in indices if start <= self.times[i] <= end]
        pts = [self.local(self.lat[i], self.lon[i]) for i in indices]
        pts = [(x, -y) for x, y in pts]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        scale = max(1.0, max(max(xs) - min(xs), max(ys) - min(ys)))
        self._norm = (cx, cy, scale)
        return [{"x": (x - cx) / scale + 0.5, "y": (y - cy) / scale + 0.5} for x, y in pts]

    def track_point_at(self, t: float) -> dict:
        cx, cy, scale = self._norm
        i = self.index_at(t)
        x, y = self.local(self.lat[i], self.lon[i])
        return {"x": (x - cx) / scale + 0.5, "y": (-y - cy) / scale + 0.5}

    # --- values ---------------------------------------------------------
    def index_at(self, t: float) -> int:
        i = bisect.bisect_left(self.times, t)
        return max(0, min(len(self.times) - 1, i))

    def value(self, column: str, t: float) -> float | None:
        values = self.channels.get(column)
        if values is None:
            return None
        i = self.index_at(t)
        if i == 0:
            return values[0]
        t0, t1 = self.times[i - 1], self.times[i]
        a, b = values[i - 1], values[i]
        f = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
        return a + (b - a) * f

    def app_channels(self) -> dict[str, int]:
        """Channel name -> column index as VboParser presents them: duplicate names get " (n)",
        the time column and columns without any number are dropped, names sorted case-insensitively."""
        counts: dict[str, int] = {}
        named = []
        for column_index, column in enumerate(self.columns):
            counts[column] = counts.get(column, 0) + 1
            named.append((column if counts[column] == 1 else f"{column} ({counts[column]})", column_index))
        result = {}
        for name, column_index in named:
            if self.columns[column_index] == "time":
                continue
            values = self.raw_columns[column_index]
            if any(math.isfinite(v) for v in values):
                result[name] = column_index
        return dict(sorted(result.items(), key=lambda item: item[0].lower()))

    def app_value(self, name: str, t: float) -> float | None:
        """Value as the application stores it: coordinates in degrees (RaceChrono longitude sign kept)."""
        column_index = self.app_channels().get(name)
        if column_index is None:
            return None
        values = self.raw_columns[column_index]
        i = self.index_at(t)
        value = values[i] if i == 0 else values[i - 1] + (values[i] - values[i - 1]) * (
            (t - self.times[i - 1]) / (self.times[i] - self.times[i - 1]))
        if self.columns[column_index] in ("lat", "long"):
            value /= 60.0
        return value

    def lap_at(self, t: float) -> dict | None:
        for lap in self.laps:
            if lap["start"] <= t < lap["end"]:
                return lap
        return None

    def best_lap(self) -> dict:
        return min(self.laps, key=lambda lap: lap["durationSeconds"])

    def lap_distance(self, lap: dict) -> tuple[list[float], list[float]]:
        """Cumulative distance (m) and elapsed time (s) samples through one lap."""
        dist, times = [0.0], [0.0]
        i0, i1 = self.index_at(lap["start"]), self.index_at(lap["end"])
        prev = self.local(self.lat[i0], self.lon[i0])
        for i in range(i0 + 1, i1 + 1):
            cur = self.local(self.lat[i], self.lon[i])
            dist.append(dist[-1] + math.dist(prev, cur))
            times.append(self.times[i] - lap["start"])
            prev = cur
        return dist, times

    def reference_time_at_position(self, reference: dict, t: float) -> tuple[float, float] | None:
        """Mirror closestReferenceMatch (TelemetryRenderContext.cpp): project the current position onto
        the reference lap's GPS segments within +/- clamp(18 % of its duration, 12 s, 30 s) of the
        current elapsed time; accept a match within 50 m. Returns (reference elapsed, reference time)."""
        lap = self.lap_at(t)
        expected = min(max(t - lap["start"], 0.0), reference["durationSeconds"])
        window = min(max(reference["durationSeconds"] * 0.18, 12.0), 30.0)
        lo, hi = max(0.0, expected - window), min(reference["durationSeconds"], expected + window)
        here = self.local(self.lat[self.index_at(t)], self.lon[self.index_at(t)])
        best = None
        i0, i1 = self.index_at(reference["start"]), self.index_at(reference["end"])
        for i in range(i0 + 1, i1 + 1):
            e0, e1 = self.times[i - 1] - reference["start"], self.times[i] - reference["start"]
            if e1 < lo or e0 > hi:
                continue
            a = self.local(self.lat[i - 1], self.lon[i - 1])
            b = self.local(self.lat[i], self.lon[i])
            dx, dy = b[0] - a[0], b[1] - a[1]
            length2 = dx * dx + dy * dy
            if length2 <= 1e-12:
                continue
            f = min(max(((here[0] - a[0]) * dx + (here[1] - a[1]) * dy) / length2, 0.0), 1.0)
            d2 = (here[0] - a[0] - dx * f) ** 2 + (here[1] - a[1] - dy * f) ** 2
            if best is None or d2 < best[0]:
                best = (d2, e0 + (e1 - e0) * f)
        if best is None or best[0] > 50.0 ** 2:
            return None
        return best[1], reference["start"] + best[1]

def load(path: str | os.PathLike | None = None) -> Recording:
    path = Path(path or os.environ["FET_CAPTURE_VBO"])
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    section, columns, rows, name, gate_values, created = None, [], [], "", None, None
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1]
            continue
        if not stripped:
            continue
        if stripped.startswith("File created on "):
            created = stripped[len("File created on "):].split(" at ")[0]
        if section == "column names":
            columns = stripped.split()
        elif section == "session data" and stripped.startswith("name "):
            name = stripped[5:]
        elif section == "laptiming" and stripped.lower().startswith("start"):
            gate_values = [float(v) for v in stripped.split()[1:5]]
        elif section == "data":
            rows.append(stripped.split())

    index = {}
    for i, column in enumerate(columns):
        index.setdefault(column, i)  # duplicate names: first wins, as in the application
    times, lat, lon = [], [], []
    channels: dict[str, list[float]] = {c: [] for c in index}
    raw_columns: list[list[float]] = [[] for _ in columns]
    first = None
    for row in rows:
        if len(row) != len(columns):
            continue
        t = clock_seconds(row[index["time"]])
        first = t if first is None else first
        t -= first
        if times and t <= times[-1]:
            continue
        times.append(t)
        # RaceChrono Pro 10.2.4: signed total arc-minutes, longitude west-positive.
        lat.append(float(row[index["lat"]]) / 60.0)
        lon.append(-float(row[index["long"]]) / 60.0)
        for i, cell in enumerate(row):
            try:
                raw_columns[i].append(float(cell))
            except ValueError:
                raw_columns[i].append(float("nan"))
        for column, i in index.items():
            channels[column].append(raw_columns[i][-1])

    # Gate: centre + backward vector (both in arc-minutes, longitude first).
    c_lon, c_lat, b_lon, b_lat = gate_values
    centre = (c_lat / 60.0, -c_lon / 60.0)
    back = (b_lat / 60.0, -b_lon / 60.0)
    rec = Recording(name, columns, times, channels, lat, lon, ((0, 0), (0, 0)), raw_columns)
    rec.created_date = created
    rec.first_clock = first
    ve, vn = rec.local(back[0], back[1], origin=centre)
    width = math.hypot(ve, vn)
    m_per_deg = EARTH * math.pi / 180
    d_lat = ve * 0.5 / m_per_deg
    d_lon = -vn * 0.5 / (m_per_deg * math.cos(math.radians(centre[0])))
    rec.gate = ((centre[0] - d_lat, centre[1] - d_lon), (centre[0] + d_lat, centre[1] + d_lon))
    _detect_laps(rec, width)
    return rec


def _detect_laps(rec: Recording, _width: float) -> None:
    origin = ((rec.gate[0][0] + rec.gate[1][0]) / 2, (rec.gate[0][1] + rec.gate[1][1]) / 2)
    ga = rec.local(*rec.gate[0], origin=origin)
    gb = rec.local(*rec.gate[1], origin=origin)
    g = (gb[0] - ga[0], gb[1] - ga[1])
    normal = (-g[1], g[0])
    accepted_direction = 0
    last = -1e9
    prev = rec.local(rec.lat[0], rec.lon[0], origin=origin)
    for i in range(1, len(rec.times)):
        cur = rec.local(rec.lat[i], rec.lon[i], origin=origin)
        v = (cur[0] - prev[0], cur[1] - prev[1])
        denom = v[0] * g[1] - v[1] * g[0]
        if abs(denom) > 1e-12:
            off = (ga[0] - prev[0], ga[1] - prev[1])
            s = (off[0] * g[1] - off[1] * g[0]) / denom
            u = (off[0] * v[1] - off[1] * v[0]) / denom
            if 0 <= s <= 1 and 0 <= u <= 1:
                t = rec.times[i - 1] + s * (rec.times[i] - rec.times[i - 1])
                direction = 1 if v[0] * normal[0] + v[1] * normal[1] > 0 else -1
                speed = math.hypot(*v) / max(1e-9, rec.times[i] - rec.times[i - 1])
                if speed >= 2.0 and t - last >= 1.0 and (accepted_direction in (0, direction)):
                    accepted_direction = direction
                    rec.crossings.append(t)
                    last = t
        prev = cur
    for n, (a, b) in enumerate(zip(rec.crossings, rec.crossings[1:]), start=1):
        rec.laps.append({"number": n, "start": a, "end": b, "durationSeconds": b - a})
    best = min(lap["durationSeconds"] for lap in rec.laps) if rec.laps else None
    for lap in rec.laps:
        lap["isBest"] = lap["durationSeconds"] == best
        lap["hasDelta"] = True
        lap["deltaToBestSeconds"] = lap["durationSeconds"] - best
        lap["referenceEligible"] = True


def utc_text(rec: Recording, t: float) -> str:
    """yyyy-MM-dd HH:mm:ss.zzz from the RaceChrono creation date and the sample clock."""
    day, month, year = rec.created_date.split("/")
    seconds = rec.first_clock + t
    h, rest = divmod(seconds, 3600)
    m, s = divmod(rest, 60)
    return f"{year}-{month}-{day} {int(h):02d}:{int(m):02d}:{s:06.3f}"


def sections(rec: Recording) -> list[dict]:
    """OUT, LAP n and IN sections as OutingLaps derives them from accepted crossings."""
    result = []
    if not rec.crossings:
        return [{"type": "UNKNOWN", "lapNumber": 0, "start": rec.times[0], "end": rec.times[-1]}]
    result.append({"type": "OUT", "lapNumber": 0, "start": rec.times[0], "end": rec.crossings[0]})
    for lap in rec.laps:
        result.append({"type": "LAP", "lapNumber": lap["number"], "start": lap["start"], "end": lap["end"]})
    result.append({"type": "IN", "lapNumber": 0, "start": rec.crossings[-1], "end": rec.times[-1]})
    for section in result:
        section["durationSeconds"] = section["end"] - section["start"]
    return result


def lap_points(rec: Recording, lap: dict) -> list[tuple[float, float]]:
    return [rec.local(rec.lat[i], rec.lon[i]) for i in range(rec.index_at(lap["start"]), rec.index_at(lap["end"]) + 1)]


def direction(rec: Recording, lap: dict) -> str:
    """TrackInference: shoelace area over east/north points; negative area is clockwise."""
    pts = lap_points(rec, lap)
    pts.append(pts[0])
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:]))
    return "clockwise" if area < 0 else "counterclockwise"


def line_deviations(rec: Recording, cap: float = 16.0) -> dict[int, float]:
    """TrackInference::lapLineDeviations: worst distance from each lap's points to any other lap's path."""
    if len(rec.laps) < 3:
        return {}
    paths = {lap["number"]: lap_points(rec, lap) for lap in rec.laps}
    cell = 4.0
    grid: dict[tuple[int, int], list[tuple[int, int]]] = {}
    for number, path in paths.items():
        for i, (x, y) in enumerate(path):
            grid.setdefault((math.floor(x / cell), math.floor(y / cell)), []).append((number, i))
    reach = math.ceil(cap / cell)

    def to_segment(p, a, b):
        abx, aby = b[0] - a[0], b[1] - a[1]
        l2 = abx * abx + aby * aby
        t = 0.0 if l2 <= 0 else min(max(((p[0] - a[0]) * abx + (p[1] - a[1]) * aby) / l2, 0.0), 1.0)
        return math.hypot(p[0] - a[0] - abx * t, p[1] - a[1] - aby * t)

    result = {}
    for number, path in paths.items():
        worst = 0.0
        for p in path:
            cx, cy = math.floor(p[0] / cell), math.floor(p[1] / cell)
            nearest = cap
            for dx in range(-reach, reach + 1):
                for dy in range(-reach, reach + 1):
                    for other, i in grid.get((cx + dx, cy + dy), ()):
                        if other == number:
                            continue
                        q = paths[other]
                        if i + 1 < len(q):
                            nearest = min(nearest, to_segment(p, q[i], q[i + 1]))
                        if i > 0:
                            nearest = min(nearest, to_segment(p, q[i - 1], q[i]))
            worst = max(worst, nearest)
        result[number] = worst
    return result


def series(rec: Recording, name: str, start: float, end: float, maximum_points: int) -> dict:
    """AnalysisController::sessionSeries shape: segments of {x normalized, y}, min, max, unit."""
    column = rec.app_channels().get(name)
    if column is None:
        return {"reason": "channelMissing"}
    if not end > start:
        return {"reason": "invalidRange"}
    i0, i1 = rec.index_at(start), rec.index_at(end)
    values = rec.raw_columns[column]
    buckets = max(1, min(2000, maximum_points) // 2)
    span = end - start
    points = []
    per = max(1, (i1 - i0 + 1) // buckets)
    for b in range(i0, i1 + 1, per):
        chunk = [(rec.times[i], values[i]) for i in range(b, min(i1 + 1, b + per)) if math.isfinite(values[i])]
        if not chunk:
            continue
        lo = min(chunk, key=lambda item: item[1])
        hi = max(chunk, key=lambda item: item[1])
        for t, v in sorted({lo, hi}):
            points.append({"x": (t - start) / span, "y": v / 60.0 if rec.columns[column] in ("lat", "long") else v})
    if not points:
        return {}
    ys = [p["y"] for p in points]
    return {"segments": [points], "brakingUp": name == "longacc-calc", "minimum": min(ys), "maximum": max(ys),
            "unit": ""}


def normalized_track(rec: Recording, start: float, end: float):
    """Lap outline normalized like TrackGeometry (x east, y south, uniform scale, centred)."""
    idx = range(rec.index_at(start), rec.index_at(end) + 1)
    pts = [rec.local(rec.lat[i], rec.lon[i]) for i in idx]
    pts = [(x, -y) for x, y in pts]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    scale = max(1.0, max(max(xs) - min(xs), max(ys) - min(ys)))

    def norm(x, y):
        return {"x": (x - cx) / scale + 0.5, "y": (y - cy) / scale + 0.5}

    def point_at(t):
        i = rec.index_at(t)
        x, y = rec.local(rec.lat[i], rec.lon[i])
        return norm(x, -y)

    return [norm(x, y) for x, y in pts], point_at


if __name__ == "__main__":
    rec = load()
    print(rec.name, f"{len(rec.times)} samples, {rec.times[-1]:.1f} s")
    for lap in rec.laps:
        m, s = divmod(lap["durationSeconds"], 60)
        print(f"LAP {lap['number']}: {int(m)}:{s:06.3f}{'  best' if lap['isBest'] else ''}")
    print("direction", direction(rec, rec.best_lap()), "deviations", line_deviations(rec))
    print("start", utc_text(rec, 0.0))
