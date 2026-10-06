#pragma once

#include "telemetry/ChannelSummary.h"
#include "telemetry/TelemetrySession.h"

#include <QHash>
#include <QString>

#include <array>
#include <optional>

namespace FlappedEar {

// KAN-132: tyre temperature and pressure per corner of the car, from the
// recording's own channels (for example RaceChrono's CAN channels
// `tyre_temp_fl-canbus` and `tyre_pressure_fl-canbus`). Nothing is
// substituted: a corner without a channel, a gap or a placeholder is no data.
//
// Temperatures are degrees Celsius unless the channel declares Fahrenheit.
// Pressures are converted to bar. When a channel does not declare its unit,
// the unit is classified from the median of its non-zero samples: 0.5-8 is
// bar, 8-100 psi, 100-1000 kPa. Anything else is not a tyre pressure, and the
// corner's pressure is unavailable.
//
// An exact zero is a placeholder in both: TPMS sensors report 0 until their
// first reading. A tyre at exactly 0 °C or 0 bar therefore shows as no data.
inline constexpr auto tyreDataAlgorithm = "tyre-data-v1";

enum class TyreCorner { FrontLeft = 0, FrontRight = 1, RearLeft = 2, RearRight = 3 };
inline constexpr int tyreCornerCount = 4;

enum class PressureUnit { Unknown, Bar, Psi, Kilopascal };

struct TyreChannelMap {
    std::array<QString, tyreCornerCount> temperature; // empty: not recorded
    std::array<QString, tyreCornerCount> pressure;
    std::array<bool, tyreCornerCount> temperatureFahrenheit{};
    std::array<PressureUnit, tyreCornerCount> pressureUnit{};

    [[nodiscard]] bool hasAny() const;
};

struct TyreReading {
    std::optional<double> temperatureCelsius;
    std::optional<double> pressureBar;
};

// "FL", "FR", "RL", "RR".
[[nodiscard]] QString tyreCornerCode(TyreCorner corner);
[[nodiscard]] QString pressureUnitName(PressureUnit unit); // "bar", "psi", "kPa" or ""

// The first channel per corner whose name is a tyre (or tire) temperature or
// pressure for that corner, and each pressure channel's unit.
[[nodiscard]] TyreChannelMap mapTyreChannels(const TelemetrySession &session);

// KAN-203: the widget's own choice of channel per corner. An empty name keeps
// the automatic channel; any other name replaces it, and a name the recording
// lacks gives that corner no data. `pressureUnits` caches the classification
// per channel name, so a caller that keeps it across frames classifies once.
[[nodiscard]] TyreChannelMap withTyreChannelChoices(const TelemetrySession &session, TyreChannelMap map,
    const std::array<QString, tyreCornerCount> &temperature, const std::array<QString, tyreCornerCount> &pressure,
    QHash<QString, PressureUnit> *pressureUnits = nullptr);

// A declared unit wins; otherwise the magnitude decides (see above).
[[nodiscard]] PressureUnit classifyPressureUnit(const TelemetryChannel &channel);

// Zero is a placeholder; plausible temperature -40..250 °C, pressure above 0
// and at most 10 bar (in the channel's unit).
[[nodiscard]] ChannelSummaryPolicy tyreTemperaturePolicy();
[[nodiscard]] ChannelSummaryPolicy tyrePressurePolicy(PressureUnit unit);

// One corner at telemetry `time`, interpolated between neighbouring valid
// samples, never across a gap or a placeholder.
[[nodiscard]] TyreReading tyreReadingAt(const TelemetrySession &session, const TyreChannelMap &map,
    TyreCorner corner, double time);

} // namespace FlappedEar
