// Flapped Ear Telemetry's controller stack without the overlay editor
// (KAN-124): a project document and its day analysis, driven headless the
// way the Telemetry app will drive them. Links only flappedear_telemetry_app
// (Qt Core and Concurrent, no Gui): import, laps, segments, the day report,
// save and reopen.

#include "EventProjectFixture.h"
#include "app/TelemetryController.h"
#include "project/ProjectSourceReference.h"
#include "telemetry/DayReport.h"

#include <QCoreApplication>
#include <QThread>
#include <QDir>
#include <QElapsedTimer>
#include <QSysInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>

using namespace FlappedEar;

namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// routeVbo() with each lap's first half faster or slower, so the two
// sessions differ corner by corner.
QByteArray warpedRouteVbo(const bool fastFirstHalf, const int samplesPerLap = 240)
{
    const auto lines = QString::fromUtf8(EventProjectFixture::routeVbo(samplesPerLap)).split('\n');
    QStringList out;
    bool data = false;
    int index = 0;
    double time = 0.0;
    for (const auto &line : lines) {
        if (!data || line.trimmed().isEmpty()) { out << line; data = data || line == "[data]"; continue; }
        if (index > 0) {
            const bool firstHalf = (index - 1) % samplesPerLap < samplesPerLap / 2;
            time += 48.0 / samplesPerLap * (firstHalf == fastFirstHalf ? 0.9 : 1.1);
        }
        auto fields = line.split(' ');
        fields[0] = QString::number(time, 'f', 6);
        out << fields.join(' ');
        ++index;
    }
    return out.join('\n').toUtf8();
}

// KAN-103: a recording of the same session with a non-periodic speed trace
// (the loggers' clocks then align uniquely) and, for an alternative logger,
// an OBD coolant channel. `speedBias` makes its speed disagree.
QByteArray withSpeed(const QByteArray &vbo, const bool coolant, const double speedBias = 0.0)
{
    QStringList out;
    bool data = false;
    for (const auto &line : QString::fromUtf8(vbo).split('\n')) {
        if (line.startsWith("time latitude longitude")) { out << line + " velocity" + (coolant ? " coolant_temp-obd" : ""); continue; }
        if (!data || line.trimmed().isEmpty()) { out << line; data = data || line == "[data]"; continue; }
        const double t = line.split(' ').first().toDouble();
        const double speed = 90.0 + 25.0 * std::sin(0.11 * t) + 12.0 * std::sin(0.0007 * t * t) + speedBias;
        out << line + QString(" %1").arg(speed, 0, 'f', 3) + (coolant ? QString(" %1").arg(88.0 + 0.02 * t, 0, 'f', 3) : "");
    }
    return out.join('\n').toUtf8();
}

QVariantMap result(const QVariantMap &report, const QString &id)
{
    for (const auto &value : report.value("results").toList())
        if (value.toMap().value("id") == id) return value.toMap();
    return {};
}

bool settled(const QVariantMap &report)
{
    if (report.value("results").toList().isEmpty()) return false;
    for (const auto &value : report.value("results").toList()) {
        const auto status = value.toMap().value("status").toString();
        if (status == "notComputed" || status == "computing" || status == "stale") return false;
    }
    return true;
}

// Approves every proposed segment of the first timed lap, splitting one that
// wraps across start/finish, as a driver does in the segment review.
int approveSegments(AnalysisController &analysis)
{
    int lapIndex = -1;
    const auto rows = analysis.outingLaps();
    for (int i = 0; i < rows.size() && lapIndex < 0; ++i) {
        const auto row = rows[i].toMap();
        if (row.value("type") == "LAP" && !row.value("compatibilityGroupId").toString().isEmpty()) lapIndex = i;
    }
    if (lapIndex < 0 || !analysis.selectOutingLap(lapIndex)) return 0;
    if (!QTest::qWaitFor([&] { return analysis.outingLapDetailState() == "ready"; }, 20000)) return 0;
    analysis.requestSegmentReview();
    if (!QTest::qWaitFor([&] { return analysis.segmentReviewState() == "ready"; }, 20000)) return 0;
    const auto count = analysis.segmentReviewItems().size();
    for (int i = 0; i < count; ++i) if (!analysis.approveSegmentProposal(i).isEmpty()) return 0;
    for (const auto &value : analysis.segmentReviewApproved().value("segments").toList()) {
        const auto segment = value.toMap();
        if (segment.value("endMeters").toDouble() < segment.value("startMeters").toDouble()
            && !analysis.splitApprovedSegment(segment.value("id").toString(), analysis.segmentReviewAxisLength()).isEmpty())
            return 0;
    }
    const auto approved = analysis.segmentReviewApproved().value("count").toInt();
    analysis.closeOutingLap();
    return approved;
}
// Resident and peak resident memory of this process, in MiB (macOS).
double residentMiB()
{
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return -1;
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
}

