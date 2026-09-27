// KAN-48: review of automatic track-segmentation proposals for the open lap.
// Proposals, rejections and edits live only in this review session; approval
// writes an ordinary segment into the run's "trackSegments", which is the
// approved revision every downstream consumer must use.

#include "app/AnalysisController.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectLimits.h"
#include "telemetry/TrackSegmentReview.h"
#include <QJsonArray>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>

using namespace FlappedEar;

namespace {

QString uncertaintyText(const QString &reason)
{
    if (reason == proposalUncertainConnectedCorners) return QStringLiteral("Corners connect without a straight");
    if (reason == proposalUncertainShortStraight) return QStringLiteral("Short straight");
    if (reason == proposalUncertainGpsGap) return QStringLiteral("Near a GPS gap in this lap");
    return reason;
}

QStringList uncertaintyTexts(const QStringList &reasons)
{
    QStringList texts;
    for (const auto &reason : reasons) texts.append(uncertaintyText(reason));
    return texts;
}

QString unresolvedProposalText(const QString &reason)
{
    if (reason == "continuousCorner")
        return QStringLiteral("No automatic proposal: this lap turns continuously, with no straight between corners.");
    if (reason == "noCorners") return QStringLiteral("No automatic proposal: no corner was detected on this lap.");
    if (reason == "tooManySegments")
        return QStringLiteral("No automatic proposal: the lap would split into more than %1 segments.").arg(maximumTrackSegments);
    return QStringLiteral("No automatic proposal could be made for this lap.");
}

QString apexText(const CornerPhasePoint &apex)
{
    if (apex.resolved()) return {};
    if (apex.unresolvedReason == cornerPhaseMultipleApexes) return QStringLiteral("Multiple apexes — review manually");
    if (apex.unresolvedReason == cornerPhaseCrossesGate) return QStringLiteral("Corner crosses the timing gate");
    return QStringLiteral("Apex unresolved");
}

std::optional<TrackSegmentType> segmentTypeFromName(const QString &name)
{
    for (const auto type : {TrackSegmentType::Sector, TrackSegmentType::Corner, TrackSegmentType::Straight})
        if (trackSegmentTypeName(type) == name) return type;
    return std::nullopt;
}

// Progress ranges the lap's projected trace does not cover.
QVector<ProgressRange> coverageGaps(const QVector<ProgressSegment> &trace, const double length)
{
    QVector<ProgressRange> covered;
    for (const auto &segment : trace)
        if (!segment.samples.isEmpty())
            covered.append({segment.samples.first().progressMeters, segment.samples.last().progressMeters});
    std::sort(covered.begin(), covered.end(),
        [](const ProgressRange &a, const ProgressRange &b) { return a.startMeters < b.startMeters; });
    QVector<ProgressRange> gaps;
    double reached = 0.0;
    const auto addGap = [&gaps](const double start, const double end) {
        if (end - start >= segmentReviewMinimumGapMeters && gaps.size() < maximumProposalGpsGaps) gaps.append({start, end});
    };
    for (const auto &range : covered) {
        addGap(reached, range.startMeters);
        reached = std::max(reached, range.endMeters);
    }
    addGap(reached, length);
    return gaps;
}

} // namespace

void AnalysisController::initializeSegmentReview()
{
    connect(&m_segmentReviewWatcher, &QFutureWatcher<SegmentReviewResult>::finished, this, [this] {
        auto result = m_segmentReviewWatcher.future().takeResult();
        if (result.request != m_segmentReviewRequest) return; // stale: the lap closed or review restarted
        if (!result.error.isEmpty()) {
            m_segmentReviewState = QStringLiteral("error");
            m_segmentReviewMessage = result.error;
        } else if (!result.unavailable.isEmpty()) {
            m_segmentReviewState = QStringLiteral("unavailable");
            m_segmentReviewMessage = result.unavailable;
        } else {
            m_segmentReviewState = QStringLiteral("ready");
            m_segmentReviewMessage.clear();
            m_segmentReviewAxis = std::move(result.axis);
            m_segmentProposals = std::move(result.proposals.proposals);
            m_segmentProposalPhases = std::move(result.phases);
            m_segmentReviewLapTrace = std::move(result.lapTrace);
            // KAN-50: restore persisted rejections for this configuration and proposal algorithm.
            m_rejectedSegmentProposals = rejectedProposalIndexes(
                storedRunValue(m_selectedOutingLap.value("runId").toString(), QStringLiteral("trackSegmentReview")),
                segmentReviewConfiguration(), m_segmentProposals);
        }
        m_segmentReviewLayersDirty = true;
        m_segmentReviewPickTraceDirty = true;
        emit segmentReviewChanged();
    });
    // Approvals, and any other edit of the run's stored segments, change the approved revision.
    connect(this, &AnalysisController::documentStateChanged, this, [this] {
        if (m_segmentReviewState == "idle") return;
        m_segmentReviewLayersDirty = true;
        emit segmentReviewChanged();
    });
}

