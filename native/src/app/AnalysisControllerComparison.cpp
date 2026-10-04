#include "app/AnalysisController.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectLimits.h"
#include "telemetry/TrackProgress.h"

#include <QJsonArray>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace FlappedEar;

namespace {
bool eligible(const QVariantMap &row)
{
    return row.value("type") == "LAP" && row.value("referenceEligible").toBool()
        && row.value("compatibilityResolved").toBool() && !row.value("excluded").toBool()
        && !row.value("compatibilityReasons").toStringList().contains("stale-source");
}
}

namespace {
// KAN-40: compatibility group, exclusions, GPS coverage and the reason a slot
// isn't usable, computed once here so QML need only read plain fields off
// comparisonSlots -- the same shape of read as the pre-existing lap-label
// binding, not a new JS function reactively re-entering comparisonSlots.
QString comparisonSlotStatusText(const int index, const QString &state, const QString &error, const QVariantMap &row)
{
    const QString prefix = index == 0 ? QStringLiteral("Lap A: ") : QStringLiteral("Lap B: ");
    if (state == "empty") return prefix + QObject::tr("No lap selected.");
    if (state == "loading") return prefix + QObject::tr("Loading recording…");
    if (state == "error")
        return prefix + (error.isEmpty() ? QObject::tr("This lap is unavailable.") : error);
    QStringList parts;
    const auto groupLabel = row.value("compatibilityGroupLabel").toString();
    parts << (groupLabel.isEmpty() ? QObject::tr("Unresolved track configuration") : groupLabel);
    const auto issue = row.value("referenceIssue").toString();
    if (!issue.isEmpty()) parts << issue;
    if (row.value("excluded").toBool()) {
        const auto reason = row.value("exclusionReason").toString();
        parts << QObject::tr("Excluded: %1").arg(reason.isEmpty() ? QObject::tr("no reason recorded") : reason);
    }
    parts += row.value("compatibilityReasonLabels").toStringList();
    return prefix + parts.join(QStringLiteral(" · "));
}

bool comparisonSlotStatusIsWarning(const QString &state, const QVariantMap &row)
{
    if (state == "error") return true;
    if (state != "ready") return false;
    return !row.value("referenceIssue").toString().isEmpty() || row.value("excluded").toBool()
        || !row.value("compatibilityReasonLabels").toStringList().isEmpty();
}
}

QVariantList AnalysisController::comparisonSlots() const
{
    QVariantList result;
    for (int i = 0; i < 2; ++i) {
        const auto &slot = m_comparisonSlots[i];
        result.append(QVariantMap{{"lap", slot.row}, {"state", slot.state}, {"error", slot.error},
            {"statusText", comparisonSlotStatusText(i, slot.state, slot.error, slot.row)},
            {"statusIsWarning", comparisonSlotStatusIsWarning(slot.state, slot.row)}});
    }
    return result;
}

QString AnalysisController::formatElapsedTime(const double seconds)
{
    return FlappedEar::formatElapsedTime(seconds);
}

QVariantList AnalysisController::comparisonLaps() const
{
    QVariantList result;
    if (outingLapsLoading()) return result;
    for (const auto &value : m_outingLapRows) {
        auto row = value.toMap();
        if (!eligible(row)) continue;
        row.insert("label", QStringLiteral("%1 · LAP %2 · %3").arg(row.value("runName").toString())
            .arg(row.value("lapNumber").toInt()).arg(formatElapsedTime(row.value("durationSeconds").toDouble())));
        result.append(row);
    }
    return result;
}

QStringList AnalysisController::comparisonAvailableChannels() const
{
    if (m_comparisonSlots[0].state != "ready" || m_comparisonSlots[1].state != "ready"
        || !m_comparisonSlots[0].session || !m_comparisonSlots[1].session) return {};
    const auto a = m_comparisonSlots[0].session->channelNames();
    const auto b = m_comparisonSlots[1].session->channelNames();
    QStringList shared;
    for (const auto &channel : a)
        if (b.contains(channel)) shared.append(channel);
    return shared;
}