double peakResidentMiB()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0); // bytes on macOS
}

QString hardwareModel()
{
    char model[256] = {};
    size_t size = sizeof(model);
    if (sysctlbyname("hw.model", model, &size, nullptr, 0) != 0) return QSysInfo::currentCpuArchitecture();
    char cpu[256] = {};
    size_t cpuSize = sizeof(cpu);
    sysctlbyname("machdep.cpu.brand_string", cpu, &cpuSize, nullptr, 0);
    return QString::fromLatin1(model) + " / " + QString::fromLatin1(cpu);
}
} // namespace

class TelemetryAppTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void importsAnalysesSavesAndReopensWithoutTheEditor();
    void keepsTheEditorStateAnotherAppSaved();
    void importsAFolderOfRecordings();
    void attachesAlternativeRecordingsAndSwitchesThePrimary();
    void reviewsApprovesAndReopensSourceFusion();
    void keepsAddingRunsWhileTheDayIsEdited();
    void measuresAPrivateFullDay();
};

void TelemetryAppTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FlappedEarTests"));
    QCoreApplication::setApplicationName(QStringLiteral("TelemetryAppTests"));
    QSettings().clear();
}

void TelemetryAppTests::importsAnalysesSavesAndReopensWithoutTheEditor()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeFile(first, warpedRouteVbo(true)));
    QVERIFY(writeFile(second, warpedRouteVbo(false)));
    const auto projectPath = directory.filePath("day.fetproject");
    QString bestLabel, decisions;
    double theoretical = 0;
    int eligibleLaps = 0;
    {
        TelemetryController controller(directory.filePath("recovery.json"));
        auto &document = *controller.document();
        auto &analysis = *controller.analysis();
        QSignalSpy committed(&document, &DocumentController::batchImportCommitted);
        QVERIFY(document.importAnalysisRuns("Test day",
            {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
        QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
        QCOMPARE(document.eventName(), QString("Test day"));
        QCOMPARE(document.eventRuns().size(), 2);
        QVERIFY(document.dirty());
        QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading(), 20000);
        QTRY_VERIFY(!analysis.outingComparisonGroupId().isEmpty());
        QCOMPARE(analysis.outingAnalysisStatus().value("state").toString(), QString("ready"));
        // No video link: the lap view has no video, and says so.
        QVERIFY(!analysis.outingLapVideoAvailable());

        QVERIFY(approveSegments(analysis) >= 2);
        analysis.requestOutingDayReport();
        QTRY_VERIFY_WITH_TIMEOUT(settled(analysis.outingDayReport()), 60000);
        const auto report = analysis.outingDayReport();
        QCOMPARE(validateDayReport(QJsonObject::fromVariantMap(report)), QString());
        const auto best = result(report, "bestLap").value("value").toMap();
        QVERIFY(std::abs(best.value("seconds").toDouble() - 48.0) < 0.01);
        const auto theoreticalBest = result(report, "theoreticalBest");
        QCOMPARE(theoreticalBest.value("status").toString(), QString("available"));
        theoretical = theoreticalBest.value("value").toMap().value("totalSeconds").toDouble();
        // Each session is fast in one half: the best of both halves beats either lap.
        QVERIFY(theoretical < 47.5 && theoretical > 43.2);
        bestLabel = best.value("label").toString();
        decisions = report.value("decisionsKey").toString();
        eligibleLaps = result(report, "consistency").value("value").toMap().value("day").toMap().value("count").toInt();
        QVERIFY(eligibleLaps >= 4);

        QVERIFY(document.saveProject(QUrl::fromLocalFile(projectPath)));
        QVERIFY(!document.dirty());
    }

    // A fresh controller opens the saved day and computes the same report.
    TelemetryController reopened(directory.filePath("recovery.json"));
    auto &document = *reopened.document();
    auto &analysis = *reopened.analysis();
    document.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_COMPARE_WITH_TIMEOUT(document.eventRuns().size(), 2, 20000);
    QVERIFY(!document.dirty());
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading(), 20000);
    analysis.requestOutingDayReport();
    QTRY_VERIFY_WITH_TIMEOUT(settled(analysis.outingDayReport()), 60000);
    const auto report = analysis.outingDayReport();
    QCOMPARE(result(report, "bestLap").value("value").toMap().value("label").toString(), bestLabel);
    QCOMPARE(result(report, "theoreticalBest").value("value").toMap().value("totalSeconds").toDouble(), theoretical);
    QCOMPARE(result(report, "consistency").value("value").toMap().value("day").toMap().value("count").toInt(), eligibleLaps);
    QCOMPARE(report.value("decisionsKey").toString(), decisions);
    // Opening and computing records nothing new in the document.
    QVERIFY(!document.dirty());
}

