#pragma once

#include "telemetry/TelemetrySession.h"
#include "telemetry/TelemetrySyncEngine.h"

#include <QFutureWatcher>
#include <QObject>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

class SourceTests;

namespace FlappedEar {

// KAN-215: the editor's video-to-telemetry synchronization. It holds the
// transform (offset and time scale), runs auto-sync on a worker, and keeps the
// candidate a run found until the user applies or ignores it. A timing edit
// while auto-sync runs cancels it and rejects its result. QML reaches it as
// appController.sync.
class SyncController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double offset READ offset WRITE setOffset NOTIFY changed)
    Q_PROPERTY(double timeScale READ timeScale WRITE setTimeScale NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QVariantMap candidate READ candidate NOTIFY candidateChanged)

public:
    // The sources an auto-sync run matched: its result is used only while the
    // editor still shows them.
    struct Sources {
        quint64 generation = 0;
        QString videoPath;  // normalized
        QString vboPath;    // normalized
    };
    using CurrentSources = std::function<Sources()>;

    explicit SyncController(CurrentSources currentSources, QObject *parent = nullptr);

    [[nodiscard]] const SyncTransform &transform() const { return m_transform; }
    [[nodiscard]] double offset() const { return m_transform.offset; }
    [[nodiscard]] double timeScale() const { return m_transform.timeScale; }
    [[nodiscard]] bool running() const { return m_syncWatcher.isRunning(); }
    [[nodiscard]] QVariantMap candidate() const { return m_candidate; }

    // User edits: each rejects a running auto-sync and its candidate, then
    // emits edited() and changed().
    void setOffset(double seconds);
    void setTimeScale(double scale);
    // Starts auto-sync of the video's GoPro GPS against the telemetry.
    void start(const TelemetrySession &telemetry, const Sources &sources);
    void cancel();
    // A transform read from a project; not an edit, and emits nothing: the
    // caller announces it with the rest of the document.
    void restore(const SyncTransform &transform);
    // Forgets the candidate without notifying, for a source that changed.
    void clearCandidate() { m_candidate.clear(); }

    Q_INVOKABLE void applyCandidate();
    Q_INVOKABLE void ignoreCandidate();

signals:
    void changed();
    void runningChanged();
    void candidateChanged();
    // The user changed the transform; the document is dirty.
    void edited();
    void statusMessage(const QString &status);

private:
    friend class ::SourceTests;
    struct AutoSyncResult {
        bool success = false;
        bool cancelled = false;
        QString error;
        SyncCandidate candidate;
        qsizetype packetCount = 0;
        qsizetype gpsSampleCount = 0;
        QString gpsStream;
        quint64 generation = 0;
        quint64 syncRevision = 0;
        QString videoPath;
        QString vboPath;
    };
    void finish();
    void invalidateForTimingEdit();
    [[nodiscard]] static QString candidateLevelName(double confidence);

    CurrentSources m_currentSources;
    SyncTransform m_transform;
    QFutureWatcher<AutoSyncResult> m_syncWatcher;
    quint64 m_syncRevision = 0;
    std::shared_ptr<std::atomic_bool> m_syncCancellation;
    QVariantMap m_candidate;
};

} // namespace FlappedEar