bool AnalysisController::comparisonPairReady() const
{
    return !outingLapsLoading() && m_comparisonSlots[0].state == "ready" && m_comparisonSlots[1].state == "ready"
        && m_comparisonSlots[0].row.value("compatibilityGroupId") == m_comparisonSlots[1].row.value("compatibilityGroupId");
}

bool AnalysisController::selectComparisonLap(const int index, const QVariantMap &reference)
{
    if (index < 0 || index > 1 || m_document.recoveryPending()
        || m_document.destructiveActionPending()) return false;
    const auto resolved = resolveOutingLapReference(reference);
    if (resolved.value("state") != "resolved") return false;
    const auto row = m_outingLapRows[resolved.value("index").toInt()].toMap();
    if (!eligible(row)) return false;
    const auto &other = m_comparisonSlots[1 - index];
    if ((other.state == "ready" || other.state == "loading")
        && row.value("compatibilityGroupId") != other.row.value("compatibilityGroupId")) return false;
    QJsonObject source;
    for (const auto &value : outingLapSources())
        if (value.toObject().value("runId") == row.value("runId").toString()) source = value.toObject();
    if (source.isEmpty()) return false;
    resetComparisonSlot(index);
    auto &slot = m_comparisonSlots[index];
    slot.row = row;
    slot.source = source;
    slot.key = outingRunKey(row.value("runId").toString());
    slot.state = "loading";
    m_comparisonTimer.start();
    emit comparisonSlotsChanged();
    return true;
}

void AnalysisController::resetComparisonSlot(const int index)
{
    if (index < 0 || index > 1) return;
    if (m_comparisonPending && m_comparisonLoadingSlot == index && m_comparisonCancellation)
        m_comparisonCancellation->store(true);
    m_comparisonSlots[index] = {};
    m_comparisonSlots[index].request = ++m_comparisonRequest;
    emit comparisonSlotsChanged();
}

void AnalysisController::clearComparisonLap(const int index)
{
    if (index < 0 || index > 1) return;
    resetComparisonSlot(index);
    // Only an explicit user clear forgets the persisted selection; internal
    // resets (replacing a selection, a new document generation) must not, or
    // they would race the async load and wipe the reference before it is
    // re-saved, dirtying a document that was just opened/restored.
    persistComparisonSlot(index, QJsonValue(QJsonValue::Null));
}

void AnalysisController::persistComparisonSlot(const int index, const QJsonValue &reference)
{
    if (index < 0 || index > 1 || !m_document.isEventDocument()) return;
    auto project = m_document.analysisProject();
    auto event = project.value("event").toObject();
    auto decisions = event.value("analysisDecisions").toObject();
    auto savedComparisonSlots = decisions.value("comparisonSlots").toArray();
    while (savedComparisonSlots.size() < 2) savedComparisonSlots.append(QJsonValue(QJsonValue::Null));
    if (savedComparisonSlots[index] == reference) return;
    savedComparisonSlots[index] = reference;
    decisions.insert("comparisonSlots", savedComparisonSlots);
    event.insert("analysisDecisions", decisions);
    project.insert("event", event);
    if (!ProjectLimits::validateProject(project)) return;
    m_document.commitAnalysisProject(project);
}

void AnalysisController::restorePersistedComparisonSlots()
{
    // Retried whenever the day's laps change, not only once per document
    // (KAN-82): a saved lap whose recording was missing at open comes back
    // once it is relinked. An explicit clear saves null and choosing another
    // lap fills the slot, so neither is ever overridden.
    if (outingLapsLoading() || !m_document.isEventDocument()) return;
    const auto savedComparisonSlots = m_document.analysisProject().value("event").toObject()
        .value("analysisDecisions").toObject().value("comparisonSlots").toArray();
    for (int i = 0; i < 2 && i < savedComparisonSlots.size(); ++i) {
        if (!m_comparisonSlots[i].row.isEmpty() || !savedComparisonSlots[i].isObject()) continue;
        // A stale/unresolvable reference (changed derivation, removed run, etc.)
        // is left empty rather than silently reconnected by lap number.
        selectComparisonLap(i, savedComparisonSlots[i].toObject().toVariantMap());
    }
}