void TelemetryAppTests::keepsTheEditorStateAnotherAppSaved()
{
    // A project saved by the overlay editor carries editor state: the active
    // run's synchronisation and video, and the chart channels. The Telemetry
    // app has no editor; saving there keeps that state, and Save As into
    // another folder keeps every recording and video reachable.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto recording = directory.filePath("run.vbo");
    QVERIFY(writeFile(recording, warpedRouteVbo(true)));
    const auto video = directory.filePath("clip.mp4");
    QVERIFY(writeFile(video, "not decoded here"));
    const auto projectPath = directory.filePath("editor.fetproject");
    {
        TelemetryController controller(directory.filePath("recovery.json"));
        QSignalSpy committed(controller.document(), &DocumentController::batchImportCommitted);
        QVERIFY(controller.document()->importAnalysisRuns("Editor day", {QUrl::fromLocalFile(recording)}));
        QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
        QVERIFY(controller.document()->saveProject(QUrl::fromLocalFile(projectPath)));
    }
    QFile file(projectPath); QVERIFY(file.open(QIODevice::ReadOnly));
    auto project = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    // What the overlay editor stores, as it writes it.
    auto event = project.value("event").toObject();
    auto runs = event.value("runs").toArray();
    QCOMPARE(runs.size(), 1);
    auto run = runs[0].toObject();
    run.insert("sync", QJsonObject{{"offset", 12.5}, {"timeScale", 1.0}});
    auto runSources = run.value("sources").toObject();
    runSources.insert("video", QJsonObject{{"relativePath", "clip.mp4"}});
    run.insert("sources", runSources);
    runs[0] = run; event.insert("runs", runs); project.insert("event", event);
    project.insert("analysis", QJsonObject{{"channels", QJsonArray{"speed"}}});
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(project).toJson());
    file.close();

    TelemetryController controller(directory.filePath("recovery.json"));
    auto &document = *controller.document();
    auto &analysis = *controller.analysis();
    document.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_COMPARE_WITH_TIMEOUT(document.eventRuns().size(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading(), 20000);
    // An analysis edit (renaming the run), then Save As into another folder.
    const auto runId = document.activeRunId();
    const auto metadata = analysis.runMetadata(runId);
    QVERIFY(analysis.updateRunMetadata(runId, metadata.value("editToken").toString(), "Morning", {}, {}, {}));
    QVERIFY(document.dirty());
    QVERIFY(QDir(directory.path()).mkdir("archive"));
    const auto movedPath = directory.filePath("archive/editor.fetproject");
    QVERIFY(document.saveProject(QUrl::fromLocalFile(movedPath)));
    QFile moved(movedPath); QVERIFY(moved.open(QIODevice::ReadOnly));
    const auto saved = QJsonDocument::fromJson(moved.readAll()).object();
    const auto savedRun = saved.value("event").toObject().value("runs").toArray().first().toObject();
    QCOMPARE(savedRun.value("name").toString(), QString("Morning"));
    QCOMPARE(savedRun.value("sync").toObject().value("offset").toDouble(), 12.5);
    QCOMPARE(saved.value("analysis").toObject().value("channels").toArray(), QJsonArray{"speed"});
    const auto videoReference = ProjectSourceReferenceCodec::fromProject(
        QJsonObject{{"sources", savedRun.value("sources")}}, "video", "videoPath");
    QCOMPARE(QFileInfo(ProjectSourceReferenceCodec::resolve(videoReference, movedPath)).canonicalFilePath(),
             QFileInfo(video).canonicalFilePath());

    // Reopened from the new folder, the recording still resolves: laps load.
    TelemetryController reopened(directory.filePath("recovery.json"));
    reopened.document()->requestOpenProject(QUrl::fromLocalFile(movedPath));
    QTRY_COMPARE_WITH_TIMEOUT(reopened.document()->eventRuns().size(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.analysis()->outingLapsLoading(), 20000);
    QCOMPARE(reopened.analysis()->outingAnalysisStatus().value("state").toString(), QString("ready"));
    QVERIFY(reopened.analysis()->outingLaps().size() >= 3);
}

void TelemetryAppTests::importsAFolderOfRecordings()
{
    // KAN-87: a folder of recordings goes through the same review and event
    // as picked files. Subfolders only when asked; other files are reported,
    // not imported; cancelling leaves the document as it was.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QDir day(directory.filePath("day"));
    QVERIFY(day.mkpath("afternoon"));
    QVERIFY(writeFile(day.filePath("session-1.vbo"), warpedRouteVbo(true)));
    QVERIFY(writeFile(day.filePath("session-2.VBO"), warpedRouteVbo(false)));
    QVERIFY(writeFile(day.filePath("notes.txt"), "tyres 1.9 bar"));
    QVERIFY(writeFile(day.filePath("afternoon/session-3.vbo"), warpedRouteVbo(true, 200)));
    QVERIFY(QDir(directory.path()).mkpath("empty"));

    TelemetryController controller(directory.filePath("recovery.json"));
    auto &document = *controller.document();
    QSignalSpy committed(&document, &DocumentController::batchImportCommitted);

    // A folder without recordings is an explicit outcome, not a silent no-op.
    QVERIFY(document.importAnalysisFolder("Folder day", QUrl::fromLocalFile(directory.filePath("empty")), true));
    QTRY_COMPARE_WITH_TIMEOUT(document.batchImportState(), QString("error"), 20000);
    QVERIFY(document.batchImportError().contains("No VBO or RCZ recordings were found"));

    // Cancelling while scanning changes nothing.
    QVERIFY(document.importAnalysisFolder("Folder day", QUrl::fromLocalFile(day.path()), true));
    QCOMPARE(document.batchImportState(), QString("scanning"));
    document.cancelBatchImport();
    QTRY_COMPARE_WITH_TIMEOUT(document.batchImportState(), QString("idle"), 20000);
    QVERIFY(document.eventRuns().isEmpty());
    QVERIFY(!document.dirty());

    // Top level only: the two sessions, and the ignored note is reported.
    QVERIFY(document.importAnalysisFolder("Folder day", QUrl::fromLocalFile(day.path()), false));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
    QCOMPARE(document.eventRuns().size(), 2);
    QVERIFY(document.analysisImportMessages().join(' ').contains("1 other file(s) were ignored"));
    // Appended at once: the day's analysis bookkeeping while its laps derive
    // does not make an append import stale (9a79e67).

    // Again with subfolders: the sessions already imported are skipped and
    // the afternoon one is appended to the same event.
    QVERIFY(document.importAnalysisFolder({}, QUrl::fromLocalFile(day.path()), true));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 2, 20000);
    QCOMPARE(document.eventRuns().size(), 3);
    QCOMPARE(document.eventName(), QString("Folder day"));
    QVERIFY(document.analysisImportMessages().join(' ').contains("Already in this outing; skipped."));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.analysis()->outingLapsLoading(), 20000);
    QCOMPARE(controller.analysis()->outingAnalysisStatus().value("state").toString(), QString("ready"));
}