void AnalysisController::resetSegmentReview()
{
    ++m_segmentReviewRequest;
    if (m_segmentReviewCancellation) m_segmentReviewCancellation->store(true);
    const bool changed = m_segmentReviewState != "idle";
    m_segmentReviewState = QStringLiteral("idle");
    m_segmentReviewMessage.clear();
    m_segmentReviewAxis = {};
    m_segmentProposals.clear();
    m_segmentProposalPhases.clear();
    m_segmentReviewLapTrace.clear();
    m_editedSegmentProposals.clear();
    m_rejectedSegmentProposals.clear();
    m_segmentReviewLayerCache.clear();
    m_segmentReviewLayersDirty = true;
    m_segmentReviewPickTrace.clear();
    m_segmentReviewPickTraceDirty = true;
    m_segmentEditHistory.clear();
    if (changed) emit segmentReviewChanged();
}

QString AnalysisController::segmentReviewConfiguration() const
{
    return m_selectedOutingLap.value("compatibilityGroupId").toString();
}

QString AnalysisController::segmentReviewUnavailableReason() const
{
    if (m_selectedOutingLap.value("type").toString() != "LAP")
        return QStringLiteral("Segment proposals need a complete timed lap.");
    if (!m_selectedOutingLap.value("referenceIssue").toString().isEmpty()
        || !m_selectedOutingLap.value("layoutIssue").toString().isEmpty())
        return QStringLiteral("This lap has incomplete GPS or does not follow the run's layout. Open a clean lap to review segments.");
    if (segmentReviewConfiguration().isEmpty())
        return QStringLiteral("Confirm the track layout, direction and timing gate before reviewing segments.");
    return {};
}

void AnalysisController::requestSegmentReview()
{
    if (m_outingLapDetailState != "ready" || !m_outingLapDetailSession || m_segmentReviewState == "loading") return;
    resetSegmentReview();
    if (const auto reason = segmentReviewUnavailableReason(); !reason.isEmpty()) {
        m_segmentReviewState = QStringLiteral("unavailable");
        m_segmentReviewMessage = reason;
        emit segmentReviewChanged();
        return;
    }
    m_segmentReviewCancellation = std::make_shared<std::atomic_bool>(false);
    m_segmentReviewState = QStringLiteral("loading");
    emit segmentReviewChanged();
    m_segmentReviewWatcher.setFuture(QtConcurrent::run(
        [session = m_outingLapDetailSession, start = m_selectedOutingLap.value("startTime").toDouble(),
            end = m_selectedOutingLap.value("endTime").toDouble(), lap = m_selectedOutingLap.value("lapNumber").toInt(),
            request = m_segmentReviewRequest, cancellation = m_segmentReviewCancellation] {
            return computeSegmentReview(session, start, end, lap, request, cancellation);
        }));
}