QVariantMap AnalysisController::comparisonPersistedRangeMeters() const
{
    const auto range = m_document.analysisProject().value("event").toObject()
        .value("analysisDecisions").toObject().value("comparisonRange").toObject();
    if (!range.value("startMeters").isDouble() || !range.value("endMeters").isDouble()) return {};
    return {{"startMeters", range.value("startMeters").toDouble()}, {"endMeters", range.value("endMeters").toDouble()}};
}

QStringList AnalysisController::comparisonPersistedChannels() const
{
    QStringList result;
    for (const auto &value : m_document.analysisProject().value("event").toObject()
             .value("analysisDecisions").toObject().value("comparisonChannels").toArray())
        result.append(value.toString());
    return result;
}

void AnalysisController::persistComparisonRange(const double startMeters, const double endMeters)
{
    if (!m_document.isEventDocument() || !std::isfinite(startMeters) || !std::isfinite(endMeters)
        || startMeters < 0.0 || endMeters <= startMeters
        || endMeters > ProjectLimits::maximumComparisonRangeMeters) return;
    auto project = m_document.analysisProject();
    auto event = project.value("event").toObject();
    auto decisions = event.value("analysisDecisions").toObject();
    const QJsonObject range{{"startMeters", startMeters}, {"endMeters", endMeters}};
    if (decisions.value("comparisonRange").toObject() == range) return;
    decisions.insert("comparisonRange", range);
    event.insert("analysisDecisions", decisions);
    project.insert("event", event);
    if (!ProjectLimits::validateProject(project)) return;
    m_document.commitAnalysisProject(project);
}

void AnalysisController::persistComparisonChannels(const QStringList &channels)
{
    if (!m_document.isEventDocument() || channels.size() > ProjectLimits::maximumComparisonChannels) return;
    auto project = m_document.analysisProject();
    auto event = project.value("event").toObject();
    auto decisions = event.value("analysisDecisions").toObject();
    QJsonArray array;
    for (const auto &channel : channels) array.append(channel);
    if (decisions.value("comparisonChannels").toArray() == array) return;
    decisions.insert("comparisonChannels", array);
    event.insert("analysisDecisions", decisions);
    project.insert("event", event);
    if (!ProjectLimits::validateProject(project)) return;
    m_document.commitAnalysisProject(project);
}

void AnalysisController::failComparisonLap(const int index, const QString &reason)
{
    auto &slot = m_comparisonSlots[index];
    if (slot.state == "error" && slot.error == reason) return;
    if (m_comparisonPending && m_comparisonLoadingSlot == index && m_comparisonCancellation)
        m_comparisonCancellation->store(true);
    slot.request = ++m_comparisonRequest;
    slot.session.reset(); slot.geometry = {}; slot.track.clear();
    slot.referenceTrace = {}; slot.referenceGate = {}; slot.hasReferenceGate = false;
    slot.state = "error"; slot.error = reason;
}

void AnalysisController::invalidateComparisonLaps()
{
    restorePersistedComparisonSlots();
    for (int i = 0; i < 2; ++i) {
        auto &slot = m_comparisonSlots[i];
        if (slot.row.isEmpty()) continue;
        if (slot.key != outingRunKey(slot.row.value("runId").toString())) {
            failComparisonLap(i, QStringLiteral("Recording or track configuration changed. Select this lap again."));
            continue;
        }
        // An unrelated run refresh temporarily hides the row list, not this slot.
        if (outingLapsLoading()) continue;
        const auto resolved = resolveOutingLapReference(slot.row.value("reference").toMap());
        if (resolved.value("state") != "resolved") {
            failComparisonLap(i, resolved.value("reason").toString());
            continue;
        }
        const auto row = m_outingLapRows[resolved.value("index").toInt()].toMap();
        if (!eligible(row)) failComparisonLap(i, QStringLiteral("This lap is no longer eligible. Select an eligible lap."));
        slot.row = row;
    }
    if (m_comparisonSlots[0].state == "ready" && m_comparisonSlots[1].state == "ready"
        && m_comparisonSlots[0].row.value("compatibilityGroupId") != m_comparisonSlots[1].row.value("compatibilityGroupId"))
        failComparisonLap(1, QStringLiteral("These laps no longer share a compatible track configuration."));
    emit comparisonSlotsChanged();
}