void TelemetryAppTests::attachesAlternativeRecordingsAndSwitchesThePrimary()
{
    // KAN-90: an alternative recording is attached to an existing run only
    // after its match evidence is reviewed, never becomes the primary by
    // itself, and a primary chosen explicitly re-derives the run's laps.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto primary = directory.filePath("session.vbo"), alternative = directory.filePath("session-copy.vbo");
    QVERIFY(writeFile(primary, warpedRouteVbo(true)));
    QVERIFY(writeFile(alternative, warpedRouteVbo(true, 200)));
    TelemetryController controller(directory.filePath("recovery.json"));
    auto &document = *controller.document();
    auto &analysis = *controller.analysis();
    QSignalSpy committed(&document, &DocumentController::batchImportCommitted);
    QVERIFY(document.importAnalysisRuns("Alternatives", {QUrl::fromLocalFile(primary)}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading(), 20000);
    const auto runId = document.activeRunId();
    QCOMPARE(document.runRecordings(runId).size(), 1);
    QVERIFY(document.runRecordings(runId).first().toMap().value("primary").toBool());
    // An exclusion on the current laps, to see what a primary change does to it.
    QVariantMap excluded;
    for (const auto &value : analysis.outingLaps())
        if (value.toMap().value("type") == "LAP") { excluded = value.toMap().value("reference").toMap(); break; }
    QVERIFY(analysis.setOutingLapExcluded(excluded, true, "Traffic"));

    // The same recording again is refused.
    QVERIFY(document.attachRunRecording(runId, QUrl::fromLocalFile(primary)));
    QTRY_COMPARE_WITH_TIMEOUT(document.runRecordingReview().value("state").toString(), QString("error"), 20000);
    QVERIFY(document.runRecordingReview().value("message").toString().contains("already in the run"));

    // A different recording of the session: reviewed with its evidence first.
    QVERIFY(document.attachRunRecording(runId, QUrl::fromLocalFile(alternative)));
    QTRY_COMPARE_WITH_TIMEOUT(document.runRecordingReview().value("state").toString(), QString("review"), 20000);
    const auto evidence = document.runRecordingReview().value("evidence").toMap();
    QCOMPARE(evidence.value("format").toString(), QString("VBO"));
    QVERIFY(evidence.contains("matched"));
    QCOMPARE(document.runRecordings(runId).size(), 1); // nothing added before confirming
    QVERIFY(document.confirmRunRecording());
    QTRY_VERIFY_WITH_TIMEOUT(document.runRecordingReview().isEmpty(), 20000);
    const auto recordings = document.runRecordings(runId);
    QCOMPARE(recordings.size(), 2);
    QVERIFY(recordings[0].toMap().value("primary").toBool());   // unchanged: no automatic preference
    QVERIFY(!recordings[1].toMap().value("primary").toBool());
    QVERIFY(document.dirty());

    // KAN-101: comparing the clocks describes the alignment and changes
    // nothing. These route fixtures carry no speed, so there is no measured
    // evidence: insufficient, never approved.
    QVERIFY(!document.checkRunRecordingAlignment(runId, recordings[0].toMap().value("sourceId").toString())); // the primary itself
    const auto revisionBefore = document.currentProjectObject();
    QVERIFY(document.checkRunRecordingAlignment(runId, recordings[1].toMap().value("sourceId").toString()));
    QCOMPARE(document.runRecordingReview().value("state").toString(), QString("aligning"));
    QTRY_COMPARE_WITH_TIMEOUT(document.runRecordingReview().value("state").toString(), QString("alignment"), 20000);
    const auto alignment = document.runRecordingReview().value("alignment").toMap();
    QCOMPARE(alignment.value("algorithm").toString(), QString("recording-alignment-v1"));
    QCOMPARE(alignment.value("status").toString(), QString("insufficient"));
    QCOMPARE(alignment.value("reason").toString(), QString("noSpeed"));
    QVERIFY(!alignment.contains("offsetSeconds"));
    QCOMPARE(document.runRecordingReview().value("name").toString(), QString("session-copy.vbo"));
    QCOMPARE(document.currentProjectObject(), revisionBefore);
    document.cancelRunRecording();
    QVERIFY(document.runRecordingReview().isEmpty());

    // Choosing the alternative as primary re-derives the run from it.
    const auto previousPrimary = recordings[0].toMap().value("sourceId").toString();
    const auto newPrimary = recordings[1].toMap().value("sourceId").toString();
    QVERIFY(document.setRunPrimarySource(runId, newPrimary));
    QTRY_VERIFY_WITH_TIMEOUT(document.runRecordingReview().isEmpty(), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading() && !analysis.outingLaps().isEmpty(), 20000);
    const auto after = document.runRecordings(runId);
    QVERIFY(!after[0].toMap().value("primary").toBool());
    QVERIFY(after[1].toMap().value("primary").toBool());
    QCOMPARE(after[0].toMap().value("sourceId").toString(), previousPrimary); // identities kept
    for (const auto &value : analysis.outingLaps())
        QCOMPARE(value.toMap().value("reference").toMap().value("sourceId").toString(), newPrimary);
    const auto run = document.currentProjectObject().value("event").toObject().value("runs").toArray().first().toObject();
    QVERIFY(run.value("trackConfiguration").toObject().value("gateRevision").toString().startsWith("gates-v1:"));
    // The exclusion referred to the old primary's laps: kept, reported, not applied.
    QVERIFY(analysis.outingLapMessages().join(' ').contains("could not be matched"));

    // With the primary missing, attaching explains why it cannot compare.
    QVERIFY(QFile::rename(alternative, alternative + ".moved"));
    QVERIFY(!document.attachRunRecording(runId, QUrl::fromLocalFile(primary)));
    QVERIFY(document.runRecordingReview().value("message").toString().contains("primary recording is missing"));
}

void TelemetryAppTests::reviewsApprovesAndReopensSourceFusion()
{
    // KAN-103: fusing an alternative recording into a run's analysis is
    // reviewed (alignment, resulting channels, coverage, conflicts), needs a
    // rule for every conflict, is saved and reopened, and stops applying when
    // a recording no longer has the content it was approved with.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto primaryPath = directory.filePath("session.vbo"), obdPath = directory.filePath("session-obd.vbo"),
        biasedPath = directory.filePath("session-biased.vbo");
    QVERIFY(writeFile(primaryPath, withSpeed(warpedRouteVbo(true), false)));
    QVERIFY(writeFile(obdPath, withSpeed(warpedRouteVbo(true), true)));
    QVERIFY(writeFile(biasedPath, withSpeed(warpedRouteVbo(true), true, 8.0)));
    TelemetryController controller(directory.filePath("recovery.json"));
    auto &document = *controller.document();
    auto &analysis = *controller.analysis();
    QSignalSpy committed(&document, &DocumentController::batchImportCommitted);
    QVERIFY(document.importAnalysisRuns("Fusion", {QUrl::fromLocalFile(primaryPath)}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading(), 20000);
    const auto runId = document.activeRunId();
    for (const auto &path : {obdPath, biasedPath}) {
        QVERIFY(document.attachRunRecording(runId, QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(document.runRecordingReview().value("state").toString(), QString("review"), 20000);
        QVERIFY(document.confirmRunRecording());
        QTRY_VERIFY_WITH_TIMEOUT(document.runRecordingReview().isEmpty(), 20000);
    }
    const auto recordings = document.runRecordings(runId);
    QCOMPARE(recordings.size(), 3);
    const auto obdId = recordings[1].toMap().value("sourceId").toString();
    const auto biasedId = recordings[2].toMap().value("sourceId").toString();

    // The biased logger: aligned, but its speed disagrees -- approval needs a rule.
    QVERIFY(document.reviewRunFusion(runId, biasedId));
    QTRY_COMPARE_WITH_TIMEOUT(document.runRecordingReview().value("state").toString(), QString("fusionReview"), 30000);
    auto review = document.runRecordingReview();
    QCOMPARE(review.value("alignment").toMap().value("status").toString(), QString("aligned"));
    auto preview = review.value("preview").toMap();
    QVERIFY(preview.value("approvable").toBool());
    QCOMPARE(preview.value("conflicts").toStringList(), QStringList{"speed"});
    QVERIFY(!document.approveRunFusion({}));                              // the conflict has no rule
    QVERIFY(!document.approveRunFusion({{"speed", "overwrite"}}));         // not a rule
    document.cancelRunRecording();

    // The OBD logger: speed agrees, coolant is added.
    QVERIFY(document.reviewRunFusion(runId, obdId));
    QTRY_COMPARE_WITH_TIMEOUT(document.runRecordingReview().value("state").toString(), QString("fusionReview"), 30000);
    preview = document.runRecordingReview().value("preview").toMap();
    QVERIFY(preview.value("approvable").toBool());
    QVERIFY(preview.value("conflicts").toStringList().isEmpty());
    QVariantMap coolant, speed;
    for (const auto &value : preview.value("channels").toList()) {
        if (value.toMap().value("name") == "coolant_temp-obd") coolant = value.toMap();
        if (value.toMap().value("key") == "speed") speed = value.toMap();
    }
    QVERIFY(coolant.value("added").toBool());
    QVERIFY(coolant.value("coverage").toDouble() > 0.95);
    QVERIFY(!speed.value("added").toBool() && !speed.value("conflicting").toBool());
    QVERIFY(speed.value("comparedSamples").toInt() > 100);
    QVERIFY(document.approveRunFusion({}));
    auto run = document.currentProjectObject().value("event").toObject().value("runs").toArray().first().toObject();
    QCOMPARE(run.value("fusion").toObject().value("alternativeSourceId").toString(), obdId);
    QVERIFY(document.dirty());
    QCOMPARE(document.runRecordings(runId)[1].toMap().value("fusion").toString(), QString("applied"));

    // The run's analysis now carries the fused coolant channel.
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading() && !analysis.outingLaps().isEmpty(), 20000);
    int lapIndex = -1;
    const auto laps = analysis.outingLaps();
    for (int index = 0; index < laps.size(); ++index)
        if (laps[index].toMap().value("type") == "LAP") { lapIndex = index; break; }
    QVERIFY(lapIndex >= 0);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading() && analysis.selectOutingLap(lapIndex), 20000);
    QTRY_COMPARE_WITH_TIMEOUT(analysis.outingLapDetailState(), QString("ready"), 20000);
    const auto series = analysis.outingLapSeries("coolant_temp-obd", 200);
    QVERIFY2(!series.contains("reason") && !series.value("segments").toList().isEmpty(), qPrintable(series.value("reason").toString()));

    // Saved and reopened: the decision is kept and applies again.
    const auto projectPath = directory.filePath("fusion.fetproject");
    QVERIFY(document.saveProject(QUrl::fromLocalFile(projectPath)));
    {
        TelemetryController reopened(directory.filePath("recovery-2.json"));
        reopened.document()->requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY_WITH_TIMEOUT(!reopened.document()->projectLoading() && reopened.document()->runRecordings(runId).size() == 3, 20000);
        QCOMPARE(reopened.document()->runRecordings(runId)[1].toMap().value("fusion").toString(), QString("applied"));
    }

    // The fused file changes on disk: the lap no longer opens with it.
    QVERIFY(writeFile(obdPath, withSpeed(warpedRouteVbo(true), true, 0.5)));
    // Saving under a new path re-derives the laps; select once they have settled.
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading() && analysis.selectOutingLap(lapIndex == 0 ? 1 : 0), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(analysis.outingLapDetailState() != "loading", 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading() && analysis.selectOutingLap(lapIndex), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(analysis.outingLapDetailState() == "error" || analysis.outingLapDetailState() == "stale", 20000);

    // Removing the fusion returns the run to its primary alone; so does a new primary.
    QVERIFY(document.removeRunFusion(runId));
    QVERIFY(!document.currentProjectObject().value("event").toObject().value("runs").toArray().first().toObject().contains("fusion"));
    QVERIFY(!document.runRecordings(runId)[1].toMap().contains("fusion"));
    QVERIFY(!document.removeRunFusion(runId));
    // A conflicting logger approved with an explicit rule, then a new primary.
    QVERIFY(document.reviewRunFusion(runId, biasedId));
    QTRY_COMPARE_WITH_TIMEOUT(document.runRecordingReview().value("state").toString(), QString("fusionReview"), 30000);
    QVERIFY(document.approveRunFusion({{"speed", "fillGaps"}}));
    run = document.currentProjectObject().value("event").toObject().value("runs").toArray().first().toObject();
    QCOMPARE(run.value("fusion").toObject().value("rules").toArray().first().toObject().value("rule").toString(), QString("fillGaps"));
    QVERIFY(document.setRunPrimarySource(runId, biasedId));
    QTRY_VERIFY_WITH_TIMEOUT(document.runRecordingReview().isEmpty()
        && document.runRecordings(runId)[2].toMap().value("primary").toBool(), 20000);
    QVERIFY(!document.currentProjectObject().value("event").toObject().value("runs").toArray().first().toObject().contains("fusion"));
}

void TelemetryAppTests::keepsAddingRunsWhileTheDayIsEdited()
{
    // Adding runs to an open day is not made stale by edits that leave its
    // recordings alone -- the analysis writing its verified track inference
    // while laps derive, or a note -- and those edits survive the import.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeFile(first, warpedRouteVbo(true)));
    QVERIFY(writeFile(second, warpedRouteVbo(false)));
    TelemetryController controller(directory.filePath("recovery.json"));
    auto &document = *controller.document();
    auto &analysis = *controller.analysis();
    QSignalSpy committed(&document, &DocumentController::batchImportCommitted);
    QVERIFY(document.importAnalysisRuns("Growing day", {QUrl::fromLocalFile(first)}));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 20000);
    // Immediately, while the first run's laps are still deriving.
    QVERIFY(document.importAnalysisRuns({}, {QUrl::fromLocalFile(second)}));
    // User edits wait for the import; the analysis's own bookkeeping does
    // not. Commit such a change (as the verified inference is written) now.
    const auto runId = document.activeRunId();
    auto project = document.analysisProject();
    auto event = project.value("event").toObject();
    auto runs = event.value("runs").toArray();
    auto run = runs[0].toObject(); run.insert("notes", "Written during the import"); runs[0] = run;
    event.insert("runs", runs); project.insert("event", event);
    document.commitAnalysisProject(project);
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 2, 20000);
    QVERIFY2(document.batchImportError().isEmpty(), qPrintable(document.batchImportError()));
    QCOMPARE(document.eventRuns().size(), 2);
    QCOMPARE(analysis.runMetadata(runId).value("notes").toString(), QString("Written during the import"));
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading(), 20000);
    QCOMPARE(analysis.outingAnalysisStatus().value("state").toString(), QString("ready"));
}

