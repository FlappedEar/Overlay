#include "app/ApplicationIdentity.h"
#include "app/AppController.h"
#include "app/AppLog.h"
#include "app/GuiSessionLock.h"
#include "app/LegacyStorageMigration.h"
#include "export/TelemetryFrameRenderer.h"
#include "export/ExportEngine.h"
#include "export/ExportFormat.h"
#include "export/ExportDiagnostics.h"
#include "export/ExportProgress.h"
#include "export/ExportOutputTransaction.h"
#include "export/ExportArtifactManifest.h"
#include "telemetry/VboParser.h"
#include "telemetry/TelemetrySource.h"
#include "telemetry/TrackGeometry.h"
#include "telemetry/LapTiming.h"
#include "telemetry/OutingLaps.h"
#include "widgets/WidgetModel.h"

#include <QGuiApplication>
#include <QIcon>
#include <QEvent>
#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QEventLoop>
#include <QMediaPlayer>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <numbers>
#include <memory>

namespace {

FlappedEar::TelemetrySession syntheticTrackSession(const qsizetype pointCount = 30'000)
{
    FlappedEar::TelemetrySession session;
    FlappedEar::TelemetryChannel latitude;
    latitude.name = QStringLiteral("latitude");
    latitude.timestamps.reserve(pointCount);
    latitude.values.reserve(pointCount);
    FlappedEar::TelemetryChannel longitude;
    longitude.name = QStringLiteral("longitude");
    longitude.timestamps.reserve(pointCount);
    longitude.values.reserve(pointCount);
    for (qsizetype index = 0; index < pointCount; ++index) {
        const double progress = static_cast<double>(index) / static_cast<double>(pointCount - 1);
        const double angle = progress * 2.0 * std::numbers::pi;
        const double timestamp = static_cast<double>(index) / 10.0;
        latitude.timestamps.append(timestamp);
        longitude.timestamps.append(timestamp);
        latitude.values.append(static_cast<float>(52.0 + 0.001 * std::sin(angle)));
        longitude.values.append(static_cast<float>(21.0 + 0.0015 * std::cos(angle)));
    }
    session.channels.insert(latitude.name, latitude);
    session.channels.insert(longitude.name, longitude);
    session.aliases.insert(QStringLiteral("latitude"), latitude.name);
    session.aliases.insert(QStringLiteral("longitude"), longitude.name);
    session.sampleCount = pointCount;
    session.startTime = 0.0;
    session.duration = latitude.timestamps.back();
    return session;
}

int renderStill(const QString &path)
{
    const FlappedEar::TelemetrySession session = syntheticTrackSession();
    const FlappedEar::TrackGeometry geometry = FlappedEar::buildTrackGeometry(session);
    FlappedEar::WidgetModel widgets;
    widgets.resetDefaults();
    FlappedEar::TelemetryFrameRenderer renderer;
    if (!renderer.initialize(
            &widgets, &session, &geometry, FlappedEar::SyncTransform{}, QSize(1920, 1080))) {
        qCritical().noquote() << renderer.errorString();
        return EXIT_FAILURE;
    }
    const QImage image = renderer.renderFrame(10.0);
    if (image.isNull() || !image.save(path)) {
        qCritical() << "Could not render telemetry still:" << renderer.errorString();
        return EXIT_FAILURE;
    }
    qInfo().noquote() << QStringLiteral("Telemetry still rendered to %1 (%2×%3).")
                              .arg(path)
                              .arg(image.width())
                              .arg(image.height());
    return EXIT_SUCCESS;
}

int renderVisualSmoke(const QString &path, const bool darkBackground)
{
    QQmlEngine engine;
    QQmlComponent component(
        &engine, QUrl(QStringLiteral("qrc:/qt/qml/FlappedEar/qml/VisualSmokeScene.qml")));
    QQuickItem *scene = qobject_cast<QQuickItem *>(component.createWithInitialProperties(
        {{QStringLiteral("darkBackground"), darkBackground}}));
    if (!scene) {
        qCritical().noquote() << QStringLiteral("Could not create visual smoke scene: %1")
                                      .arg(component.errorString());
        return EXIT_FAILURE;
    }

    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 1920, 1080);
    scene->setParentItem(window.contentItem());
    scene->setSize(window.size());
    window.show();

    bool saved = false;
    QEventLoop eventLoop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
    // VisualSmokeScene uses the same asynchronous Loader path as preview and
    // export. Let that production component tree complete before its capture.
    QTimer::singleShot(200, &window, [&] {
        QQuickItem *telemetryScene = scene->findChild<QQuickItem *>(
            QStringLiteral("visual-smoke-telemetry-scene"));
        if (!telemetryScene) {
            qCritical() << "Visual smoke capture could not find its telemetry scene";
            eventLoop.quit();
            return;
        }
        int visibleWidgetCount = 0;
        for (QQuickItem *item : telemetryScene->childItems()) {
            const bool visible = item->isVisible() && item->opacity() > 0.0
                && item->width() > 0.0 && item->height() > 0.0;
            if (visible)
                ++visibleWidgetCount;
        }
        if (visibleWidgetCount != 9) {
            qCritical() << "Visual smoke capture requires nine visible widget frames; found"
                        << visibleWidgetCount;
            eventLoop.quit();
            return;
        }
        const QSharedPointer<QQuickItemGrabResult> result = scene->grabToImage();
        if (!result) {
            eventLoop.quit();
            return;
        }
        QObject::connect(result.data(), &QQuickItemGrabResult::ready, &eventLoop, [&, result] {
            saved = result->saveToFile(path);
            eventLoop.quit();
        });
    });
    timeout.start(10'000);
    eventLoop.exec();
    delete scene;
    if (!saved) {
        qCritical().noquote() << QStringLiteral("Could not capture visual smoke scene to %1").arg(path);
        return EXIT_FAILURE;
    }
    qInfo().noquote() << QStringLiteral("Visual smoke scene rendered to %1 (1920×1080).")
                              .arg(path);
    return EXIT_SUCCESS;
}

int exportTest(const QString &inputPath, const QString &outputPath)
{
    FlappedEar::ExportOutputTransaction outputTransaction;
    const auto preparation = outputTransaction.prepare(outputPath, inputPath, {}, true);
    if (preparation.status != FlappedEar::ExportOutputTransaction::PreparationStatus::Ready) {
        qCritical().noquote() << (preparation.error.isEmpty()
            ? QStringLiteral("Export target requires explicit overwrite approval.") : preparation.error);
        return EXIT_FAILURE;
    }
    const FlappedEar::TelemetrySession session = FlappedEar::VboParser::parse(
        u"[column names]\ntime speed\n[data]\n0 0\n10 100");
    FlappedEar::WidgetModel widgets;
    widgets.resetDefaults();
    const FlappedEar::MediaInfo input = FlappedEar::MediaProbe::probe(inputPath);
    FlappedEar::TelemetryFrameRenderer renderer;
    if (!renderer.initialize(
            &widgets, &session, nullptr, FlappedEar::SyncTransform{}, input.videoSize)) {
        qCritical().noquote() << renderer.errorString();
        return EXIT_FAILURE;
    }
    FlappedEar::ExportSettings settings;
    settings.inputPath = inputPath;
    settings.outputPath = outputTransaction.stagingPath();
    settings.outputSize = input.videoSize;
    settings.frameRate = FlappedEar::ExportEngine::effectiveFrameRate(input);
    settings.videoBitrate = FlappedEar::ExportFormat::recommendedVideoBitrate(
        settings.outputSize, settings.frameRate, input.bitDepth.value_or(8));
    const FlappedEar::ExportResult result = FlappedEar::ExportEngine::exportVideo(settings, renderer);
    if (!result.success) {
        qCritical().noquote() << result.error;
        if (!result.diagnostics.isEmpty()) qCritical().noquote() << result.diagnostics;
        return EXIT_FAILURE;
    }
    QString commitError;
    if (!result.validationWarning.isEmpty() || !outputTransaction.commit(&commitError)) {
        qCritical().noquote() << (commitError.isEmpty()
            ? QStringLiteral("Export validation did not pass; output was not committed.") : commitError);
        return EXIT_FAILURE;
    }
    if (!result.validationWarning.isEmpty()) {
        qWarning().noquote() << QStringLiteral("Video export completed, but automatic validation failed.");
        qWarning().noquote() << result.validationWarning;
        if (!result.diagnostics.isEmpty()) qWarning().noquote() << result.diagnostics;
    }
    qInfo().noquote() << QStringLiteral(
        "HEVC export completed: %1 submitted / %10 encoded frames in %2 ms (%3 fps); render=%4 ms, polish=%5 ms, "
        "syncRender=%6 ms, readback=%7 ms, cpuCopy=%8 ms, ffmpegWrite=%9 ms, maxQueued=%11 MiB, temporaryOverlay=%12 MiB.")
                             .arg(result.renderedFrames).arg(result.elapsedMilliseconds)
                             .arg(result.renderedFrames * 1000.0 / qMax<qint64>(1, result.elapsedMilliseconds), 0, 'f', 2)
                             .arg(result.renderMilliseconds)
                             .arg(result.polishNanoseconds / 1'000'000)
                             .arg(result.syncRenderNanoseconds / 1'000'000)
                             .arg(result.readbackNanoseconds / 1'000'000)
                             .arg(result.cpuCopyNanoseconds / 1'000'000)
                             .arg(result.ffmpegWriteNanoseconds / 1'000'000)
                             .arg(result.encodedFrames)
                             .arg(result.maximumQueuedBytes / (1024.0 * 1024.0), 0, 'f', 1)
                             .arg(result.temporaryOverlayBytes / (1024.0 * 1024.0), 0, 'f', 1);
    return EXIT_SUCCESS;
}

int benchmarkRender(const QSize size, const int frames)
{
    const FlappedEar::TelemetrySession session = syntheticTrackSession();
    const FlappedEar::TrackGeometry geometry = FlappedEar::buildTrackGeometry(session);
    FlappedEar::WidgetModel widgets;
    widgets.resetDefaults();
    FlappedEar::TelemetryFrameRenderer renderer;
    if (!renderer.initialize(&widgets, &session, &geometry, FlappedEar::SyncTransform{}, size)) {
        qCritical().noquote() << renderer.errorString();
        return EXIT_FAILURE;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    for (int frame = 0; frame < frames; ++frame) {
        if (renderer.renderFrame(static_cast<double>(frame) / 60.0).isNull()) {
            qCritical().noquote() << renderer.errorString();
            return EXIT_FAILURE;
        }
    }
    const auto metrics = renderer.timingMetrics();
    const double milliseconds = elapsed.nsecsElapsed() / 1'000'000.0;
    const auto perFrame = [frames](const qint64 nanoseconds) {
        return nanoseconds / 1'000'000.0 / qMax(1, frames);
    };
    qInfo().noquote() << QStringLiteral(
        "Renderer benchmark: backend=Qt Quick RHI graphicsApi=%1 resolution=%2x%3 frames=%4 "
        "trackPoints=30000 elapsedMs=%5 fps=%6 totalMsPerFrame=%7 polishMsPerFrame=%8 "
        "syncRenderMsPerFrame=%9 readbackMsPerFrame=%10")
                             .arg(renderer.graphicsApiName()).arg(size.width()).arg(size.height())
                             .arg(frames).arg(milliseconds, 0, 'f', 1)
                             .arg(frames * 1000.0 / milliseconds, 0, 'f', 2)
                             .arg(milliseconds / frames, 0, 'f', 2)
                             .arg(perFrame(metrics.polishNanoseconds), 0, 'f', 2)
                             .arg(perFrame(metrics.syncRenderNanoseconds), 0, 'f', 2)
                             .arg(perFrame(metrics.readbackNanoseconds), 0, 'f', 2);
    return EXIT_SUCCESS;
}

void writeExportEvent(const QJsonObject &event)
{
    QTextStream stream(stdout);
    stream << QJsonDocument(event).toJson(QJsonDocument::Compact) << '\n' << Qt::flush;
}

int exportWorker(const QString &configPath)
{
    QFile configFile(configPath);
    if (!configFile.open(QIODevice::ReadOnly)) {
        writeExportEvent({{"state", "failed"}, {"error", "Could not open export configuration."}});
        return EXIT_FAILURE;
    }
    const QJsonObject config = QJsonDocument::fromJson(configFile.readAll()).object();
    const QString supervisionReadyPath = config.value("supervisionReadyPath").toString();
    if (!supervisionReadyPath.isEmpty()) {
        QElapsedTimer supervisionWait;
        supervisionWait.start();
        while (!QFileInfo::exists(supervisionReadyPath) && supervisionWait.elapsed() < 15'000) {
            const QString cancellationPath = config.value("cancelPath").toString();
            if (!cancellationPath.isEmpty() && QFileInfo::exists(cancellationPath)) {
                writeExportEvent({{"state", "cancelled"}, {"message", "Export cancelled before worker release."}});
                return EXIT_SUCCESS;
            }
            QThread::msleep(25);
        }
        if (!QFileInfo::exists(supervisionReadyPath)) {
            writeExportEvent({{"state", "failed"}, {"error", "Export worker supervision was not established."}});
            return EXIT_FAILURE;
        }
    }
    QElapsedTimer elapsed;
    elapsed.start();
    FlappedEar::ExportStageTimer stageTimer;
    stageTimer.start(0, QStringLiteral("preparing"));
    QString currentOperation = QStringLiteral("prepareTelemetryScene");
    QString currentMessage = QStringLiteral("Preparing telemetry scene");
    const auto stageDurationsJson = [&] {
        QJsonObject durations;
        const auto values = stageTimer.completedStageDurations();
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            durations.insert(it.key(), it.value());
        }
        return durations;
    };
    const auto emitEvent = [&](QJsonObject event) {
        const QString state = event.value("state").toString();
        if (!state.isEmpty() && state != stageTimer.stage()) {
            stageTimer.transition(elapsed.elapsed(), state);
        }
        if (event.contains("operation")) currentOperation = event.value("operation").toString();
        if (event.value("type").toString() != QStringLiteral("log") && event.contains("message")) {
            currentMessage = event.value("message").toString();
        }
        event.insert("state", stageTimer.stage());
        event.insert("operation", currentOperation);
        event.insert("currentOperation", currentMessage);
        event.insert("stageElapsedMilliseconds", stageTimer.stageElapsedMilliseconds(elapsed.elapsed()));
        event.insert("totalElapsedMilliseconds", stageTimer.totalElapsedMilliseconds(elapsed.elapsed()));
        event.insert("elapsedMilliseconds", stageTimer.totalElapsedMilliseconds(elapsed.elapsed()));
        event.insert("timestampMilliseconds", stageTimer.totalElapsedMilliseconds(elapsed.elapsed()));
        event.insert("stageDurations", stageDurationsJson());
        writeExportEvent(event);
    };
    const auto emitProbeEvent = [&](const FlappedEar::MediaProbeEvent &probe) {
        QJsonObject details{{"mode", probe.mode}, {"target", probe.targetPath},
                            {"probeElapsedMilliseconds", probe.elapsedMilliseconds}};
        QString message;
        if (probe.phase == FlappedEar::MediaProbeEvent::Phase::Started) {
            details.insert("executable", probe.executable);
            details.insert("arguments", QJsonArray::fromStringList(probe.arguments));
            message = QStringLiteral("ffprobe started");
        } else if (probe.phase == FlappedEar::MediaProbeEvent::Phase::Heartbeat) {
            message = QStringLiteral("ffprobe running · %1 s")
                          .arg(probe.elapsedMilliseconds / 1000.0, 0, 'f', 1);
        } else {
            details.insert("exitCode", probe.exitCode);
            message = QStringLiteral("ffprobe exited · code %1").arg(probe.exitCode);
        }
        emitEvent({{"type", "log"}, {"level", "debug"}, {"component", "ffprobe"},
                   {"message", message}, {"details", details}});
    };
    try {
        emitEvent({{"type", "log"}, {"level", "info"}, {"component", "export"},
                   {"message", "Export worker started"}});
        const QString cancellationPath = config.value("cancelPath").toString();
        const auto cancelled = [cancellationPath] {
            return !cancellationPath.isEmpty() && QFileInfo::exists(cancellationPath);
        };
        currentOperation = QStringLiteral("parseTelemetry");
        currentMessage = QStringLiteral("Reading telemetry data");
        emitEvent({{"type", "status"}, {"operation", currentOperation}, {"message", currentMessage}});
        const auto telemetryPath = config.value("vboPath").toString();
        const auto binding = config.value("lapBinding").toObject();
        const auto exclusions = config.value("lapExclusions");
        if (!FlappedEar::validLapExclusions(exclusions, binding.value("eventId").toString()))
            throw std::runtime_error("Export lap exclusions are invalid.");
        const auto sourceSize = QFileInfo(telemetryPath).size();
        const auto contentRevision = binding.isEmpty() ? QByteArray{}
            : FlappedEar::TelemetrySource::contentSha256(telemetryPath, sourceSize, cancelled).toHex();
        if (!binding.isEmpty() && contentRevision != binding.value("sourceRevision").toString().toLatin1())
            throw std::runtime_error("Recording changed since preview; reload before exporting.");
        const FlappedEar::TelemetrySession session = FlappedEar::TelemetrySource::load(telemetryPath, cancelled);
        FlappedEar::WidgetModel widgets;
        if (!widgets.fromJson(config.value("widgets").toArray())) {
            writeExportEvent({{"state", "failed"}, {"error", "Widget scene is invalid."}});
            return EXIT_FAILURE;
        }
        currentOperation = QStringLiteral("buildTrackGeometry");
        currentMessage = QStringLiteral("Preparing track geometry");
        emitEvent({{"type", "status"}, {"operation", currentOperation}, {"message", currentMessage}});
        const FlappedEar::TrackGeometry geometry = FlappedEar::buildTrackGeometry(session, cancelled);
        currentOperation = QStringLiteral("deriveLapTiming");
        currentMessage = QStringLiteral("Deriving lap timing");
        emitEvent({{"type", "status"}, {"operation", currentOperation}, {"message", currentMessage}});
        auto lapSession = FlappedEar::deriveSourceLapSession(session, {}, cancelled);
        if (!binding.isEmpty() && FlappedEar::TelemetrySource::contentSha256(telemetryPath, sourceSize, cancelled).toHex() != contentRevision)
            throw std::runtime_error("Recording changed during export preparation.");
        FlappedEar::applyLapExclusions(lapSession, binding, exclusions.toArray());
        const QJsonObject syncJson = config.value("sync").toObject();
        const FlappedEar::SyncTransform sync{
            syncJson.value("offset").toDouble(), syncJson.value("timeScale").toDouble(1.0)};
        currentOperation = QStringLiteral("probeInput");
        currentMessage = QStringLiteral("Reading input metadata with ffprobe");
        emitEvent({{"type", "status"}, {"operation", currentOperation}, {"message", currentMessage}});
        const FlappedEar::MediaInfo input = FlappedEar::MediaProbe::probe(
            config.value("inputPath").toString(), {}, false, -1, emitProbeEvent, cancelled);
        emitEvent({{"type", "log"}, {"level", "info"}, {"component", "ffprobe"},
                   {"message", "Input probed"},
                   {"details", QJsonObject{{"codec", input.videoCodec},
                                            {"width", input.videoSize.width()},
                                            {"height", input.videoSize.height()},
                                            {"duration", input.duration}}}});
        currentOperation = QStringLiteral("initializeRenderer");
        currentMessage = QStringLiteral("Preparing telemetry scene");
        emitEvent({{"type", "status"}, {"operation", currentOperation}, {"message", currentMessage}});
        FlappedEar::TelemetryFrameRenderer renderer;
        const QSize outputSize(config.value("outputWidth").toInt(input.videoSize.width()),
                               config.value("outputHeight").toInt(input.videoSize.height()));
        if (!renderer.initialize(&widgets, &session, &geometry, sync, outputSize, &lapSession)) {
            writeExportEvent({{"state", "failed"}, {"error", renderer.errorString()}});
            return EXIT_FAILURE;
        }
        emitEvent({{"type", "log"}, {"level", "info"}, {"component", "renderer"},
                   {"message", "Renderer initialized"},
                   {"details", QJsonObject{{"width", outputSize.width()},
                                            {"height", outputSize.height()}}}});
        FlappedEar::ExportSettings settings;
        settings.inputPath = config.value("inputPath").toString();
        settings.outputPath = config.value("outputPath").toString();
        settings.encoder = config.value("encoder").toString();
        settings.outputSize = outputSize;
        settings.frameRate = {config.value("frameRateNumerator").toInteger(), config.value("frameRateDenominator").toInteger(1)};
        if (!settings.frameRate.isValid()) settings.frameRate = FlappedEar::ExportEngine::effectiveFrameRate(input);
        settings.frameRange = {config.value("firstFrame").toInteger(),
                               config.value("lastFrame").toInteger(-1)};
        if (!settings.frameRange.isValid()) {
            const auto fullRange = FlappedEar::ExportEngine::fullVideoFrameRange(input, settings.frameRate);
            if (!fullRange) {
                emitEvent({{"type", "log"}, {"state", "failed"}, {"level", "error"},
                           {"operation", "resolveSourceFrameDomain"},
                           {"message", "Could not determine the usable source video-frame domain"}});
                return EXIT_FAILURE;
            }
            settings.frameRange = *fullRange;
        }
        settings.videoBitrate = config.value("videoBitrate").toInteger();
        if (settings.videoBitrate <= 0) {
            settings.videoBitrate = FlappedEar::ExportFormat::recommendedVideoBitrate(
                outputSize, settings.frameRate, input.bitDepth.value_or(8));
        }
        settings.audioEnabled = config.value("audioEnabled").toBool(true);
        settings.cancellationFilePath = config.value("cancelPath").toString();
        settings.temporaryOverlayPath = config.value("temporaryOverlayPath").toString();
        settings.manifestPath = config.value("manifestPath").toString();
        const double sourceRangeStart = FlappedEar::ExportEngine::exportRelativeTime(
            static_cast<qsizetype>(settings.frameRange.firstFrame), settings.frameRate);
        const double sourceRangeEnd = FlappedEar::ExportEngine::exportRelativeTime(
            static_cast<qsizetype>(settings.frameRange.lastFrame + 1), settings.frameRate);
        const double exportDuration = sourceRangeEnd - sourceRangeStart;
        const qsizetype expectedFrames = static_cast<qsizetype>(settings.frameRange.frameCount());
        FlappedEar::ExportProgressEstimator rendererProgress;
        qint64 lastUpdate = -125;
        emitEvent({{"type", "status"}, {"state", "preparing"},
                          {"operation", "prepareTelemetryScene"},
                          {"message", "Preparing telemetry scene"},
                          {"sourceRangeStart", sourceRangeStart},
                          {"sourceRangeEnd", sourceRangeEnd}, {"exportDuration", exportDuration},
                          {"sourceVideoTime", sourceRangeStart},
                          {"exportRelativeTime", 0.0},
                          {"expectedFrames", static_cast<qint64>(expectedFrames)},
                          {"width", outputSize.width()}, {"height", outputSize.height()},
                          {"frameRate", settings.frameRate.value()}, {"audioEnabled", settings.audioEnabled}});
        settings.progressCallback = [&](const FlappedEar::ExportPipelineProgress &pipeline) {
            const qint64 elapsedMilliseconds = elapsed.elapsed();
            if (pipeline.submittedFrames != pipeline.expectedFrames
                && elapsedMilliseconds - lastUpdate < 125) return;
            lastUpdate = elapsedMilliseconds;
            const auto rendererSnapshot = rendererProgress.update(
                pipeline.submittedFrames, pipeline.expectedFrames, elapsedMilliseconds, settings.frameRate);
            const double encodedPercent = FlappedEar::FfmpegProgressParser::overallPercent(
                pipeline.encodedSeconds, pipeline.outputDurationSeconds);
            const double overlayPercent = pipeline.expectedFrames > 0
                ? 60.0 * double(pipeline.submittedFrames) / double(pipeline.expectedFrames) : 0.0;
            const double visiblePercent = pipeline.stage == QStringLiteral("renderingOverlay") ? overlayPercent
                : pipeline.stage == QStringLiteral("encodingVideo") ? 60.0 + 0.35 * encodedPercent
                : pipeline.stage == QStringLiteral("finalizing") ? 97.0 : encodedPercent;
            const QString eventStage = pipeline.stage == QStringLiteral("renderingOverlay")
                ? QStringLiteral("renderingOverlay") : QStringLiteral("encodingVideo");
            const QString operation = pipeline.stage == QStringLiteral("renderingOverlay")
                ? QStringLiteral("renderTelemetryOverlay")
                : pipeline.stage == QStringLiteral("finalizing")
                    ? QStringLiteral("flushOutputContainer") : QStringLiteral("encodeFinalVideo");
            const QString operationMessage = pipeline.stage == QStringLiteral("renderingOverlay")
                ? QStringLiteral("Rendering telemetry overlay")
                : pipeline.stage == QStringLiteral("finalizing")
                    ? QStringLiteral("Flushing MP4 container")
                    : QStringLiteral("Encoding final HEVC video");
            const auto telemetryTime = FlappedEar::videoToTelemetryTime(pipeline.sourceVideoTime, sync);
            QJsonObject event{{"type", "progress"}, {"state", eventStage},
                              {"operation", operation}, {"message", operationMessage},
                              {"renderedFrames", static_cast<qint64>(pipeline.submittedFrames)},
                              {"generatedFrames", static_cast<qint64>(pipeline.generatedFrames)},
                              {"expectedFrames", static_cast<qint64>(pipeline.expectedFrames)},
                              {"sourceRangeStart", pipeline.sourceRangeStart},
                              {"sourceRangeEnd", pipeline.sourceRangeEnd},
                              {"exportDuration", pipeline.exportDuration},
                              {"exportRelativeTime", pipeline.exportRelativeTime},
                              {"sourceVideoTime", pipeline.sourceVideoTime},
                              {"telemetryTime", telemetryTime ? QJsonValue(*telemetryTime) : QJsonValue(QJsonValue::Null)},
                              {"encodedFrames", static_cast<qint64>(pipeline.encodedFrames)},
                              {"encodedSeconds", pipeline.encodedSeconds},
                              {"encodedProgress", encodedPercent},
                              {"encoderFps", pipeline.encoderFps},
                              {"encoderRealtimeFactor", pipeline.encoderRealtimeFactor},
                              {"queuedBytes", pipeline.queuedBytes},
                              {"maximumQueuedBytes", pipeline.maximumQueuedBytes},
                              {"temporaryOverlayBytes", pipeline.temporaryOverlayBytes},
                              {"visibleProgress", visiblePercent},
                              {"outputBytes", QFileInfo(settings.outputPath).size()}};
            if (rendererSnapshot.etaAvailable) {
                event.insert("rendererFps", rendererSnapshot.throughputFps);
            }
            emitEvent(event);
        };
        settings.stateCallback = [](const QString &) {};
        settings.encoderCallback = [&](const QString &id, const QString &name) {
            emitEvent({{"type", "log"}, {"state", "preparing"}, {"level", "info"},
                       {"component", "encoder"}, {"message", "Final encoder selected"},
                       {"encoderId", id}, {"encoderName", name},
                       {"details", QJsonObject{{"id", id}, {"name", name}}}});
        };
        settings.observationCallback = [&](const FlappedEar::ExportObservation &observation) {
            QJsonObject event{{"type", observation.type}, {"state", observation.state},
                              {"operation", observation.operation},
                              {"message", observation.message},
                              {"component", observation.component}};
            if (!observation.details.isEmpty()) {
                event.insert("details", QJsonObject::fromVariantMap(observation.details));
                if (observation.component == QStringLiteral("storage")) {
                    for (auto it = observation.details.cbegin(); it != observation.details.cend(); ++it) {
                        event.insert(it.key(), QJsonValue::fromVariant(it.value()));
                    }
                }
            }
            emitEvent(event);
        };
        const FlappedEar::ExportResult result = FlappedEar::ExportEngine::exportVideo(settings, renderer);
        if (result.cancelled) {
            emitEvent({{"type", "log"}, {"state", "cancelled"}, {"level", "warning"},
                       {"operation", "cancelled"}, {"message", "Export cancelled"}});
            return EXIT_SUCCESS;
        }
        if (!result.success) {
            const auto failure = FlappedEar::describeExportFailure(result.error, result.diagnostics);
            emitEvent({{"type", "log"}, {"state", "failed"}, {"level", "error"},
                       {"operation", "failed"}, {"message", "Export failed"},
                       {"error", failure.error}, {"diagnostics", failure.diagnostics}});
            return EXIT_FAILURE;
        }
        const QString completionState = result.validationWarning.isEmpty()
            ? QStringLiteral("complete") : QStringLiteral("validationWarning");
        emitEvent({{"type", "log"}, {"state", completionState},
                   {"level", result.validationWarning.isEmpty() ? "info" : "warning"},
                   {"operation", "complete"},
                   {"component", "export"},
                   {"message", result.validationWarning.isEmpty()
                       ? QStringLiteral("Export complete")
                       : QStringLiteral("Export completed with validation warning")}});
        emitEvent({{"type", "status"}, {"state", completionState},
                          {"operation", "complete"}, {"message", "Export complete"},
                          {"warning", result.validationWarning},
                          {"diagnostics", result.diagnostics},
                          {"renderedFrames", static_cast<qint64>(result.renderedFrames)},
                          {"generatedFrames", static_cast<qint64>(result.generatedFrames)},
                          {"expectedFrames", static_cast<qint64>(result.expectedFrames)},
                          {"sourceFrameCount", result.sourceFrameCount},
                          {"firstFrame", result.firstFrame}, {"lastFrame", result.lastFrame},
                          {"finalFrameCount", result.finalFrameCount},
                          {"frameDeficit", result.frameDeficit},
                          {"resultClassification", result.validationWarning.isEmpty() ? "Success" : "SuccessWithWarning"},
                          {"elapsedMilliseconds", result.elapsedMilliseconds},
                          {"renderMilliseconds", result.renderMilliseconds},
                          {"renderNanoseconds", result.renderNanoseconds},
                          {"polishNanoseconds", result.polishNanoseconds},
                          {"syncRenderNanoseconds", result.syncRenderNanoseconds},
                          {"readbackNanoseconds", result.readbackNanoseconds},
                          {"cpuCopyNanoseconds", result.cpuCopyNanoseconds},
                          {"ffmpegWriteNanoseconds", result.ffmpegWriteNanoseconds},
                          {"maximumQueuedBytes", result.maximumQueuedBytes},
                          {"temporaryOverlayBytes", result.temporaryOverlayBytes},
                          {"outputBytes", result.outputBytes},
                          {"encodedFrames", static_cast<qint64>(result.encodedFrames)},
                          {"encodedSeconds", result.encodedSeconds},
                          {"exportFrameRateNumerator", result.exportFrameRate.numerator},
                          {"exportFrameRateDenominator", result.exportFrameRate.denominator},
                          {"exportFrameRate", result.exportFrameRate.value()},
                          {"outputVideoCodec", result.mediaInfo.videoCodec},
                          {"outputVideoProfile", result.mediaInfo.videoCodecProfile},
                          {"outputPixelFormat", result.mediaInfo.pixelFormat},
                          {"outputBitDepth", result.mediaInfo.bitDepth
                              ? QJsonValue(*result.mediaInfo.bitDepth) : QJsonValue(QJsonValue::Null)},
                          {"outputColorRange", result.mediaInfo.colorRange},
                          {"outputColorSpace", result.mediaInfo.colorSpace},
                          {"outputColorTransfer", result.mediaInfo.colorTransfer},
                          {"outputColorPrimaries", result.mediaInfo.colorPrimaries},
                          {"outputWidth", result.mediaInfo.videoSize.width()},
                          {"outputHeight", result.mediaInfo.videoSize.height()},
                          {"outputDuration", result.mediaInfo.duration},
                          {"outputVideoDuration", result.mediaInfo.videoDuration},
                          {"outputVideoStart", result.mediaInfo.videoStartTime},
                          {"outputVideoPacketCount", static_cast<qint64>(result.mediaInfo.videoPacketCount)},
                          {"outputAverageFrameRate", result.mediaInfo.averageFrameRate.value()},
                          {"outputAudioCodecs", result.mediaInfo.audioCodecs.join(", ")},
                          {"outputAudioStart", result.mediaInfo.audioStartTime},
                          {"outputAudioDuration", result.mediaInfo.audioDuration}});
        return EXIT_SUCCESS;
    } catch (const FlappedEar::OperationCancelled &) {
        emitEvent({{"type", "log"}, {"state", "cancelled"}, {"level", "warning"},
                   {"operation", "cancelled"}, {"message", "Export cancelled"}});
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        emitEvent({{"type", "log"}, {"state", "failed"}, {"level", "error"},
                   {"operation", "failed"}, {"message", "Export failed"},
                   {"error", QString::fromUtf8(error.what())}});
        return EXIT_FAILURE;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    const bool renderStillMode = argc == 3 && QString::fromLocal8Bit(argv[1]) == "--render-still";
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{};
    const bool renderVisualSmokeMode = argc == 3 && command == "--render-visual-smoke";
    const bool renderVisualSmokeDarkMode = argc == 3 && command == "--render-visual-smoke-dark";
    const bool exportTestMode = argc == 4 && QString::fromLocal8Bit(argv[1]) == "--export-test";
    const bool exportWorkerMode = argc == 3 && QString::fromLocal8Bit(argv[1]) == "--export-worker";
    const bool benchmarkRenderMode = argc == 5 && QString::fromLocal8Bit(argv[1]) == "--benchmark-render";
    const bool startupSmokeMode = argc == 2 && QString::fromLocal8Bit(argv[1]) == "--startup-smoke";
    if (qEnvironmentVariableIntValue("FLAPPEDEAR_EXPORT_SOFTWARE") == 1) {
        qputenv("QT_QUICK_BACKEND", "software");
    }
    FlappedEar::AppLog::installQtMessageHandler();
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    FlappedEar::ApplicationIdentity::initialize();
    const bool applicationMode = !renderStillMode && !renderVisualSmokeMode && !renderVisualSmokeDarkMode
        && !exportTestMode && !exportWorkerMode
        && !benchmarkRenderMode && !startupSmokeMode;
    std::unique_ptr<FlappedEar::GuiSessionLock> guiSessionLock;
    FlappedEar::LegacyStorageMigration::Result storageMigration;
    if (applicationMode) {
        guiSessionLock = std::make_unique<FlappedEar::GuiSessionLock>();
        QString startupError;
        if (!guiSessionLock->tryAcquire(&startupError)) {
            qCritical().noquote() << startupError;
            QQmlApplicationEngine errorEngine;
            errorEngine.setInitialProperties({{QStringLiteral("message"), startupError}});
            errorEngine.loadFromModule("FlappedEar", "StartupError");
            if (!errorEngine.rootObjects().isEmpty()) app.exec();
            return EXIT_FAILURE;
        }
        // KAN-125: bring the previous storage identity's preferences, templates,
        // recovery snapshot and logs across before anything uses the new one.
        storageMigration = FlappedEar::LegacyStorageMigration::migrateFrom(
            QString::fromLatin1(FlappedEar::ApplicationIdentity::legacyStorageName));
        static_cast<void>(FlappedEar::AppLog::initialize());
        FlappedEar::AppLog::info(QStringLiteral("Application startup"));
        FlappedEar::AppLog::info(
            QStringLiteral("Log file: %1").arg(FlappedEar::AppLog::filePath()));
        if (storageMigration.attempted) {
            FlappedEar::AppLog::info(
                QStringLiteral("Storage migration from \"%1\": preferences %2, %3 item(s) moved")
                    .arg(QString::fromLatin1(FlappedEar::ApplicationIdentity::legacyStorageName),
                         storageMigration.settingsCopied ? QStringLiteral("copied") : QStringLiteral("not copied"))
                    .arg(storageMigration.moved.size()));
            for (const QString &path : storageMigration.moved)
                FlappedEar::AppLog::info(QStringLiteral("Moved: %1").arg(path));
            for (const QString &warning : storageMigration.warnings) FlappedEar::AppLog::warn(warning);
        }
    }
    if (!exportWorkerMode) {
        QStringList janitorDiagnostics;
        const QStringList recovered = FlappedEar::ExportArtifactManifest::recoverStale(&janitorDiagnostics);
        for (const QString &path : recovered) qInfo().noquote() << QStringLiteral("Recovered owned stale export artifacts: %1").arg(path);
        for (const QString &message : janitorDiagnostics) qWarning().noquote() << message;
    }
    app.setWindowIcon(QIcon(QStringLiteral(":/flappedear/resources/branding/app-logo.png")));
    if (renderStillMode) {
        return renderStill(QString::fromLocal8Bit(argv[2]));
    }
    if (renderVisualSmokeMode) {
        return renderVisualSmoke(QString::fromLocal8Bit(argv[2]), false);
    }
    if (renderVisualSmokeDarkMode) {
        return renderVisualSmoke(QString::fromLocal8Bit(argv[2]), true);
    }
    if (exportTestMode) {
        return exportTest(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    if (exportWorkerMode) {
        return exportWorker(QString::fromLocal8Bit(argv[2]));
    }
    if (benchmarkRenderMode) {
        bool widthOk = false;
        bool heightOk = false;
        bool framesOk = false;
        const int width = QString::fromLocal8Bit(argv[2]).toInt(&widthOk);
        const int height = QString::fromLocal8Bit(argv[3]).toInt(&heightOk);
        const int frames = QString::fromLocal8Bit(argv[4]).toInt(&framesOk);
        if (!widthOk || !heightOk || !framesOk || width <= 0 || height <= 0 || frames <= 0) {
            qCritical() << "Usage: --benchmark-render <width> <height> <frames>";
            return EXIT_FAILURE;
        }
        return benchmarkRender(QSize(width, height), frames);
    }
    if (startupSmokeMode) {
        QStandardPaths::setTestModeEnabled(true);
        QSettings settings;
        settings.clear();
        settings.sync();
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        FlappedEar::AppLog::info(QStringLiteral("Application shutdown"));
    });
    int result = EXIT_FAILURE;
    {
        FlappedEar::AppController controller;
        if (!storageMigration.warnings.isEmpty())
            controller.setStartupNotice(storageMigration.warnings.join(QStringLiteral("\n\n")));
        if (startupSmokeMode) {
            // Instantiate every newly added renderer under the normal QML application
            // path. The widgets intentionally have no telemetry here: this also checks
            // their no-data rendering contract.
            FlappedEar::WidgetModel *widgets = controller.widgetModel();
            widgets->addWidget(QStringLiteral("f1GForceRadar"));
            widgets->addWidget(QStringLiteral("gForceMagnitudeBar"));
            const int automaticGear = widgets->addWidget(QStringLiteral("retroGear"));
            const int explicitGear = widgets->addWidget(QStringLiteral("retroGear"));
            widgets->setSetting(automaticGear, QStringLiteral("fontSize"), 0);
            widgets->setSetting(explicitGear, QStringLiteral("fontSize"), 42);
        }
        // KAN-136: segments are created automatically for a layout that has none.
        controller.setAutomaticSegments(true);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appController", &controller);
        QObject::connect(
            &engine,
            &QQmlApplicationEngine::objectCreationFailed,
            &app,
            [] {
                FlappedEar::AppLog::error(QStringLiteral("Main QML failed to load"));
                QCoreApplication::exit(EXIT_FAILURE);
            },
            Qt::QueuedConnection);
        engine.loadFromModule("FlappedEar", "Main");
        if (startupSmokeMode && !engine.rootObjects().isEmpty()) {
            QObject *root = engine.rootObjects().first();
            QObject *about = root->findChild<QObject *>(QStringLiteral("productAboutDialog"));
            const QString productName = QString::fromLatin1(FlappedEar::ApplicationIdentity::displayName);
            if (root->property("title").toString() != productName || !about
                || about->property("title").toString() != QStringLiteral("About %1").arg(productName)
                || !QMetaObject::invokeMethod(about, "open")) {
                qCritical() << "Startup smoke failed: product title or About dialog is invalid";
                return EXIT_FAILURE;
            }
            QCoreApplication::processEvents();
            if (!about->property("visible").toBool()) {
                qCritical() << "Startup smoke failed: About dialog did not open";
                return EXIT_FAILURE;
            }
            QMetaObject::invokeMethod(about, "close");
            if (!engine.rootObjects().first()->property("welcomeVisible").toBool()
                || !engine.rootObjects().first()->findChild<QObject *>(QStringLiteral("welcomeLapAnalysis"))) {
                qCritical() << "Startup smoke failed: fresh launch must offer Lap Analysis on the welcome screen";
                return EXIT_FAILURE;
            }
            // Load the all-in-one broadcast composition as well as the normal
            // application window. This catches QML binding/Loader regressions
            // across the widget family in one deterministic scene.
            engine.loadFromModule("FlappedEar", "VisualSmokeScene");
            engine.setInitialProperties({{QStringLiteral("message"), QStringLiteral("Startup guard smoke")}});
            engine.loadFromModule("FlappedEar", "StartupError");
        }
        if (!engine.rootObjects().isEmpty()) {
            FlappedEar::AppLog::info(QStringLiteral("Main QML loaded"));
        }
        if (startupSmokeMode) {
            if (engine.rootObjects().size() < 3) {
                qCritical() << "Startup smoke failed: broadcast HUD composition did not load";
                result = EXIT_FAILURE;
            } else {
                QObject *root = engine.rootObjects().constFirst();
            QCoreApplication::processEvents();
            const qsizetype closedPlayers = root->findChildren<QMediaPlayer *>().size();
            controller.setAnalysisVisible(true);
            QCoreApplication::processEvents();
            const qsizetype openPlayers = root->findChildren<QMediaPlayer *>().size();
            controller.setAnalysisVisible(false);
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            const qsizetype releasedPlayers = root->findChildren<QMediaPlayer *>().size();
            if (closedPlayers != 1 || openPlayers != 2 || releasedPlayers != 1) {
                qCritical().noquote() << QStringLiteral(
                    "Startup smoke failed: media players closed=%1 open=%2 released=%3")
                                             .arg(closedPlayers).arg(openPlayers).arg(releasedPlayers);
                result = EXIT_FAILURE;
            } else {
                qInfo() << "Startup smoke passed: Analysis decoder lifecycle is lazy and broadcast HUD composition loaded";
                result = EXIT_SUCCESS;
            }
            }
        } else {
            result = app.exec();
        }
    }
    FlappedEar::AppLog::restoreQtMessageHandler();
    FlappedEar::AppLog::shutdown();
    return result;
}