bool AnalysisController::swapComparisonLaps()
{
    if (m_comparisonSlots[0].row.isEmpty() || m_comparisonSlots[1].row.isEmpty()) return false;
    if (m_comparisonCancellation) m_comparisonCancellation->store(true);
    std::swap(m_comparisonSlots[0], m_comparisonSlots[1]);
    // In-flight results still carry their old slot address and must be rejected.
    for (auto &slot : m_comparisonSlots) slot.request = ++m_comparisonRequest;
    for (int i = 0; i < 2; ++i)
        persistComparisonSlot(i, QJsonObject::fromVariantMap(m_comparisonSlots[i].row).value("reference"));
    m_comparisonTimer.start();
    emit comparisonSlotsChanged();
    return true;
}

bool AnalysisController::useBestComparisonLap(const bool wholeDay)
{
    if (outingLapsLoading()) return false;
    const auto &a = m_comparisonSlots[0];
    if (!wholeDay && a.row.isEmpty()) return false;
    const auto groupId = a.row.isEmpty() ? outingComparisonGroupId() : a.row.value("compatibilityGroupId").toString();
    for (const auto &value : outingCompatibilityGroups()) {
        const auto group = value.toMap();
        if (group.value("id") != groupId) continue;
        const auto ranking = group.value("ranking").toMap();
        if (wholeDay) return selectComparisonLap(1, ranking.value("bestOfDay").toMap().value("reference").toMap());
        for (const auto &runValue : ranking.value("runs").toList()) {
            const auto run = runValue.toMap();
            if (run.value("runId") == a.row.value("runId"))
                return selectComparisonLap(1, run.value("bestLap").toMap().value("reference").toMap());
        }
    }
    return false;
}

bool AnalysisController::inspectComparisonLap(const int index)
{
    return index >= 0 && index < 2 && m_comparisonSlots[index].state == "ready"
        && selectOutingLapReference(m_comparisonSlots[index].row.value("reference").toMap());
}

void AnalysisController::setComparisonViewOpen(const bool open)
{
    if (m_comparisonViewOpen == open) return;
    m_comparisonViewOpen = open;
    if (!open) {
        clearComparisonFocusSegment();
        m_comparisonSegmentationRunId.clear();
    }
    emit comparisonViewOpenChanged();
}

QVariantMap AnalysisController::comparisonLapSeries(
    const int slot, const QString &channel, const double startTime, const double endTime,
    const int maximumPoints) const
{
    if (slot < 0 || slot > 1 || maximumPoints < 2) return {};
    const auto &comparisonSlot = m_comparisonSlots[slot];
    if (!comparisonSlot.session || comparisonSlot.state != "ready") return {};
    return sessionSeries(*comparisonSlot.session, channel, startTime, endTime, maximumPoints);
}

QVariantList AnalysisController::comparisonLapTrack(const int slot) const
{
    if (slot < 0 || slot > 1) return {};
    return m_comparisonSlots[slot].track;
}

void AnalysisController::ensureComparisonSharedGeometry() const
{
    const auto &a = m_comparisonSlots[0];
    const auto &b = m_comparisonSlots[1];
    if (a.state != "ready" || b.state != "ready" || !a.session || !b.session) {
        if (m_comparisonSharedGeometryRequestA != 0 || m_comparisonSharedGeometryRequestB != 0) {
            m_comparisonSharedGeometry = {};
            m_comparisonSharedGeometryRequestA = 0;
            m_comparisonSharedGeometryRequestB = 0;
            m_comparisonOverlayTrackCache = {};
        }
        return;
    }
    if (m_comparisonSharedGeometryRequestA == a.request && m_comparisonSharedGeometryRequestB == b.request) return;
    m_comparisonSharedGeometry = buildSharedTrackGeometry(
        *a.session, a.row.value("startTime").toDouble(), a.row.value("endTime").toDouble(),
        *b.session, b.row.value("startTime").toDouble(), b.row.value("endTime").toDouble());
    m_comparisonOverlayTrackCache[0] = m_comparisonSharedGeometry.valid
        ? buildTrackSegments(*a.session, a.row.value("startTime").toDouble(), a.row.value("endTime").toDouble(),
              m_comparisonSharedGeometry)
        : QVariantList{};
    m_comparisonOverlayTrackCache[1] = m_comparisonSharedGeometry.valid
        ? buildTrackSegments(*b.session, b.row.value("startTime").toDouble(), b.row.value("endTime").toDouble(),
              m_comparisonSharedGeometry)
        : QVariantList{};
    m_comparisonSharedGeometryRequestA = a.request;
    m_comparisonSharedGeometryRequestB = b.request;
}

