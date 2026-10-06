#include "telemetry/TyreData.h"

#include "telemetry/MapLayers.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace FlappedEar {
namespace {

std::optional<int> cornerIndex(const QString &token)
{
    if (token == QLatin1String("fl") || token == QLatin1String("lf")) return 0;
    if (token == QLatin1String("fr") || token == QLatin1String("rf")) return 1;
    if (token == QLatin1String("rl") || token == QLatin1String("lr")) return 2;
    if (token == QLatin1String("rr")) return 3;
    return std::nullopt;
}

bool declaresFahrenheit(const QString &unit)
{
    const QString normalized = unit.trimmed().toLower().remove(QChar(0x00B0));
    return normalized == QLatin1String("f") || normalized == QLatin1String("degf")
        || normalized == QLatin1String("fahrenheit");
}

PressureUnit declaredPressureUnit(const QString &unit)
{
    const QString normalized = unit.trimmed().toLower();
    if (normalized == QLatin1String("bar")) return PressureUnit::Bar;
    if (normalized == QLatin1String("psi")) return PressureUnit::Psi;
    if (normalized == QLatin1String("kpa")) return PressureUnit::Kilopascal;
    return PressureUnit::Unknown;
}

double barPerUnit(const PressureUnit unit)
{
    switch (unit) {
    case PressureUnit::Bar: return 1.0;
    case PressureUnit::Psi: return 0.0689475729;
    case PressureUnit::Kilopascal: return 0.01;
    case PressureUnit::Unknown: break;
    }
    return 0.0;
}

} // namespace

bool TyreChannelMap::hasAny() const
{
    for (int corner = 0; corner < tyreCornerCount; ++corner)
        if (!temperature[corner].isEmpty() || !pressure[corner].isEmpty()) return true;
    return false;
}

QString tyreCornerCode(const TyreCorner corner)
{
    switch (corner) {
    case TyreCorner::FrontLeft: return QStringLiteral("FL");
    case TyreCorner::FrontRight: return QStringLiteral("FR");
    case TyreCorner::RearLeft: return QStringLiteral("RL");
    case TyreCorner::RearRight: return QStringLiteral("RR");
    }
    return {};
}

QString pressureUnitName(const PressureUnit unit)
{
    switch (unit) {
    case PressureUnit::Bar: return QStringLiteral("bar");
    case PressureUnit::Psi: return QStringLiteral("psi");
    case PressureUnit::Kilopascal: return QStringLiteral("kPa");
    case PressureUnit::Unknown: break;
    }
    return {};
}

PressureUnit classifyPressureUnit(const TelemetryChannel &channel)
{
    if (const auto declared = declaredPressureUnit(channel.unit); declared != PressureUnit::Unknown) return declared;
    QVector<float> recorded;
    for (const float value : channel.values)
        if (std::isfinite(value) && value > 0.0f) recorded.append(value);
    if (recorded.isEmpty()) return PressureUnit::Unknown;
    std::nth_element(recorded.begin(), recorded.begin() + recorded.size() / 2, recorded.end());
    const double median = recorded[recorded.size() / 2];
    if (median >= 0.5 && median < 8.0) return PressureUnit::Bar;
    if (median >= 8.0 && median < 100.0) return PressureUnit::Psi;
    if (median >= 100.0 && median <= 1000.0) return PressureUnit::Kilopascal;
    return PressureUnit::Unknown;
}

