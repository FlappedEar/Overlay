// KAN-97: analytical map layers for the A/B pair. One lap's racing line on
// the shared map is coloured by speed, the A-B time delta, lateral or
// longitudinal G, throttle, the measured brake or a recorded temperature.
// Values come from the recording (never inferred: no brake channel means no
// brake layer) and are sampled along the shared progress axis, so gaps in
// GPS, progress or the channel stay open (telemetry/MapLayers).

#include "app/AnalysisController.h"
#include "telemetry/ChannelSummary.h"
#include "telemetry/MapLayers.h"

using namespace FlappedEar;

namespace {

constexpr int mapLayerPoints = 800;
const QString temperaturePrefix = QStringLiteral("temperature:");

struct MapLayerSpec {
    QString id;
    QString label;
    QString alias;          // channel alias, or the channel itself for temperatures
    QString scale;          // "sequential" or "diverging" (signed, centred on zero)
    QString negativeLabel;  // what the negative end means, for diverging scales
    QString positiveLabel;
    bool temperature = false;
};

QVector<MapLayerSpec> fixedLayers()
{
    return {
        {"speed", QStringLiteral("Speed"), "speed", "sequential", {}, {}},
        {"delta", QStringLiteral("Δ time (A−B)"), {}, "diverging", QStringLiteral("A ahead"), QStringLiteral("A behind")},
        {"lateralG", QStringLiteral("Lateral G"), "lateralAcceleration", "diverging", QStringLiteral("−"), QStringLiteral("+")},
        {"longitudinalG", QStringLiteral("Longitudinal G"), "longitudinalAcceleration", "diverging",
            QStringLiteral("braking"), QStringLiteral("accelerating")},
        {"throttle", QStringLiteral("Throttle"), "throttle", "sequential", {}, {}},
        {"brake", QStringLiteral("Brake (measured)"), "brake", "sequential", {}, {}},
    };
}

const TelemetryChannel *layerChannel(const TelemetrySession &session, const MapLayerSpec &spec, QString *name)
{
    *name = spec.temperature ? spec.alias : session.aliases.value(spec.alias);
    const auto found = session.channels.constFind(*name);
    return name->isEmpty() || found == session.channels.cend() ? nullptr : &*found;
}

} // namespace

QVariantList AnalysisController::comparisonMapLayerOptions() const
{
    if (!comparisonPairReady()) return {};
    ensureComparisonProgressAxis();
    auto specs = fixedLayers();
    QStringList temperatures;
    for (const auto &slot : m_comparisonSlots)
        for (const auto &name : recordedTemperatureChannels(*slot.session))
            if (!temperatures.contains(name)) temperatures.append(name);
    std::sort(temperatures.begin(), temperatures.end());
    for (const auto &name : temperatures)
        specs.append({temperaturePrefix + name, name, name, "sequential", {}, {}, true});
    QVariantList options;
    for (const auto &spec : specs) {
        bool available = false;
        if (spec.id == QLatin1String("delta")) {
            available = m_comparisonProgressAxis.valid;
        } else {
            QString name;
            for (const auto &slot : m_comparisonSlots) available = available || layerChannel(*slot.session, spec, &name);
        }
        options.append(QVariantMap{{"id", spec.id}, {"label", spec.label}, {"available", available},
            {"temperature", spec.temperature}});
    }
    // No recorded temperature is shown as one unavailable entry, never invented.
    if (temperatures.isEmpty())
        options.append(QVariantMap{{"id", QStringLiteral("temperature")}, {"label", QStringLiteral("Temperature")},
            {"available", false}, {"temperature", true}});
    return options;
}

QVariantMap AnalysisController::comparisonMapLayer(const QString &layerId, const int slot) const
{
    if (slot < 0 || slot > 1 || !comparisonPairReady()) return {{"valid", false}, {"reason", "pairNotReady"}};
    MapLayerSpec spec;
    for (const auto &candidate : fixedLayers())
        if (candidate.id == layerId) spec = candidate;
    if (spec.id.isEmpty() && layerId.startsWith(temperaturePrefix)) {
        const auto name = layerId.mid(temperaturePrefix.size());
        spec = {layerId, name, name, "sequential", {}, {}, true};
    }
    if (spec.id.isEmpty()) return {{"valid", false}, {"reason", "unknownLayer"}};
    ensureComparisonProgressAxis();
    ensureComparisonSharedGeometry();
    QVariantMap result{{"id", spec.id}, {"label", spec.label}, {"scale", spec.scale}, {"slot", slot},
        {"negativeLabel", spec.negativeLabel}, {"positiveLabel", spec.positiveLabel},
        {"algorithm", QString::fromLatin1(mapLayerAlgorithm)}};
    if (!m_comparisonProgressAxis.valid || !m_comparisonSharedGeometry.valid) {
        result.insert("valid", false);
        result.insert("reason", "noProgressAxis");
        return result;
    }
    const auto &session = *m_comparisonSlots[slot].session;
    const auto &trace = m_comparisonProgressTraceCache[slot];
    const double length = m_comparisonProgressAxis.lengthMeters;
    ProgressValueSegments values;
    if (spec.id == QLatin1String("delta")) {
        // The pair's delta at each progress, drawn where the chosen lap was.
        for (const auto &segment : computeDeltaSeries(m_comparisonProgressTraceCache[0], m_comparisonProgressTraceCache[1],
                 length / mapLayerPoints, comparisonDeltaTiming())) {
            QVector<QPointF> points;
            for (const auto &point : segment) points.append({point.progressMeters, point.deltaSeconds});
            values.append(points);
        }
        result.insert("unit", QStringLiteral("s"));
        result.insert("provenance", QStringLiteral("calculated"));
    } else {
        QString name;
        const auto *channel = layerChannel(session, spec, &name);
        if (!channel) {
            result.insert("valid", false);
            result.insert("reason", "channelMissing");
            return result;
        }
        values = channelAlongProgress(session, name, trace, length, mapLayerPoints,
            spec.temperature ? temperatureSummaryPolicy() : ChannelSummaryPolicy{});
        result.insert("channel", name);
        result.insert("unit", channel->unit);
        result.insert("provenance", name.endsWith(QStringLiteral("-calc"), Qt::CaseInsensitive)
            ? QStringLiteral("calculated") : QStringLiteral("measured"));
    }
    const auto layer = placeOnMap(session, trace, m_comparisonSharedGeometry, values);
    if (layer.polylines.isEmpty()) {
        result.insert("valid", false);
        result.insert("reason", "noSamples");
        return result;
    }
    QVariantList polylines;
    for (const auto &polyline : layer.polylines) {
        QVariantList points, pointValues;
        for (const auto &point : polyline) {
            points.append(QPointF(point.x, point.y));
            pointValues.append(point.value);
        }
        polylines.append(QVariantMap{{"points", points}, {"values", pointValues}});
    }
    result.insert("valid", true);
    result.insert("polylines", polylines);
    result.insert("minimum", *layer.minimum);
    result.insert("maximum", *layer.maximum);
    return result;
}