QVariantList AnalysisController::comparisonOverlayTrack(const int slot) const
{
    if (slot < 0 || slot > 1) return {};
    ensureComparisonSharedGeometry();
    return m_comparisonOverlayTrackCache[slot];
}

namespace {
// Discrete/categorical channels (a gear number, a flag) must never be
// linearly interpolated -- halfway between gear 2 and gear 3 is not gear 2.5.
// Analog channels (speed, throttle %, RPM, G, temperature, HR) are fine to
// interpolate. Name-based, not a full provenance system: there is no
// existing "measured vs calculated" field on TelemetryChannel to draw on,
// so this is deliberately the minimal safety behavior the acceptance
// criterion actually requires (never fabricate/misrepresent a value),
// not a claim that full channel provenance metadata exists.
bool isDiscreteChannel(const QString &channel)
{
    return channel.compare("gear", Qt::CaseInsensitive) == 0;
}
}

void AnalysisController::ensureComparisonProgressAxis() const
{
    const auto &a = m_comparisonSlots[0];
    const auto &b = m_comparisonSlots[1];
    if (a.state != "ready" || b.state != "ready" || !a.session || !b.session) {
        if (m_comparisonProgressAxisRequestA != 0 || m_comparisonProgressAxisRequestB != 0) {
            m_comparisonProgressAxis = {};
            m_comparisonProgressAxisRequestA = 0;
            m_comparisonProgressAxisRequestB = 0;
            m_comparisonProgressTraceCache = {};
        }
        return;
    }
    if (m_comparisonProgressAxisRequestA == a.request && m_comparisonProgressAxisRequestB == b.request) return;

    m_comparisonProgressAxis = {};
    m_comparisonProgressTraceCache = {};
    if (a.hasReferenceGate) {
        const GeoCoordinate origin{
            (a.referenceGate.endpointA.latitudeDegrees + a.referenceGate.endpointB.latitudeDegrees) / 2.0,
            (a.referenceGate.endpointA.longitudeDegrees + a.referenceGate.endpointB.longitudeDegrees) / 2.0};
        m_comparisonProgressAxis = buildProgressAxis(a.referenceTrace, origin, a.referenceGate);
    }
    if (m_comparisonProgressAxis.valid) {
        m_comparisonProgressTraceCache[0] = projectLapTrace(
            m_comparisonProgressAxis, *a.session, a.row.value("startTime").toDouble(), a.row.value("endTime").toDouble());
        m_comparisonProgressTraceCache[1] = projectLapTrace(
            m_comparisonProgressAxis, *b.session, b.row.value("startTime").toDouble(), b.row.value("endTime").toDouble());
    }
    m_comparisonProgressAxisRequestA = a.request;
    m_comparisonProgressAxisRequestB = b.request;
}

QVariantMap AnalysisController::comparisonPositionAtProgress(const int slot, const double progressMeters) const
{
    if (slot < 0 || slot > 1) return {};
    const auto &comparisonSlot = m_comparisonSlots[slot];
    if (!comparisonSlot.session || comparisonSlot.state != "ready") return {};
    ensureComparisonProgressAxis();
    if (!m_comparisonProgressAxis.valid) return {};
    const auto time = timeAtProgress(m_comparisonProgressTraceCache[slot], progressMeters);
    if (!time) return {};
    ensureComparisonSharedGeometry();
    if (!m_comparisonSharedGeometry.valid) return {};
    const auto point = FlappedEar::currentTrackPoint(*comparisonSlot.session, *time, m_comparisonSharedGeometry);
    if (!point) return {};
    return {{"x", point->x()}, {"y", point->y()}};
}

