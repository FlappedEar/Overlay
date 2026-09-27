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
