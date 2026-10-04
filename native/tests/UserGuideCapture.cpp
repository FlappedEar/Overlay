#include "UserGuideCapture.h"

#include "app/AppController.h"
#include "widgets/WidgetModel.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSettings>
#include <QtTest>

#include <cmath>
#include <functional>
#include <memory>

namespace FlappedEar {
namespace {

QQuickItem *findItem(QQuickItem *root, const std::function<bool(QQuickItem *)> &match, bool visibleOnly = true)
{
    if (!root || (visibleOnly && !root->isVisible())) return nullptr;
    if (match(root)) return root;
    for (auto *child : root->childItems())
        if (auto *found = findItem(child, match, visibleOnly)) return found;
    return nullptr;
}

// A QML component's instances are named "<Type>_QMLTYPE_<n>".
QQuickItem *byType(QQuickItem *root, const char *type, bool visibleOnly = true)
{
    return findItem(root, [&](QQuickItem *item) {
        return QString::fromLatin1(item->metaObject()->className()).startsWith(QLatin1String(type) + "_QML");
    }, visibleOnly);
}

QQuickItem *byText(QQuickItem *root, const QString &text)
{
    return findItem(root, [&](QQuickItem *item) { return item->property("text").toString() == text; });
}

class Shooter {
public:
    explicit Shooter(QString directory) : m_directory(std::move(directory)) {}

    bool window(QQuickWindow *target, const QString &name, const QRect &windowCrop = {}, int settleMs = 700)
    {
        QTest::qWait(settleMs);
        QImage image = target->grabWindow();
        if (image.isNull()) return false;
        const qreal ratio = image.width() / qreal(target->width());
        if (!windowCrop.isNull())
            image = image.copy(QRectF(windowCrop.topLeft() * ratio, windowCrop.size() * ratio).toAlignedRect());
        return save(image, name);
    }
    // `item` re-rendered at `size`, cropped to `area` (item coordinates).
    bool item(QQuickItem *source, const QRectF &area, const QSize &size, const QString &name, int settleMs = 700)
    {
        QTest::qWait(settleMs);
        const qreal scale = size.width() / area.width();
        auto result = source->grabToImage(QSize(qRound(source->width() * scale), qRound(source->height() * scale)));
        if (!result) return false;
        bool ready = false;
        QObject::connect(result.data(), &QQuickItemGrabResult::ready, [&ready] { ready = true; });
        if (!QTest::qWaitFor([&] { return ready; }, 10000)) return false;
        QImage image = result->image().copy(QRectF(area.topLeft() * scale, area.size() * scale).toAlignedRect());
        image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (!crop.isNull()) image = image.copy(crop);
        return save(image, name);
    }
    QRect crop; // optional, in output pixels, for the next item() grab

private:
    bool save(const QImage &image, const QString &name)
    {
        const QString path = QDir(m_directory).filePath(name + ".png");
        const bool saved = image.save(path);
        qInfo().noquote() << (saved ? "captured" : "FAILED") << name << image.size();
        return saved;
    }
    QString m_directory;
};

} // namespace

void captureUserGuide(const UserGuideCaptureOptions &options)
{
    QSettings settings; settings.clear(); settings.sync();
    Shooter shoot(options.outputDirectory);
    AppController controller(nullptr, QDir(options.scratchDirectory).filePath("recovery.json"));
    controller.setAutomaticSegments(true); // as the application does (KAN-136)
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QDir(QStringLiteral(QML_SOURCE_DIR)).filePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window);
    window->resize(1440, 860);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(shoot.window(window, "welcome"));

    // Lap Analysis before anything is imported.
    const auto analysisWindow = [&]() -> QQuickWindow * {
        for (auto *candidate : object->findChildren<QQuickWindow *>())
            if (candidate != window && candidate->isVisible()) return candidate;
        return nullptr;
    };
    controller.setAnalysisVisible(true);
    QTRY_VERIFY(analysisWindow());
    analysisWindow()->resize(1440, 900);
    QVERIFY(shoot.window(analysisWindow(), "analysis-start", {}, 1200));
    controller.setAnalysisVisible(false);
    QTRY_VERIFY(!analysisWindow());