void TelemetryAppTests::measuresAPrivateFullDay()
{
    // KAN-77: opt-in timings and memory for a real day through the Telemetry
    // controller (no editor, no Gui). FLAPPEDEAR_REAL_DAY is a directory of
    // private VBO recordings (never committed). Budgets are in
    // docs/testing.md; they fail here when exceeded.
    const auto path = qEnvironmentVariable("FLAPPEDEAR_REAL_DAY");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_DAY is not set");
    QList<QUrl> recordings;
    qint64 bytes = 0;
    for (const auto &info : QDir(path).entryInfoList({"*.vbo"}, QDir::Files, QDir::Name)) {
        recordings.append(QUrl::fromLocalFile(info.absoluteFilePath()));
        bytes += info.size();
    }
    QVERIFY(!recordings.isEmpty());
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const double baseline = residentMiB();
    qInfo().noquote() << "Hardware:" << hardwareModel() << "·" << QThread::idealThreadCount() << "threads ·"
                      << QSysInfo::prettyProductName();
    qInfo().noquote() << QString("Dataset: %1 recordings, %2 MiB").arg(recordings.size()).arg(bytes / 1048576.0, 0, 'f', 1);
    QElapsedTimer timer;
    struct Phase { QString name; qint64 milliseconds; double resident; double peak; };
    QList<Phase> phases;
    const auto phase = [&](const QString &name) {
        phases.append({name, timer.restart(), residentMiB(), peakResidentMiB()});
        qInfo().noquote() << QString("  %1 %2 ms · resident %3 MiB · peak %4 MiB").arg(name, -28)
            .arg(phases.last().milliseconds, 6).arg(phases.last().resident, 6, 'f', 0).arg(phases.last().peak, 6, 'f', 0);
    };

    TelemetryController controller(directory.filePath("recovery.json"));
    auto &document = *controller.document();
    auto &analysis = *controller.analysis();
    QSignalSpy committed(&document, &DocumentController::batchImportCommitted);
    timer.start();
    QVERIFY(document.importAnalysisRuns("Measured day", recordings));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 120000);
    phase("import (verify, group)");
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading() && !analysis.outingComparisonGroupId().isEmpty(), 120000);
    int laps = 0;
    for (const auto &value : analysis.outingLaps()) laps += value.toMap().value("type") == "LAP";
    phase("lap derivation");

    const auto bestOfDay = analysis.outingRanking().value("bestOfDay").toMap();
    QVERIFY(analysis.selectOutingLapReference(bestOfDay.value("reference").toMap()));
    QTRY_COMPARE_WITH_TIMEOUT(analysis.outingLapDetailState(), QString("ready"), 60000);
    phase("open lap");
    analysis.requestSegmentReview();
    QTRY_COMPARE_WITH_TIMEOUT(analysis.segmentReviewState(), QString("ready"), 60000);
    const auto proposals = analysis.segmentReviewItems().size();
    for (int i = 0; i < proposals; ++i) QCOMPARE(analysis.approveSegmentProposal(i), QString());
    phase("segment review + approve");

    // Cursor interaction on the open lap: what a drag across the charts asks for.
    const auto lap = analysis.selectedOutingLap();
    const double start = lap.value("startTime").toDouble(), end = lap.value("endTime").toDouble();
    constexpr int steps = 1000;
    qint64 slowestLapStep = 0;
    QElapsedTimer step;
    for (int i = 0; i < steps; ++i) {
        step.start();
        analysis.setOutingLapCursor(start + (end - start) * i / (steps - 1));
        (void) analysis.outingLapTrackPoint();
        (void) analysis.outingLapValueText("speed");
        slowestLapStep = std::max(slowestLapStep, step.nsecsElapsed());
    }
    phase("lap cursor x1000");
    analysis.closeOutingLap();

    // A/B: the slowest session's best lap against the best of the day.
    const auto runs = analysis.outingRanking().value("runs").toList();
    QVariantMap slowest;
    for (const auto &value : runs) {
        const auto best = value.toMap().value("bestLap").toMap();
        if (!best.isEmpty() && (slowest.isEmpty() || best.value("durationSeconds").toDouble() > slowest.value("durationSeconds").toDouble()))
            slowest = best;
    }
    QVERIFY(analysis.selectComparisonLap(0, slowest.value("reference").toMap()));
    QVERIFY(analysis.useBestComparisonLap(true));
    QTRY_VERIFY_WITH_TIMEOUT(analysis.comparisonPairReady(), 60000);
    phase("A/B selection");
    const double axis = analysis.comparisonProgressAxisLength();
    QVERIFY(axis > 0);
    qint64 slowestPairStep = 0;
    for (int i = 0; i < steps; ++i) {
        step.start();
        const double at = axis * i / (steps - 1);
        (void) analysis.comparisonPositionAtProgress(0, at);
        (void) analysis.comparisonPositionAtProgress(1, at);
        slowestPairStep = std::max(slowestPairStep, step.nsecsElapsed());
    }
    const auto charts = [&] {
        (void) analysis.comparisonDeltaSeriesByProgress(0, axis, 1200);
        for (int slot = 0; slot < 2; ++slot) (void) analysis.comparisonChannelSeriesByProgress(slot, "speed", 0, axis, 1200);
        (void) analysis.comparisonGgScatter(0, axis, 1200);
    };
    phase("A/B cursor x1000");
    charts();
    phase("A/B charts (delta, speed, G-G)");

    analysis.requestOutingDayReport();
    QTRY_VERIFY_WITH_TIMEOUT(settled(analysis.outingDayReport()), 300000);
    phase("day report");
    const auto saved = directory.filePath("day.fetproject");
    QVERIFY(document.saveProject(QUrl::fromLocalFile(saved)));
    phase("save");

    qInfo().noquote() << QString("Laps %1 · segments %2 · slowest lap-cursor step %3 ms · slowest A/B-cursor step %4 ms · process baseline %5 MiB")
        .arg(laps).arg(proposals).arg(slowestLapStep / 1e6, 0, 'f', 2).arg(slowestPairStep / 1e6, 0, 'f', 2).arg(baseline, 0, 'f', 0);

    // Budgets (docs/testing.md, KAN-77): about 3-4x the measured Mac
    // baseline, and one 60 Hz frame for every cursor step.
    const auto took = [&](const QString &name) {
        for (const auto &item : phases) if (item.name == name) return item.milliseconds;
        return qint64(-1);
    };
    QVERIFY2(slowestLapStep < 16'000'000, "a lap cursor step must fit in one 60 Hz frame");
    QVERIFY2(slowestPairStep < 16'000'000, "an A/B cursor step must fit in one 60 Hz frame");
    QVERIFY2(took("open lap") < 3'000, "opening a lap");
    QVERIFY2(took("A/B selection") < 6'000, "loading an A/B pair");
    QVERIFY2(took("A/B charts (delta, speed, G-G)") < 250, "drawing the A/B charts' data");
    QVERIFY2(took("import (verify, group)") + took("lap derivation") < 30'000, "import to laps");
    QVERIFY2(took("day report") < 30'000, "the day report");
    QVERIFY2(phases.last().peak < 512, "peak memory");
}

QTEST_GUILESS_MAIN(TelemetryAppTests)
#include "TelemetryAppTests.moc"
