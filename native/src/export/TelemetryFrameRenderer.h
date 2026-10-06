#pragma once

#include "telemetry/TelemetryRenderContext.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QtTypes>
#include <memory>

class QQmlEngine;
class QQuickItem;
class QQuickWindow;
class WidgetTests;

namespace FlappedEar {

class WidgetModel;

struct RendererCapabilityResult {
    bool supported = false;
    QSize requestedSize;
    int maximumTextureSize = 0;
    qint64 pixelCount = 0;
    qint64 frameBytes = 0;
    QString backend;
    QString error;
};

// Owns a private Qt Quick scene. All methods must be called on its owning GUI
// thread; export workers may consume the returned QImage, but never move the
// QML/scene-graph objects themselves.
class TelemetryFrameRenderer final {
    friend class ::WidgetTests;

public:
    struct TimingMetrics {
        qsizetype frames = 0;
        qint64 polishNanoseconds = 0;
        qint64 syncRenderNanoseconds = 0;
        qint64 readbackNanoseconds = 0;
    };

    TelemetryFrameRenderer();
    ~TelemetryFrameRenderer();

    TelemetryFrameRenderer(const TelemetryFrameRenderer &) = delete;
    TelemetryFrameRenderer &operator=(const TelemetryFrameRenderer &) = delete;

    [[nodiscard]] bool initialize(
        WidgetModel *widgetModel,
        const TelemetrySession *session,
        const TrackGeometry *geometry,
        SyncTransform sync,
        QSize outputSize,
        const LapSession *lapSession = nullptr);
    [[nodiscard]] QImage renderFrame(double sourceVideoTime);
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] QString graphicsApiName() const;
    [[nodiscard]] RendererCapabilityResult capability() const;
    [[nodiscard]] TimingMetrics timingMetrics() const;
    [[nodiscard]] static RendererCapabilityResult evaluateCapability(
        const QSize &size, int maximumTextureSize, const QString &backend);
    [[nodiscard]] static bool readbackRequiresVerticalFlip(bool yUpInFramebuffer);

private:
    class Impl;

    TelemetryRenderContext m_context;
    QSize m_outputSize;
    QString m_error;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<Impl> m_impl;
    QQuickItem *m_rootItem = nullptr;
};

} // namespace FlappedEar