AnalysisController::SegmentReviewResult AnalysisController::computeSegmentReview(std::shared_ptr<const TelemetrySession> session,
    const double startTime, const double endTime, const int lapNumber, const quint64 request,
    const std::shared_ptr<std::atomic_bool> &cancellation)
{
    SegmentReviewResult result;
    result.request = request;
    const auto cancelled = [cancellation] { return cancellation->load(); };
    try {
        if (!session) throw std::runtime_error("The lap's recording is not loaded.");
        const auto laps = deriveSourceLapSession(*session, {}, cancelled);
        const auto trace = std::find_if(laps.lapTraces.cbegin(), laps.lapTraces.cend(),
            [lapNumber](const LapTrace &candidate) { return candidate.lapNumber == lapNumber; });
        if (!laps.selectedStartGate || trace == laps.lapTraces.cend()) {
            result.unavailable = QStringLiteral("This lap has no gate-anchored GPS trace to build a track axis from.");
            return result;
        }
        const auto &gate = *laps.selectedStartGate;
        const GeoCoordinate origin{(gate.endpointA.latitudeDegrees + gate.endpointB.latitudeDegrees) / 2.0,
            (gate.endpointA.longitudeDegrees + gate.endpointB.longitudeDegrees) / 2.0};
        result.axis = buildProgressAxis(*trace, origin, gate, cancelled);
        const auto features = result.axis.valid
            ? computeTrackFeatures(result.axis, segmentReviewSmoothingMeters) : TrackFeatures{};
        if (!features.valid) {
            result.unavailable = QStringLiteral("The track axis could not be built from this lap's GPS trace.");
            return result;
        }
        throwIfCancelled(cancelled);
        result.lapTrace = projectLapTrace(result.axis, *session, startTime, endTime, cancelled);
        result.proposals = proposeTrackSegments(result.axis, features, coverageGaps(result.lapTrace, result.axis.lengthMeters));
        if (!result.proposals.valid) throw std::runtime_error("Segment proposals could not be computed for this lap.");
        if (!result.proposals.unresolvedReason.isEmpty()) {
            result.unavailable = unresolvedProposalText(result.proposals.unresolvedReason);
            return result;
        }
        for (const auto &proposal : result.proposals.proposals) {
            throwIfCancelled(cancelled);
            result.phases.append(proposal.type == TrackSegmentType::Corner
                ? proposeCornerGeometryPhases(result.axis, features, proposal) : CornerGeometryPhases{});
        }
    } catch (const OperationCancelled &) {
        result.error = QStringLiteral("Segment review was cancelled.");
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

double AnalysisController::segmentReviewAxisLength() const
{
    return m_segmentReviewAxis.valid ? m_segmentReviewAxis.lengthMeters : 0.0;
}

ApprovedSegmentation AnalysisController::currentApprovedSegmentation() const
{
    const auto runId = m_selectedOutingLap.value("runId").toString();
    for (const auto &value : m_document.analysisProject().value("event").toObject().value("runs").toArray()) {
        const auto run = value.toObject();
        if (run.value("id").toString() == runId)
            return approvedSegmentation(run.value("trackSegments"), segmentReviewConfiguration());
    }
    return {};
}

QVector<SegmentReviewItem> AnalysisController::currentSegmentReviewItems() const
{
    if (m_segmentReviewState != "ready") return {};
    return reviewSegmentProposals(m_segmentProposals, m_editedSegmentProposals, m_rejectedSegmentProposals,
        currentApprovedSegmentation(), m_segmentReviewAxis.lengthMeters);
}

QVariantList AnalysisController::segmentReviewItems() const
{
    QVariantList rows;
    const auto items = currentSegmentReviewItems();
    for (int index = 0; index < items.size(); ++index) {
        const auto &item = items[index];
        const auto &proposal = item.proposal;
        QVariantMap row{{"index", index}, {"name", proposal.name}, {"type", trackSegmentTypeName(proposal.type)},
            {"state", segmentReviewStateName(item.state)}, {"edited", item.edited},
            {"approvedSegmentId", item.approvedSegmentId},
            {"startMeters", proposal.start.progressMeters}, {"endMeters", proposal.end.progressMeters},
            {"lengthMeters", proposal.lengthMeters},
            {"startToleranceMeters", proposal.start.toleranceMeters}, {"endToleranceMeters", proposal.end.toleranceMeters},
            {"startUncertainty", uncertaintyTexts(proposal.start.uncertaintyReasons)},
            {"endUncertainty", uncertaintyTexts(proposal.end.uncertaintyReasons)},
            {"certain", proposal.start.certain() && proposal.end.certain()},
            {"wrapsGate", proposal.end.progressMeters < proposal.start.progressMeters}};
        if (proposal.type == TrackSegmentType::Corner)
            row.insert("turnDegrees", proposal.turnRadians * 180.0 / 3.14159265358979323846);
        // Geometric apex (KAN-46) only for unedited corners: an edit invalidates it.
        if (!item.edited && index < m_segmentProposalPhases.size() && m_segmentProposalPhases[index].valid) {
            const auto &apex = m_segmentProposalPhases[index].apex;
            if (apex.resolved()) {
                row.insert("apexMeters", apex.progressMeters);
                row.insert("apexToleranceMeters", apex.toleranceMeters);
            } else {
                row.insert("apexNote", apexText(apex));
            }
        }
        rows.append(row);
    }
    return rows;
}

QVariantMap AnalysisController::segmentReviewApproved() const
{
    if (m_segmentReviewState == "idle") return {};
    const auto approved = currentApprovedSegmentation();
    const auto items = currentSegmentReviewItems();
    QVariantList segments;
    for (const auto &value : approved.segments) {
        const auto segment = value.toObject();
        const auto id = segment.value("id").toString();
        const bool fromProposal = std::any_of(items.cbegin(), items.cend(),
            [&id](const SegmentReviewItem &item) { return item.approvedSegmentId == id; });
        segments.append(QVariantMap{{"id", id}, {"name", segment.value("name").toString()},
            {"type", segment.value("type").toString()},
            {"startMeters", segment.value("startProgressMeters").toDouble()},
            {"endMeters", segment.value("endProgressMeters").toDouble()}, {"matchesProposal", fromProposal}});
    }
    const auto hash = approved.revision.section(':', 1);
    return {{"valid", approved.valid}, {"revision", approved.revision}, {"shortRevision", hash.left(12)},
        {"canUndo", m_segmentEditHistory.nextUndo() != nullptr}, {"canRedo", m_segmentEditHistory.nextRedo() != nullptr},
        {"trackConfigurationReference", approved.trackConfigurationReference}, {"count", approved.segments.size()},
        {"otherConfigurationCount", approved.otherConfigurationSegments}, {"segments", segments}};
}

QVariantList AnalysisController::mapPolylines(const double startMeters, const double endMeters) const
{
    const double length = m_segmentReviewAxis.lengthMeters;
    if (!m_outingLapDetailSession || !m_segmentReviewAxis.valid || m_segmentReviewLapTrace.isEmpty()
        || !std::isfinite(startMeters) || !std::isfinite(endMeters)) return {};
    const double span = endMeters >= startMeters ? endMeters - startMeters : endMeters + length - startMeters;
    if (!(span > 0.0) || span > length) return {};
    const double step = std::max({2.0, m_segmentReviewAxis.spacingMeters, span / 2000.0});
    QVariantList polylines;
    QVariantList current;
    const auto flush = [&] {
        if (current.size() >= 2) polylines.append(QVariant::fromValue(current));
        current.clear();
    };
    for (double distance = 0.0;; distance = std::min(span, distance + step)) {
        const double progress = std::fmod(startMeters + distance, length);
        const auto time = timeAtProgress(m_segmentReviewLapTrace, progress);
        const auto point = time ? FlappedEar::currentTrackPoint(*m_outingLapDetailSession, *time, m_outingLapDetailGeometry)
                                : std::nullopt;
        if (point) current.append(QVariantMap{{"x", point->x()}, {"y", point->y()}});
        else flush(); // no coverage: never bridge across it
        if (distance >= span) break;
    }
    flush();
    return polylines;
}

QVariantList AnalysisController::segmentReviewMapLayers() const
{
    if (!m_segmentReviewLayersDirty) return m_segmentReviewLayerCache;
    m_segmentReviewLayersDirty = false;
    m_segmentReviewLayerCache.clear();
    if (m_segmentReviewState != "ready") return m_segmentReviewLayerCache;
    const double length = m_segmentReviewAxis.lengthMeters;
    for (const auto &value : currentApprovedSegmentation().segments) {
        const auto segment = value.toObject();
        m_segmentReviewLayerCache.append(QVariantMap{{"kind", "approved"}, {"id", segment.value("id").toString()},
            {"type", segment.value("type").toString()},
            {"polylines", mapPolylines(segment.value("startProgressMeters").toDouble(),
                segment.value("endProgressMeters").toDouble())}});
    }
    const auto items = currentSegmentReviewItems();
    for (int index = 0; index < items.size(); ++index) {
        const auto &item = items[index];
        if (item.state != SegmentReviewState::Proposed) continue;
        const auto &proposal = item.proposal;
        m_segmentReviewLayerCache.append(QVariantMap{{"kind", "proposal"}, {"index", index},
            {"type", trackSegmentTypeName(proposal.type)},
            {"polylines", mapPolylines(proposal.start.progressMeters, proposal.end.progressMeters)}});
        for (const auto *boundary : {&proposal.start, &proposal.end}) {
            if (boundary->certain() || !(boundary->toleranceMeters > 0.0)) continue;
            const double from = std::fmod(boundary->progressMeters - boundary->toleranceMeters + length, length);
            const double to = std::fmod(boundary->progressMeters + boundary->toleranceMeters, length);
            m_segmentReviewLayerCache.append(QVariantMap{{"kind", "uncertain"}, {"index", index},
                {"polylines", mapPolylines(from, to)}});
        }
    }
    return m_segmentReviewLayerCache;
}

bool AnalysisController::replaceRunField(const QString &runId, const QString &key, const QJsonValue &value,
    const std::function<void()> &beforeNotify)
{
    if (!documentEditable()) return false;
    auto project = m_document.analysisProject();
    auto event = project.value("event").toObject();
    auto runs = event.value("runs").toArray();
    // An empty array or object is stored as an absent key.
    const bool remove = value.isUndefined() || value.isNull() || (value.isArray() && value.toArray().isEmpty())
        || (value.isObject() && value.toObject().isEmpty());
    for (qsizetype i = 0; i < runs.size(); ++i) {
        auto run = runs[i].toObject();
        if (run.value("id").toString() != runId) continue;
        if (remove ? !run.contains(key) : run.value(key) == value) return true;
        if (remove) run.remove(key);
        else run.insert(key, value);
        runs[i] = run;
        event.insert("runs", runs);
        project.insert("event", event);
        QString error;
        if (!ProjectLimits::validateProject(project, &error)) return false;
        m_document.commitAnalysisProject(project);
        if (beforeNotify) beforeNotify();
        m_segmentReviewLayersDirty = true;
        emit segmentReviewChanged();
        return true;
    }
    return false;
}

bool AnalysisController::replaceRunTrackSegments(const QString &runId, const QJsonArray &segments, const bool recordHistory)
{
    const auto before = storedRunTrackSegments(runId).toArray();
    return replaceRunField(runId, QStringLiteral("trackSegments"), segments, [&] {
        if (recordHistory) m_segmentEditHistory.record(runId, before, segments);
    });
}

QString AnalysisController::approveSegmentProposal(const int index)
{
    const auto items = currentSegmentReviewItems();
    if (index < 0 || index >= items.size()) return QStringLiteral("This proposal is no longer available.");
    const auto &item = items[index];
    if (item.state == SegmentReviewState::Approved) return {};
    if (item.state == SegmentReviewState::Superseded)
        return QStringLiteral("Overlaps an approved segment. Revoke it or edit this proposal first.");
    const auto &proposal = item.proposal;
    const auto segment = makeTrackSegment(proposal.type, proposal.name, proposal.start.progressMeters,
        proposal.end.progressMeters, segmentReviewConfiguration());
    if (segment.isEmpty()) return QStringLiteral("This proposal cannot be stored as a segment.");
    const auto runId = m_selectedOutingLap.value("runId").toString();
    QJsonValue stored;
    for (const auto &value : m_document.analysisProject().value("event").toObject().value("runs").toArray())
        if (value.toObject().value("id").toString() == runId) stored = value.toObject().value("trackSegments");
    QString error;
    const auto next = withApprovedSegment(stored, segment, m_segmentReviewAxis.lengthMeters, &error);
    if (!next) return error;
    if (!replaceRunTrackSegments(runId, *next)) return QStringLiteral("The project cannot be changed right now.");
    m_rejectedSegmentProposals.remove(index);
    return {};
}

int AnalysisController::approveCertainSegmentProposals()
{
    const auto items = currentSegmentReviewItems();
    const auto runId = m_selectedOutingLap.value("runId").toString();
    QJsonValue stored;
    for (const auto &value : m_document.analysisProject().value("event").toObject().value("runs").toArray())
        if (value.toObject().value("id").toString() == runId) stored = value.toObject().value("trackSegments");
    int approved = 0;
    for (const auto &item : items) {
        if (item.state != SegmentReviewState::Proposed || !item.proposal.start.certain() || !item.proposal.end.certain())
            continue;
        const auto segment = makeTrackSegment(item.proposal.type, item.proposal.name,
            item.proposal.start.progressMeters, item.proposal.end.progressMeters, segmentReviewConfiguration());
        const auto next = segment.isEmpty() ? std::nullopt
            : withApprovedSegment(stored, segment, m_segmentReviewAxis.lengthMeters);
        if (!next) continue;
        stored = *next;
        ++approved;
    }
    if (approved == 0 || !replaceRunTrackSegments(runId, stored.toArray())) return 0;
    return approved;
}

bool AnalysisController::setSegmentProposalRejected(const int index, const bool rejected)
{
    const auto items = currentSegmentReviewItems();
    if (index < 0 || index >= items.size()) return false;
    const auto state = items[index].state;
    if (state != SegmentReviewState::Proposed && state != SegmentReviewState::Rejected) return false;
    const auto previous = m_rejectedSegmentProposals;
    if (rejected) m_rejectedSegmentProposals.insert(index);
    else m_rejectedSegmentProposals.remove(index);
    // KAN-50: rejections are saved with the run so save, recovery and reopen keep them.
    QList<int> ordered(m_rejectedSegmentProposals.cbegin(), m_rejectedSegmentProposals.cend());
    std::sort(ordered.begin(), ordered.end());
    QVector<TrackSegmentProposal> rejectedProposals;
    for (const int rejectedIndex : ordered)
        if (rejectedIndex >= 0 && rejectedIndex < m_segmentProposals.size()) rejectedProposals.append(m_segmentProposals[rejectedIndex]);
    const auto review = rejectedProposals.isEmpty() ? QJsonObject{}
        : makeTrackSegmentReview(segmentReviewConfiguration(), rejectedProposals);
    if ((!rejectedProposals.isEmpty() && review.isEmpty())
        || !replaceRunField(m_selectedOutingLap.value("runId").toString(), QStringLiteral("trackSegmentReview"), review)) {
        m_rejectedSegmentProposals = previous;
        return false;
    }
    m_segmentReviewLayersDirty = true;
    emit segmentReviewChanged();
    return true;
}

QString AnalysisController::editSegmentProposal(const int index, const QString &name, const QString &type,
    const double startMeters, const double endMeters)
{
    const auto items = currentSegmentReviewItems();
    if (index < 0 || index >= items.size()) return QStringLiteral("This proposal is no longer available.");
    if (items[index].state == SegmentReviewState::Approved)
        return QStringLiteral("Approved segments are not edited here. Revoke the approval to change the proposal.");
    const auto segmentType = segmentTypeFromName(type);
    if (!segmentType) return QStringLiteral("Choose corner, straight or sector.");
    QString error;
    if (!validProposalEdit(name, startMeters, endMeters, m_segmentReviewAxis.lengthMeters, &error)) return error;
    auto &proposal = m_segmentProposals[index];
    const auto place = [](SegmentProposalBoundary &boundary, const double meters) {
        if (boundary.progressMeters == meters) return;
        // A reviewer-placed boundary replaces the automatic estimate and its uncertainty.
        boundary = SegmentProposalBoundary{meters, 0.0, {}};
    };
    proposal.name = name.trimmed();
    proposal.type = *segmentType;
    place(proposal.start, startMeters);
    place(proposal.end, endMeters);
    const double span = endMeters - startMeters;
    proposal.lengthMeters = span > 0.0 ? span : span + m_segmentReviewAxis.lengthMeters;
    m_editedSegmentProposals.insert(index);
    m_segmentReviewLayersDirty = true;
    emit segmentReviewChanged();
    return {};
}

bool AnalysisController::revokeApprovedSegment(const QString &id)
{
    const auto runId = m_selectedOutingLap.value("runId").toString();
    for (const auto &value : m_document.analysisProject().value("event").toObject().value("runs").toArray()) {
        const auto run = value.toObject();
        if (run.value("id").toString() != runId) continue;
        const auto next = withoutApprovedSegment(run.value("trackSegments"), id);
        return next && replaceRunTrackSegments(runId, *next);
    }
    return false;
}

bool AnalysisController::discardOtherConfigurationSegments()
{
    const auto configuration = segmentReviewConfiguration();
    if (configuration.isEmpty()) return false;
    const auto runId = m_selectedOutingLap.value("runId").toString();
    for (const auto &value : m_document.analysisProject().value("event").toObject().value("runs").toArray()) {
        const auto run = value.toObject();
        if (run.value("id").toString() != runId) continue;
        if (!validTrackSegments(run.value("trackSegments"))) return false;
        return replaceRunTrackSegments(runId, withoutOtherConfigurations(run.value("trackSegments"), configuration));
    }
    return false;
}

QJsonValue AnalysisController::storedRunValue(const QString &runId, const QString &key) const
{
    for (const auto &value : m_document.analysisProject().value("event").toObject().value("runs").toArray())
        if (value.toObject().value("id").toString() == runId) return value.toObject().value(key);
    return {};
}

QJsonValue AnalysisController::storedRunTrackSegments(const QString &runId) const
{
    return storedRunValue(runId, QStringLiteral("trackSegments"));
}

QString AnalysisController::applySegmentEdit(const std::optional<QJsonArray> &next, const QString &error)
{
    if (!next) return error.isEmpty() ? QStringLiteral("This edit is not possible.") : error;
    if (!replaceRunTrackSegments(m_selectedOutingLap.value("runId").toString(), *next))
        return QStringLiteral("The project cannot be changed right now.");
    return {};
}

QString AnalysisController::editApprovedSegment(const QString &id, const QString &name, const QString &type,
    const double startMeters, const double endMeters, const bool keepAdjacentJoined)
{
    if (m_segmentReviewState != "ready") return QStringLiteral("Open the segment review first.");
    if (currentApprovedSegmentation().otherConfigurationSegments > 0)
        return QStringLiteral("Segments approved for a different track configuration must be discarded first.");
    QString error;
    const auto next = withEditedSegment(storedRunTrackSegments(m_selectedOutingLap.value("runId").toString()), id, name,
        type, startMeters, endMeters, keepAdjacentJoined, m_segmentReviewAxis.lengthMeters, &error);
    return applySegmentEdit(next, error);
}

QString AnalysisController::splitApprovedSegment(const QString &id, const double atMeters)
{
    if (m_segmentReviewState != "ready") return QStringLiteral("Open the segment review first.");
    if (currentApprovedSegmentation().otherConfigurationSegments > 0)
        return QStringLiteral("Segments approved for a different track configuration must be discarded first.");
    const auto stored = storedRunTrackSegments(m_selectedOutingLap.value("runId").toString());
    QString name;
    for (const auto &value : stored.toArray())
        if (value.toObject().value("id").toString() == id) name = value.toObject().value("name").toString();
    QString error;
    const auto next = withSplitSegment(stored, id, atMeters, QStringLiteral("%1 (2)").arg(name.left(150)),
        m_segmentReviewAxis.lengthMeters, &error);
    return applySegmentEdit(next, error);
}

QString AnalysisController::mergeApprovedSegments(const QString &firstId, const QString &secondId)
{
    if (m_segmentReviewState != "ready") return QStringLiteral("Open the segment review first.");
    if (currentApprovedSegmentation().otherConfigurationSegments > 0)
        return QStringLiteral("Segments approved for a different track configuration must be discarded first.");
    QString error;
    const auto next = withMergedSegments(storedRunTrackSegments(m_selectedOutingLap.value("runId").toString()),
        firstId, secondId, m_segmentReviewAxis.lengthMeters, &error);
    return applySegmentEdit(next, error);
}

QString AnalysisController::applySegmentHistoryStep(const bool undo)
{
    const auto *step = undo ? m_segmentEditHistory.nextUndo() : m_segmentEditHistory.nextRedo();
    if (!step) return undo ? QStringLiteral("Nothing to undo.") : QStringLiteral("Nothing to redo.");
    const auto runId = step->runId;
    const auto expected = undo ? step->after : step->before;
    const auto target = undo ? step->before : step->after;
    // Never overwrite a change made outside this history (another run, a reopened project).
    if (storedRunTrackSegments(runId).toArray() != expected) {
        m_segmentEditHistory.clear();
        emit segmentReviewChanged();
        return QStringLiteral("The segments changed outside this editor, so the edit history was cleared.");
    }
    if (!replaceRunTrackSegments(runId, target, false)) return QStringLiteral("The project cannot be changed right now.");
    if (undo) m_segmentEditHistory.commitUndo();
    else m_segmentEditHistory.commitRedo();
    emit segmentReviewChanged();
    return {};
}

QString AnalysisController::undoSegmentEdit()
{
    return applySegmentHistoryStep(true);
}

QString AnalysisController::redoSegmentEdit()
{
    return applySegmentHistoryStep(false);
}

QVariantMap AnalysisController::segmentReviewProgressAt(const double x, const double y) const
{
    if (m_segmentReviewState != "ready" || !m_segmentReviewAxis.valid || !m_outingLapDetailSession)
        return {{"error", QStringLiteral("Open the segment review first.")}};
    const double length = m_segmentReviewAxis.lengthMeters;
    if (m_segmentReviewPickTraceDirty) {
        m_segmentReviewPickTraceDirty = false;
        m_segmentReviewPickTrace.clear();
        const double step = std::max(1.0, m_segmentReviewAxis.spacingMeters);
        for (double progress = 0.0; progress < length; progress += step) {
            const auto time = timeAtProgress(m_segmentReviewLapTrace, progress);
            const auto point = time
                ? FlappedEar::currentTrackPoint(*m_outingLapDetailSession, *time, m_outingLapDetailGeometry) : std::nullopt;
            if (point) m_segmentReviewPickTrace.append({progress, *point});
        }
    }
    // Normalized map units: within 3% of the map; a second branch of the track
    // within 1% of that distance and 30 m away along the lap is ambiguous.
    const auto pick = pickProgressAt(m_segmentReviewPickTrace, {x, y}, 0.03, 0.01, 30.0, length);
    if (pick.progressMeters) return {{"progressMeters", *pick.progressMeters}};
    if (pick.reason == "ambiguous")
        return {{"error", QStringLiteral("Another part of the track passes close by here. Enter the distance instead.")}};
    if (pick.reason == "farFromTrack") return {{"error", QStringLiteral("Click on the lap's track line.")}};
    return {{"error", QStringLiteral("The lap trace is not available for picking.")}};
}

QVariantMap AnalysisController::outingLapSectorTimes() const
{
    if (m_segmentReviewState != "ready" || !m_segmentReviewAxis.valid) return {{"valid", false}};
    const auto times = computeLapSectorTimes(currentApprovedSegmentation(), m_segmentReviewAxis.lengthMeters,
        m_segmentReviewLapTrace, m_selectedOutingLap.value("startTime").toDouble(),
        m_selectedOutingLap.value("endTime").toDouble(),
        QJsonObject::fromVariantMap(m_selectedOutingLap.value("reference").toMap()));
    QVariantList sectors;
    for (const auto &sector : times.sectors) {
        QVariantMap row{{"segmentId", sector.segmentId}, {"name", sector.name}, {"type", sector.type},
            {"startMeters", sector.startProgressMeters}, {"endMeters", sector.endProgressMeters},
            {"lengthMeters", sector.lengthMeters}, {"coveredMeters", sector.coveredMeters}};
        if (sector.seconds) row.insert("seconds", *sector.seconds);
        else row.insert("unavailableReason", sector.unavailableReason);
        sectors.append(row);
    }
    QVariantMap result{{"valid", times.valid}, {"revision", times.stamp.revision},
        {"trackConfigurationReference", times.stamp.trackConfigurationReference},
        {"calculationAlgorithm", times.stamp.calculationAlgorithm}, {"completePartition", times.completePartition},
        {"lapSeconds", times.lapSeconds}, {"sectors", sectors}};
    if (times.sumSeconds) result.insert("sumSeconds", *times.sumSeconds);
    if (times.partitionErrorSeconds) result.insert("partitionErrorSeconds", *times.partitionErrorSeconds);
    return result;
}

namespace {

QVariantMap cornerSpeedValueMap(const CornerSpeedValue &value)
{
    QVariantMap map{{"progressMeters", value.progressMeters}, {"limitations", value.limitations}};
    if (value.value) map.insert("value", *value.value);
    else map.insert("unavailableReason", value.unavailableReason);
    if (value.telemetryTime) map.insert("telemetryTime", *value.telemetryTime);
    return map;
}

} // namespace

QVariantList AnalysisController::outingLapCornerSpeeds() const
{
    if (m_segmentReviewState != "ready" || !m_segmentReviewAxis.valid || !m_outingLapDetailSession) return {};
    const auto features = computeTrackFeatures(m_segmentReviewAxis, segmentReviewSmoothingMeters);
    const auto approved = currentApprovedSegmentation();
    QVariantList rows;
    for (const auto &value : approved.segments) {
        const auto segment = value.toObject();
        if (segment.value("type").toString() != trackSegmentTypeName(TrackSegmentType::Corner)) continue;
        const auto speeds = computeCornerSpeeds(m_segmentReviewAxis, features, approved, segment.value("id").toString(),
            m_segmentReviewLapTrace, *m_outingLapDetailSession);
        if (!speeds.valid) continue;
        rows.append(QVariantMap{{"segmentId", speeds.segmentId}, {"name", speeds.name}, {"channel", speeds.channel},
            {"unit", speeds.unit}, {"provenance", speeds.provenance}, {"entry", cornerSpeedValueMap(speeds.entry)},
            {"apex", cornerSpeedValueMap(speeds.apex)}, {"minimum", cornerSpeedValueMap(speeds.minimum)},
            {"exit", cornerSpeedValueMap(speeds.exit)}, {"lengthMeters", speeds.lengthMeters},
            {"coveredMeters", speeds.coveredMeters}, {"meanSampleSpacingMeters", speeds.meanSampleSpacingMeters},
            {"revision", speeds.stamp.revision}, {"calculationAlgorithm", speeds.stamp.calculationAlgorithm}});
    }
    return rows;
}

QVariantList AnalysisController::outingLapBrakingMetrics() const
{
    if (m_segmentReviewState != "ready" || !m_segmentReviewAxis.valid || !m_outingLapDetailSession) return {};
    const auto approved = currentApprovedSegmentation();
    QVariantList rows;
    for (const auto &value : approved.segments) {
        const auto segment = value.toObject();
        if (segment.value("type").toString() != trackSegmentTypeName(TrackSegmentType::Corner)) continue;
        const auto metrics = computeBrakingMetrics(m_segmentReviewAxis.lengthMeters, approved,
            segment.value("id").toString(), m_segmentReviewLapTrace, *m_outingLapDetailSession,
            m_selectedOutingLap.value("startTime").toDouble(), m_selectedOutingLap.value("endTime").toDouble());
        if (!metrics.valid) continue;
        QVariantMap row{{"segmentId", metrics.segmentId}, {"name", segment.value("name").toString()},
            {"intervalStartMeters", metrics.intervalStartMeters}, {"intervalEndMeters", metrics.intervalEndMeters},
            {"entryMeters", metrics.entryMeters}, {"method", metrics.method}, {"provenance", metrics.provenance},
            {"channel", metrics.channel}, {"thresholdUnit", metrics.thresholdUnit}, {"onThreshold", metrics.onThreshold},
            {"limitations", metrics.limitations}, {"decelerationChannel", metrics.decelerationChannel},
            {"decelerationUnit", metrics.decelerationUnit}, {"revision", metrics.stamp.revision},
            {"calculationAlgorithm", metrics.stamp.calculationAlgorithm}};
        if (!metrics.unavailableReason.isEmpty()) row.insert("unavailableReason", metrics.unavailableReason);
        if (!metrics.decelerationUnavailableReason.isEmpty())
            row.insert("decelerationUnavailableReason", metrics.decelerationUnavailableReason);
        const auto insert = [&row](const char *key, const std::optional<double> &number) {
            if (number) row.insert(QString::fromLatin1(key), *number);
        };
        insert("brakingPointMeters", metrics.brakingPointMeters);
        insert("brakingPointTime", metrics.brakingPointTime);
        insert("distanceBeforeEntryMeters", metrics.distanceBeforeEntryMeters);
        insert("brakingSeconds", metrics.brakingSeconds);
        insert("brakingDistanceMeters", metrics.brakingDistanceMeters);
        insert("peakDeceleration", metrics.peakDeceleration);
        insert("meanDeceleration", metrics.meanDeceleration);
        rows.append(row);
    }
    return rows;
}

QVariantList AnalysisController::outingLapExitMetrics() const
{
    if (m_segmentReviewState != "ready" || !m_segmentReviewAxis.valid || !m_outingLapDetailSession) return {};
    const auto approved = currentApprovedSegmentation();
    const double lapEnd = m_selectedOutingLap.value("endTime").toDouble();
    QVariantList rows;
    for (const auto &value : approved.segments) {
        const auto segment = value.toObject();
        if (segment.value("type").toString() != trackSegmentTypeName(TrackSegmentType::Corner)) continue;
        const auto metrics = computeExitMetrics(m_segmentReviewAxis.lengthMeters, approved, segment.value("id").toString(),
            m_segmentReviewLapTrace, *m_outingLapDetailSession, lapEnd);
        if (!metrics.valid) continue;
        QVariantMap pickup{{"method", metrics.pickup.method}, {"provenance", metrics.pickup.provenance},
            {"channel", metrics.pickup.channel}, {"unit", metrics.pickup.unit},
            {"onThreshold", metrics.pickup.threshold.on}, {"thresholdUnit", metrics.pickup.threshold.unit},
            {"limitations", metrics.pickup.limitations}};
        if (metrics.pickup.progressMeters) pickup.insert("progressMeters", *metrics.pickup.progressMeters);
        if (metrics.pickup.telemetryTime) pickup.insert("telemetryTime", *metrics.pickup.telemetryTime);
        if (!metrics.pickup.unavailableReason.isEmpty()) pickup.insert("unavailableReason", metrics.pickup.unavailableReason);
        QVariantMap row{{"segmentId", metrics.segmentId}, {"name", segment.value("name").toString()}, {"pickup", pickup},
            {"intervalSource", metrics.intervalSource}, {"intervalStartMeters", metrics.intervalStartMeters},
            {"intervalEndMeters", metrics.intervalEndMeters}, {"speedChannel", metrics.speedChannel},
            {"speedUnit", metrics.speedUnit}, {"revision", metrics.stamp.revision},
            {"calculationAlgorithm", metrics.stamp.calculationAlgorithm}};
        if (metrics.exitSpeed) row.insert("exitSpeed", *metrics.exitSpeed);
        if (metrics.intervalEndSpeed) row.insert("intervalEndSpeed", *metrics.intervalEndSpeed);
        if (metrics.elapsedSeconds) row.insert("elapsedSeconds", *metrics.elapsedSeconds);
        if (!metrics.downstreamUnavailableReason.isEmpty())
            row.insert("downstreamUnavailableReason", metrics.downstreamUnavailableReason);
        rows.append(row);
    }
    return rows;
}