bool AnalysisController::openComparisonLapAtProgress(const int slot, const double progressMeters)
{
    if (slot < 0 || slot > 1 || !comparisonPairReady()) return false;
    ensureComparisonProgressAxis();
    const auto time = m_comparisonProgressAxis.valid
        ? timeAtProgress(m_comparisonProgressTraceCache[slot], progressMeters) : std::nullopt;
    const auto row = m_comparisonSlots[slot].row;
    if (!selectOutingLapReference(row.value("reference").toMap())) return false;
    // Without a projected time here the lap still opens, at its start.
    if (time) {
        m_outingLapCursor = std::clamp(*time, row.value("startTime").toDouble(), row.value("endTime").toDouble());
        emit outingLapCursorChanged();
        emit outingLapVideoChanged();
    }
    return true;
}

double AnalysisController::comparisonProgressAxisLength() const
{
    ensureComparisonProgressAxis();
    return m_comparisonProgressAxis.valid ? m_comparisonProgressAxis.lengthMeters : 0.0;
}

QVariantMap AnalysisController::comparisonDeltaSeriesByProgress(
    const double startProgress, const double endProgress, const int maximumPoints) const
{
    if (maximumPoints < 2) return {};
    ensureComparisonProgressAxis();
    if (!m_comparisonProgressAxis.valid) return {};
    if (!std::isfinite(startProgress) || !std::isfinite(endProgress) || endProgress <= startProgress)
        return {{"reason", QStringLiteral("invalidRange")}};

    const auto deltaSegments = computeDeltaSeries(
        m_comparisonProgressTraceCache[0], m_comparisonProgressTraceCache[1], (endProgress - startProgress) / maximumPoints);
    QVariantList segments;
    double minimum = 0.0, maximum = 0.0;
    bool haveExtent = false;
    for (const auto &deltaSegment : deltaSegments) {
        QVariantList points;
        for (const auto &point : deltaSegment) {
            if (point.progressMeters < startProgress || point.progressMeters > endProgress) continue;
            if (!haveExtent) { minimum = maximum = point.deltaSeconds; haveExtent = true; }
            else { minimum = std::min(minimum, point.deltaSeconds); maximum = std::max(maximum, point.deltaSeconds); }
            points.append(QPointF((point.progressMeters - startProgress) / (endProgress - startProgress), point.deltaSeconds));
        }
        if (!points.isEmpty()) segments.append(QVariant::fromValue(points));
    }
    if (segments.isEmpty()) return {};
    return {
        {"segments", segments},
        {"minimum", minimum},
        {"maximum", maximum},
        {"unit", QStringLiteral("s")},
    };
}

QVariantMap AnalysisController::comparisonChannelSeriesByProgress(const int slot, const QString &channel,
    const double startProgress, const double endProgress, const int maximumPoints) const
{
    if (slot < 0 || slot > 1 || maximumPoints < 2) return {};
    const auto &comparisonSlot = m_comparisonSlots[slot];
    if (!comparisonSlot.session || comparisonSlot.state != "ready") return {};
    const auto &session = *comparisonSlot.session;
    const QString resolved = session.aliases.value(channel, channel);
    const auto channelIterator = session.channels.constFind(resolved);
    if (channelIterator == session.channels.cend()) return {{"reason", QStringLiteral("channelMissing")}};
    if (!std::isfinite(startProgress) || !std::isfinite(endProgress) || endProgress <= startProgress)
        return {{"reason", QStringLiteral("invalidRange")}};
    ensureComparisonProgressAxis();
    if (!m_comparisonProgressAxis.valid) return {{"reason", QStringLiteral("channelMissing")}};

    const auto interpolation = isDiscreteChannel(channel) ? InterpolationMode::Previous : InterpolationMode::Linear;
    const double span = endProgress - startProgress;
    QVariantList segments;
    QVariantList current;
    double minimum = 0.0, maximum = 0.0;
    bool haveExtent = false;
    for (int index = 0; index < maximumPoints; ++index) {
        const double targetProgress = startProgress + span * index / (maximumPoints - 1);
        const auto time = timeAtProgress(m_comparisonProgressTraceCache[slot], targetProgress);
        const auto value = time ? session.valueAt(channel, *time, interpolation) : std::nullopt;
        if (!time || !value) {
            if (!current.isEmpty()) { segments.append(QVariant::fromValue(current)); current.clear(); }
            continue;
        }
        if (!haveExtent) { minimum = maximum = *value; haveExtent = true; }
        else { minimum = std::min(minimum, *value); maximum = std::max(maximum, *value); }
        // QPointF, not {"x":..,"y":..}: this runs for every point of every
        // visible row on every zoom/pan step, and a QVariantMap's QString-keyed
        // QMap is dramatically more expensive to build per point than a plain
        // value type. QML reads point.x/point.y the same way either way.
        current.append(QPointF((targetProgress - startProgress) / span, *value));
    }
    if (!current.isEmpty()) segments.append(QVariant::fromValue(current));
    if (segments.isEmpty()) return {};
    return {
        {"segments", segments},
        {"brakingUp", resolved == session.aliases.value("longitudinalAcceleration")},
        {"minimum", minimum},
        {"maximum", maximum},
        {"unit", channelIterator->unit},
    };
}