    // The day's recordings through Import runs, as a driver adds them.
    QList<QUrl> day;
    for (const auto &path : options.day) day << QUrl::fromLocalFile(path);
    if (day.isEmpty()) day << QUrl::fromLocalFile(options.recording);
    QVERIFY(controller.beginBatchImport(day));
    QTRY_COMPARE_WITH_TIMEOUT(controller.batchImportState(), QStringLiteral("review"), 120000);
    auto *batch = window->findChild<QObject *>("batchImportDialog");
    QVERIFY(batch);
    QVERIFY(QMetaObject::invokeMethod(batch, "open"));
    QTRY_VERIFY(batch->property("opened").toBool());
    auto *eventName = window->findChild<QObject *>("batchEventName");
    QVERIFY(eventName);
    eventName->setProperty("text", QStringLiteral("Jastrząb, 29 August"));
    QVERIFY(shoot.window(window, "import-runs"));
    QSignalSpy committed(&controller, &AppController::batchImportCommitted);
    QVERIFY(QMetaObject::invokeMethod(batch, "submit"));
    QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 120000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.vboLoadState() == "ready" && !controller.outingLapsLoading(), 180000);
    // Automatic segments (KAN-136) are created in the background; the video is
    // opened at once, as a driver does (KAN-142 keeps the segments).
    // The run of the onboard video becomes the active one; its video is
    // opened and synchronized as in the editor.
    const int runIndex = std::max(0, int(options.day.indexOf(options.recording)));
    const auto runs = controller.eventRuns();
    QVERIFY(runIndex < runs.size());
    const QString runId = runs[runIndex].toMap().value("id").toString();
    if (controller.activeRunId() != runId) {
        QVERIFY(controller.selectEventRun(runId));
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeRunId(), runId, 60000);
    }
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QStringLiteral("ready"), 60000);
    QCOMPARE(controller.telemetryName(), QFileInfo(options.recording).fileName());
    window->setProperty("welcomeVisible", false);
    controller.loadVideo(QUrl::fromLocalFile(options.video));
    QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QStringLiteral("ready"), 60000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.playbackTime() > 0.0, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(!window->property("previewPrimeFramePending").toBool(), 30000);
    // Auto Sync is refused until the video's GPS has been read; retry until it starts.
    bool started = false;
    for (int attempt = 0; attempt < 30 && !started; ++attempt) {
        controller.autoSync();
        started = QTest::qWaitFor([&] { return controller.syncing() || !controller.syncCandidate().isEmpty(); }, 2000);
    }
    QVERIFY2(started, "Auto Sync did not start");
    QTRY_VERIFY_WITH_TIMEOUT(!controller.syncing() && !controller.syncCandidate().isEmpty(), 300000);
    const QVariantMap sync = controller.syncCandidate();
    qInfo().noquote() << QString("sync offset %1 s · correlation %2 · confidence %3 · applied %4")
        .arg(controller.syncOffset(), 0, 'f', 3).arg(sync.value("correlation").toDouble(), 0, 'f', 4)
        .arg(sync.value("confidence").toDouble(), 0, 'f', 2).arg(sync.value("automaticallyApplied").toBool());
    QVERIFY(sync.value("automaticallyApplied").toBool());

    // Halfway into the run's best lap.
    QVariantMap best;
    for (const auto &value : controller.lapSummaries())
        if (value.toMap().value("isBest").toBool()) best = value.toMap();
    QVERIFY(!best.isEmpty());
    const auto videoSecondsAt = [&](double telemetry) {
        return (telemetry - controller.syncOffset()) / controller.timeScale();
    };
    const auto seek = [&](double seconds) {
        QMetaObject::invokeMethod(window, "seekTimeline", Q_ARG(QVariant, seconds * 1000.0));
        return QTest::qWaitFor([&] { return std::abs(controller.playbackTime() - seconds) < 0.25; }, 30000);
    };
    const double moment = videoSecondsAt(best.value("startTelemetryTime").toDouble() + 38.0);
    QVERIFY(seek(moment));
    // The values the captions quote.
    auto *context = controller.renderContext();
    qInfo().noquote() << QString("moment %1 s into %2 (LAP %3): speed %4 km/h · rpm %5 · throttle %6 % · brake %7 % · heart rate %8")
        .arg(38).arg(controller.telemetryName()).arg(best.value("number").toInt())
        .arg(context->valueText("speed", 1), context->valueText("rpm", 0), context->valueText("throttle", 0),
             context->valueText("brake", 0), context->valueText("heartRate", 0));
    QVERIFY(shoot.window(window, "editor-sync-result", {}, 1500));
    QVERIFY(shoot.window(window, "editor-window"));

    auto *inspector = byType(window->contentItem(), "InspectorPanel");
    QVERIFY(inspector);
    const QRect inspectorArea = inspector->mapRectToScene(inspector->boundingRect()).toAlignedRect();
    inspector->setProperty("currentTab", 1);
    QVERIFY(shoot.window(window, "editor-inspector-data", {inspectorArea.x(), 0, inspectorArea.width(), window->height()}));
    window->setProperty("selectedWidgetIndex", 0);
    inspector->setProperty("currentTab", 2);
    QVERIFY(shoot.window(window, "editor-inspector-cues", {inspectorArea.x(), 0, inspectorArea.width(), window->height()}));
    inspector->setProperty("currentTab", 0);
    window->setProperty("selectedWidgetIndex", -1);

    // Each built-in template over the onboard picture, at 1280x720.
    auto *gap = findItem(window->contentItem(), [](QQuickItem *item) { return item->objectName() == "videoChapterGap"; }, false);
    QVERIFY(gap && gap->parentItem() && gap->parentItem()->parentItem());
    QQuickItem *viewport = gap->parentItem(), *stage = viewport->parentItem();
    const QRectF viewportArea = viewport->mapRectToItem(stage, viewport->boundingRect());
    for (const QString id : {"track-day", "minimal", "broadcast", "motorsport-broadcast-smoke", "performance",
                              "circuit-pro", "endurance", "drag-strip", "clean-hud", "2000s-grand-prix"}) {
        QVERIFY2(controller.widgetModel()->applyTemplate(id), qPrintable(id));
        QVERIFY(shoot.item(stage, viewportArea, {1280, 720}, "template-" + id, 1200));
    }

    // Each widget alone over the onboard picture, on a 1920x1080 stage, framed
    // with a 28 px margin (the gallery of the Widgets page).
    struct GalleryWidget {
        GalleryWidget(const char *type, const char *name, double width = 0, double height = 0, QVariantMap settings = {})
            : type(type), name(name), width(width), height(height), settings(std::move(settings)) {}
        const char *type, *name;
        double width, height; // 0: the widget's default size
        QVariantMap settings;
    };
    const QList<GalleryWidget> gallery{
        {"speed", "widget-speed"}, {"rpm", "widget-rpm", 0.22}, {"heartRate", "widget-heart-rate"},
        {"pedals", "widget-pedals"}, {"gForce", "widget-gforce"}, {"f1GForceRadar", "widget-f1-radar"},
        {"gForceMagnitudeBar", "widget-gforce-bar"}, {"track", "widget-track", 0.20, 0.30},
        {"customValue", "widget-custom", 0, 0, {{"source", "engine_oil_temp-obd"}, {"label", "OIL"}, {"unit", "°C"}, {"decimals", 0}}},
        {"tyres", "widget-tyres"},
        {"retroCustomValue", "widget-retro-custom", 0, 0.06, {{"source", "coolant_temp-obd"}, {"label", "COOLANT"}, {"unit", "°C"}, {"decimals", 0}}},
        {"arcGauge", "widget-arc-gauge"}, {"dialGauge", "widget-dial-gauge"}, {"telemetryOverlay", "widget-data-strip"},
        {"lapBest", "widget-lap-best"}, {"lapCurrent", "widget-lap-current"}, {"lapDelta", "widget-lap-delta"},
        {"speedBest", "widget-speed-best"}, {"speedCurrent", "widget-speed-current"}, {"speedDelta", "widget-speed-delta"},
        {"retroTachometer", "widget-retro-rpm"}, {"retroGear", "widget-retro-gear", 0.16, 0.06},
        {"retroPedal", "widget-retro-pedal", 0.16, 0.06, {{"showValue", true}}},
        {"retroSpeedArc", "widget-retro-speed"}, {"retroNameplate", "widget-nameplate"}, {"brandLogo", "widget-logo"}};
    auto *model = controller.widgetModel();
    for (const auto &entry : gallery) {
        while (model->count() > 0) model->removeWidget(model->count() - 1);
        const int index = model->addWidget(QString::fromLatin1(entry.type));
        QVERIFY2(index >= 0, entry.type);
        const QVariantMap added = model->widget(index);
        const double width = entry.width > 0 ? entry.width : added.value("width").toDouble();
        const double height = entry.height > 0 ? entry.height : added.value("height").toDouble();
        model->resizeWidget(index, width, height);
        model->moveWidget(index, 0.5 - width / 2, 0.62 - height / 2);
        for (auto it = entry.settings.cbegin(); it != entry.settings.cend(); ++it) model->setSetting(index, it.key(), it.value());
        constexpr int margin = 28;
        shoot.crop = QRect(qRound((0.5 - width / 2) * 1920) - margin, qRound((0.62 - height / 2) * 1080) - margin,
                           qRound(width * 1920) + 2 * margin, qRound(height * 1080) + 2 * margin);
        QVERIFY(shoot.item(stage, viewportArea, {1920, 1080}, QString::fromLatin1(entry.name), 1000));
        shoot.crop = {};
    }
    QVERIFY(controller.widgetModel()->applyTemplate("track-day"));

    window->setProperty("fullScreenPreview", true);
    window->setProperty("fullScreenControlsVisible", true);
    QVERIFY(shoot.window(window, "editor-fullscreen", {}, 1200));
    window->setProperty("fullScreenPreview", false);
    window->setProperty("fullScreenControlsVisible", false);

    // Export: the dialog, then a short real export of this moment.
    auto *exportDialog = window->findChild<QObject *>("exportDialog");
    QVERIFY(exportDialog);
    window->resize(1440, 1040);
    QVERIFY(QMetaObject::invokeMethod(exportDialog, "open"));
    QTRY_VERIFY(exportDialog->property("opened").toBool());
    // Single lap · hotlap, on the run's best lap.
    auto *rangeMode = window->findChild<QObject *>("exportRangeMode");
    QVERIFY(rangeMode);
    rangeMode->setProperty("currentIndex", 2);
    QVERIFY(QMetaObject::invokeMethod(rangeMode, "activated", Q_ARG(int, 2)));
    QTRY_VERIFY(exportDialog->property("singleLapRange").toMap().value("valid").toBool());
    qInfo() << "export single lap" << exportDialog->property("singleLapRange").toMap();
    QVERIFY(shoot.window(window, "export-dialog", {}, 1200));
    QVERIFY(QMetaObject::invokeMethod(exportDialog, "close"));
    QTRY_VERIFY(!exportDialog->property("visible").toBool());
    window->resize(1440, 860);
    QTest::qWait(500);

    // Timecodes count 60 nominal frames per second at 59.94 fps, as the export dialog does.
    const auto timecode = [](qint64 frame) {
        return QString::asprintf("%02lld:%02lld:%02lld:%02lld", frame / 216000, frame / 3600 % 60, frame / 60 % 60, frame % 60);
    };
    const qint64 firstFrame = qRound64(moment * 60000.0 / 1001.0);
    // A neutral output path: the guide shows it in the export details.
    QDir().mkpath(QStringLiteral("/tmp/FlappedEar"));
    const QString target = QStringLiteral("/tmp/FlappedEar/jastrzab-session-5.mp4");
    QFile::remove(target);
    QVERIFY(controller.startExport(QUrl::fromLocalFile(target), 1920, 1080, 60000, 1001,
        controller.recommendedExportBitrate(1920, 1080, 60000, 1001, QStringLiteral("high")),
        true, true, timecode(firstFrame), timecode(firstFrame + 8 * 60)));
    qInfo() << "export started" << controller.exportState() << timecode(firstFrame);
    QQuickItem *overlayRoot = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    const auto finished = [&] { return controller.exportState() == "complete" || controller.exportState() == "failed"; };
    QVERIFY(QTest::qWaitFor([&] {
        return controller.exportProgressInfo().value("progressPercent").toDouble() >= 25.0 || finished();
    }, 240000));
    QVERIFY(shoot.window(window, "export-progress", {}, 100));
    auto *details = byText(overlayRoot, QStringLiteral("Details"));
    QVERIFY(details);
    details->setProperty("checked", true);
    QVERIFY(shoot.window(window, "export-details", {}, 300));
    auto *verbose = byText(overlayRoot, QStringLiteral("Very verbose"));
    QVERIFY(verbose);
    verbose->setProperty("checked", true);
    QVERIFY(shoot.window(window, "export-verbose", {}, 300));
    verbose->setProperty("checked", false);
    details->setProperty("checked", false);
    QTRY_VERIFY_WITH_TIMEOUT(finished(), 600000);
    qInfo() << "export state" << controller.exportState();
    QCOMPARE(controller.exportState(), QStringLiteral("complete"));
    QVERIFY(shoot.window(window, "export-complete", {}, 800));
    controller.dismissExportProgress();
    QFile::remove(target);

    // Lap Analysis of the day, with this run's onboard video.
    controller.requestOutingDayReport();
    const auto settled = [&] {
        if (controller.outingLapsLoading()) return false;
        for (const auto &value : controller.outingDayReport().value("results").toList()) {
            const auto status = value.toMap().value("status").toString();
            if (status == "notComputed" || status == "computing" || status == "stale") return false;
        }
        return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(settled(), 300000);
    controller.setAnalysisVisible(true);
    QTRY_VERIFY(analysisWindow());
    QQuickWindow *analysis = analysisWindow();
    analysis->resize(1440, 900);
    QVERIFY(QTest::qWaitForWindowExposed(analysis));
    QVERIFY(shoot.window(analysis, "analysis-day-results", {}, 1500));

    // The run's best lap and its next-best lap.
    int bestIndex = -1;
    QVariantMap bestLap, otherLap;
    const auto rows = controller.outingLaps();
    for (int index = 0; index < rows.size(); ++index) {
        const auto row = rows[index].toMap();
        if (row.value("type") != "LAP" || row.value("runId") != runId) continue;
        if (row.value("lapNumber").toInt() == best.value("number").toInt()) { bestIndex = index; bestLap = row; }
    }
    for (const auto &value : rows) {
        const auto row = value.toMap();
        if (row.value("type") != "LAP" || row.value("runId") != runId || row == bestLap
            || !row.value("comparisonEligible").toBool() || row.value("excluded").toBool()) continue;
        if (otherLap.isEmpty() || row.value("durationSeconds").toDouble() < otherLap.value("durationSeconds").toDouble())
            otherLap = row;
    }
    QVERIFY(bestIndex >= 0 && !otherLap.isEmpty());
    QVERIFY(controller.selectOutingLap(bestIndex));
    QTRY_COMPARE_WITH_TIMEOUT(controller.outingLapDetailState(), QStringLiteral("ready"), 60000);
    // Play the moment briefly: the lap view's cursor follows the video only while it plays.
    QVERIFY(seek(moment - 2.0));
    QVERIFY(QMetaObject::invokeMethod(window, "togglePlayback"));
    QTest::qWait(2000);
    QVERIFY(QMetaObject::invokeMethod(window, "togglePlayback"));
    QVERIFY(shoot.window(analysis, "analysis-lap-detail", {}, 1500));
    auto *detail = byType(analysis->contentItem(), "OutingLapDetailPanel");
    QVERIFY(detail);
    detail->setProperty("reviewingSegments", true);
    QTRY_COMPARE_WITH_TIMEOUT(controller.segmentReviewState(), QStringLiteral("ready"), 60000);
    QVERIFY(shoot.window(analysis, "analysis-segment-review", {}, 1200));
    detail->setProperty("reviewingSegments", false);
    detail->setProperty("showingCoasting", true);
    QVERIFY(shoot.window(analysis, "analysis-coasting", {}, 1500));
    detail->setProperty("showingCoasting", false);
    controller.closeOutingLap();
    QTest::qWait(500);

    for (const auto &[name, shot] : {std::pair{"runDetailsDialog", "analysis-run-details"},
                                     std::pair{"outingTrackConfigurationDialog", "analysis-correct-grouping"}}) {
        auto *dialog = analysis->findChild<QObject *>(name);
        QVERIFY2(dialog, name);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(shoot.window(analysis, shot, {}, 1000));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }

    // A/B: the run's best lap against its next best, both with video.
    QVERIFY(controller.selectComparisonLap(0, bestLap.value("reference").toMap()));
    QVERIFY(controller.selectComparisonLap(1, otherLap.value("reference").toMap()));
    QTRY_VERIFY_WITH_TIMEOUT(controller.comparisonPairReady(), 60000);
    controller.setComparisonViewOpen(true);
    QTRY_COMPARE_WITH_TIMEOUT(controller.comparisonVideo(0).value("state").toString(), QStringLiteral("ready"), 60000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.comparisonVideo(1).value("state").toString(), QStringLiteral("ready"), 60000);
    auto *comparison = byType(analysis->contentItem(), "ComparisonDetailPanel");
    QVERIFY(comparison);
    comparison->setProperty("hoverDistanceMeters", controller.comparisonProgressAxisLength() * 0.32);
    qInfo() << "comparison cursor" << controller.comparisonProgressAxisLength() * 0.32 << "m";
    QVERIFY(shoot.window(analysis, "analysis-comparison", {}, 1500));
    comparison->setProperty("showingVideo", true);
    QVERIFY(shoot.window(analysis, "analysis-comparison-video", {}, 3000));
    comparison->setProperty("showingVideo", false);
    comparison->setProperty("showingCornerAnalyzer", true);
    QVERIFY(shoot.window(analysis, "analysis-corner-analyzer", {}, 1500));
    comparison->setProperty("showingCornerAnalyzer", false);
    comparison->setProperty("showingGg", true);
    QVERIFY(shoot.window(analysis, "analysis-gg", {}, 1500));
    comparison->setProperty("showingGg", false);
    comparison->setProperty("hoverDistanceMeters", -1.0);
    controller.setComparisonViewOpen(false);
    QTest::qWait(500);

    for (const auto &[name, shot] : {std::pair{"theoreticalBestDialog", "analysis-theoretical-best"},
                                     std::pair{"timeLossDialog", "analysis-time-losses"},
                                     std::pair{"dayReportDialog", "analysis-day-report"}}) {
        auto *dialog = analysis->findChild<QObject *>(name);
        QVERIFY2(dialog, name);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        // Opening a result recomputes what is stale (segments were just created).
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 300000);
        QVERIFY(shoot.window(analysis, shot, {}, 2000));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    auto *progression = analysis->findChild<QObject *>("outingProgressionDialog");
    auto *tabs = analysis->findChild<QObject *>("progressionTabs");
    QVERIFY(progression && tabs);
    QVERIFY(QMetaObject::invokeMethod(progression, "open"));
    QTRY_VERIFY(progression->property("opened").toBool());
    const QStringList tabNames{"laps", "sections", "car-driver"};
    for (int tab = 0; tab < tabNames.size(); ++tab) {
        tabs->setProperty("currentIndex", tab);
        QVERIFY(shoot.window(analysis, "analysis-progression-" + tabNames[tab], {}, 1500));
    }
    QVERIFY(QMetaObject::invokeMethod(progression, "close"));
    controller.setAnalysisVisible(false);
    QTRY_VERIFY(!analysisWindow());

    // A GoPro recording split into chapters, on the run it belongs to.
    if (options.chapters.size() >= 2) {
        const int chapterRun = int(runs.size()) - 1; // the day's last session
        const QString chapterRunId = runs[chapterRun].toMap().value("id").toString();
        QVERIFY(controller.selectEventRun(chapterRunId));
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeRunId(), chapterRunId, 60000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QStringLiteral("ready"), 60000);
        // As Open video does with several files: review them as chapter groups first.
        QList<QUrl> files;
        for (const auto &path : options.chapters) files << QUrl::fromLocalFile(path);
        QVERIFY(controller.videoFilesNeedReview(files));
        auto *review = controller.property("videoChapters").value<QObject *>();
        QVERIFY(review);
        bool reviewing = false;
        QVERIFY(QMetaObject::invokeMethod(review, "review", Q_RETURN_ARG(bool, reviewing), Q_ARG(QList<QUrl>, files)));
        QVERIFY(reviewing);
        QTRY_VERIFY_WITH_TIMEOUT(!review->property("groups").toList().isEmpty() && review->property("state").toString() != "probing", 120000);
        qInfo() << "chapter review" << review->property("state").toString() << review->property("message").toString();
        auto *chaptersDialog = window->findChild<QObject *>("videoChaptersDialog");
        QVERIFY(chaptersDialog);
        QVERIFY(QMetaObject::invokeMethod(chaptersDialog, "open"));
        QTRY_VERIFY(chaptersDialog->property("opened").toBool());
        QVERIFY(shoot.window(window, "video-chapters", {}, 1500));
        QVERIFY(QMetaObject::invokeMethod(chaptersDialog, "close"));
    }
}

} // namespace FlappedEar
