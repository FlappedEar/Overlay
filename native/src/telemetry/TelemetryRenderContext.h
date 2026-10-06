#pragma once

#include "telemetry/LapTiming.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TrackGeometry.h"
#include "telemetry/TyreData.h"

#include <QObject>
#include <QVariant>
#include <QVariantList>

namespace FlappedEar {

// A render context is deliberately independent from QMediaPlayer. It borrows
// the session and geometry from its owner, and resolves all values at an
// explicit video timestamp through the central SyncTransform.
class TelemetryRenderContext final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double time READ time WRITE setTime NOTIFY timeChanged)
    Q_PROPERTY(QVariantList trackPoints READ trackPoints NOTIFY trackGeometryChanged)
    Q_PROPERTY(quint64 trackRevision READ trackRevision NOTIFY trackGeometryChanged)
    Q_PROPERTY(QVariantMap currentTrackPoint READ currentTrackPoint NOTIFY timeChanged)
    Q_PROPERTY(QVariantMap lapTiming READ lapTiming NOTIFY timeChanged)
    Q_PROPERTY(bool tyreChannelsMissing READ tyreChannelsMissing NOTIFY sourceChanged)

public:
    explicit TelemetryRenderContext(QObject *parent = nullptr);

    [[nodiscard]] double time() const;
    [[nodiscard]] QVariantList trackPoints() const;
    [[nodiscard]] quint64 trackRevision() const;
    [[nodiscard]] quint64 trackConversionCount() const;
    [[nodiscard]] QVariantMap currentTrackPoint() const;
    [[nodiscard]] QVariantMap lapTiming() const;
    [[nodiscard]] const TelemetrySession *session() const;
    [[nodiscard]] SyncTransform syncTransform() const;

    void setSession(const TelemetrySession *session);
    void setTrackGeometry(const TrackGeometry *geometry);
    void setLapSession(const LapSession &lapSession);
    void setSyncTransform(SyncTransform transform);

    Q_INVOKABLE QVariant telemetryValue(const QString &channelName) const;
    Q_INVOKABLE QString valueText(const QString &channelName, int decimals = 2) const;
    // KAN-193: a channel's value `secondsAgo` before the current time, for
    // trails drawn from the recording itself, so preview and export match.
    Q_INVOKABLE QVariant telemetryValueAgo(const QString &channelName, double secondsAgo) const;
    Q_INVOKABLE QVariant telemetryTime() const;
    // Hotlap mode: one chosen lap only. `lapNumber` 0 picks the recording's
    // best eligible lap. {state: "before" | "running" | "finished" |
    // "unavailable", lapNumber, elapsedSeconds, durationSeconds, isBest}:
    // before the lap's start-line crossing the elapsed time is 0, during it
    // runs, and after the lap ends it stays at the lap's final time.
    Q_INVOKABLE QVariantMap fixedLapTiming(int lapNumber) const;
    // KAN-132: {available, corners: [{corner: "FL", hasTemperature,
    // hasPressure, temperature (°C), pressure (bar), pressureSourceUnit}]}
    // at the current time, FL, FR, RL, RR in order. A value is present only
    // when recorded and valid at that time; `available` is false when the
    // recording has no tyre channels at all.
    Q_INVOKABLE QVariantMap tyreValues() const;
    // A recording is loaded and none of its channels is a tyre temperature
    // or pressure, so the tyres widget can only show dashes.
    [[nodiscard]] bool tyreChannelsMissing() const;
    // KAN-149: lap times for the tiles, rounded before minutes are split.
    Q_INVOKABLE QString formatLapTime(double seconds, int decimals) const { return FlappedEar::formatLapTime(seconds, decimals); }

public slots:
    void setTime(double time);

signals:
    void timeChanged();
    void sourceChanged();
    void trackGeometryChanged();
    void syncTransformChanged();

private:
    const TelemetrySession *m_session = nullptr;
    const TrackGeometry *m_geometry = nullptr;
    LapSession m_lapSession;
    TyreChannelMap m_tyreChannels;
    QVariantList m_trackPoints;
    SyncTransform m_sync;
    double m_time = 0.0;
    quint64 m_trackRevision = 0;
    quint64 m_trackConversionCount = 0;
};

} // namespace FlappedEar