void AnalysisController::initializeComparisonLaps()
{
    m_comparisonTimer.setSingleShot(true);
    m_comparisonTimer.setInterval(0);
    connect(&m_comparisonTimer, &QTimer::timeout, this, &AnalysisController::loadComparisonLap);
    connect(this, &AnalysisController::documentStateChanged, this, &AnalysisController::invalidateComparisonLaps);
    connect(this, &AnalysisController::sourceLoadStateChanged, this, &AnalysisController::invalidateComparisonLaps);
    connect(this, &AnalysisController::outingLapsChanged, this, &AnalysisController::invalidateComparisonLaps);
    connect(&m_comparisonWatcher, &QFutureWatcher<OutingLapDetailResult>::finished, this, [this] {
        auto result = m_comparisonWatcher.future().takeResult();
        const int index = m_comparisonLoadingSlot;
        m_comparisonPending = false;
        m_comparisonLoadingSlot = -1;
        auto &slot = m_comparisonSlots[index];
        if (slot.request == result.request && slot.state == "loading") {
            if (slot.key != outingRunKey(slot.row.value("runId").toString())) {
                failComparisonLap(index, QStringLiteral("Recording changed while opening this lap. Select it again."));
            } else {
                slot.session = std::move(result.session);
                slot.geometry = std::move(result.geometry);
                slot.track = std::move(result.track);
                slot.referenceTrace = std::move(result.referenceTrace);
                slot.referenceGate = std::move(result.referenceGate);
                slot.hasReferenceGate = result.hasReferenceGate;
                slot.error = result.error;
                slot.state = slot.session ? "ready" : "error";
                if (slot.state == "ready")
                    persistComparisonSlot(index, QJsonObject::fromVariantMap(slot.row).value("reference"));
                if (result.staleReference) {
                    m_outingStaleRunIds.insert(slot.row.value("runId").toString());
                    refreshOutingCompatibility();
                    emit outingLapsChanged();
                }
            }
        }
        invalidateComparisonLaps();
        m_comparisonTimer.start();
    });
}

void AnalysisController::loadComparisonLap()
{
    // One bounded parser for the pair. Repeated selections cancel and coalesce;
    // the other slot's verified session is retained throughout.
    if (m_comparisonPending) return;
    for (int i = 0; i < 2; ++i) {
        const auto &slot = m_comparisonSlots[i];
        if (slot.state != "loading") continue;
        const auto source = slot.source;
        const auto row = slot.row;
        const auto request = slot.request;
        const auto projectPath = m_document.documentProjectPath();
        m_comparisonCancellation = std::make_shared<std::atomic_bool>(false);
        const auto cancellation = m_comparisonCancellation;
        m_comparisonPending = true;
        m_comparisonLoadingSlot = i;
        m_comparisonWatcher.setFuture(QtConcurrent::run([source, projectPath, row, request, cancellation, cache = m_analysisSourceCache] {
            return FlappedEar::loadOutingLapDetail(source, projectPath, row, request, cancellation, cache, /*deriveReferenceGate=*/true);
        }));
        return;
    }
}