TyreChannelMap mapTyreChannels(const TelemetrySession &session)
{
    static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
    TyreChannelMap map;
    auto names = session.channelNames();
    std::sort(names.begin(), names.end());
    for (const QString &name : names) {
        const QStringList tokens = name.toLower().split(separators, Qt::SkipEmptyParts);
        const bool tyre = tokens.contains(QLatin1String("tyre")) || tokens.contains(QLatin1String("tire"));
        const bool temperature = tokens.contains(QLatin1String("temp")) || tokens.contains(QLatin1String("temperature"));
        const bool pressure = tokens.contains(QLatin1String("pressure")) || tokens.contains(QLatin1String("press"));
        if (!tyre || temperature == pressure) continue;
        std::optional<int> corner;
        for (const QString &token : tokens) {
            const auto found = cornerIndex(token);
            if (!found) continue;
            if (corner && *corner != *found) { corner.reset(); break; }
            corner = found;
        }
        if (!corner) continue;
        const auto channel = session.channels.constFind(name);
        if (channel == session.channels.cend()) continue;
        if (temperature && map.temperature[*corner].isEmpty()) {
            map.temperature[*corner] = name;
            map.temperatureFahrenheit[*corner] = declaresFahrenheit(channel->unit);
        } else if (pressure && map.pressure[*corner].isEmpty()) {
            map.pressure[*corner] = name;
            map.pressureUnit[*corner] = classifyPressureUnit(*channel);
        }
    }
    return map;
}

TyreChannelMap withTyreChannelChoices(const TelemetrySession &session, TyreChannelMap map,
    const std::array<QString, tyreCornerCount> &temperature, const std::array<QString, tyreCornerCount> &pressure,
    QHash<QString, PressureUnit> *pressureUnits)
{
    for (int corner = 0; corner < tyreCornerCount; ++corner) {
        if (!temperature[corner].isEmpty()) {
            map.temperature[corner] = temperature[corner];
            const auto channel = session.channels.constFind(temperature[corner]);
            map.temperatureFahrenheit[corner] = channel != session.channels.cend() && declaresFahrenheit(channel->unit);
        }
        if (!pressure[corner].isEmpty()) {
            map.pressure[corner] = pressure[corner];
            if (pressureUnits && pressureUnits->contains(pressure[corner])) {
                map.pressureUnit[corner] = pressureUnits->value(pressure[corner]);
                continue;
            }
            const auto channel = session.channels.constFind(pressure[corner]);
            map.pressureUnit[corner] = channel != session.channels.cend() ? classifyPressureUnit(*channel) : PressureUnit::Unknown;
            if (pressureUnits) pressureUnits->insert(pressure[corner], map.pressureUnit[corner]);
        }
    }
    return map;
}

ChannelSummaryPolicy tyreTemperaturePolicy()
{
    ChannelSummaryPolicy policy = temperatureSummaryPolicy();
    policy.placeholderTypicalAbove = -std::numeric_limits<double>::infinity();
    return policy;
}

ChannelSummaryPolicy tyrePressurePolicy(const PressureUnit unit)
{
    ChannelSummaryPolicy policy;
    policy.minimumPlausible = 0.0;
    const double perUnit = barPerUnit(unit);
    policy.maximumPlausible = perUnit > 0.0 ? 10.0 / perUnit : -1.0;
    policy.zeroIsPlaceholder = true;
    policy.placeholderTypicalAbove = -std::numeric_limits<double>::infinity();
    return policy;
}

TyreReading tyreReadingAt(const TelemetrySession &session, const TyreChannelMap &map, const TyreCorner corner,
    const double time)
{
    TyreReading reading;
    const int index = static_cast<int>(corner);
    if (index < 0 || index >= tyreCornerCount || !std::isfinite(time)) return reading;
    if (const auto channel = session.channels.constFind(map.temperature[index]); channel != session.channels.cend()) {
        const auto value = plausibleChannelValue(*channel, time, tyreTemperaturePolicy(), true);
        if (value)
            reading.temperatureCelsius = map.temperatureFahrenheit[index] ? (*value - 32.0) * 5.0 / 9.0 : *value;
    }
    const PressureUnit unit = map.pressureUnit[index];
    if (const auto channel = session.channels.constFind(map.pressure[index]);
        unit != PressureUnit::Unknown && channel != session.channels.cend()) {
        const auto value = plausibleChannelValue(*channel, time, tyrePressurePolicy(unit), true);
        if (value) reading.pressureBar = *value * barPerUnit(unit);
    }
    return reading;
}

} // namespace FlappedEar
