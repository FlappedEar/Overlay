#pragma once

#include "export/MediaProbe.h"
#include "project/AdditionalVideos.h"

#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <memory>

class SourceTests;

namespace FlappedEar {

// KAN-131: the run's videos besides the main one, such as a helmet camera
// without GPS. Each has its own manual sync (telemetry time = video time x
// time scale + offset); export places them by the layout. A saved video is
// probed in the background and accepted only when its fingerprint matches;
// a missing or changed file stays in the project, marked, never dropped.
// Probe results are bound to the document generation and to the request, so
// a result for a replaced document or an older request is ignored. QML
// reaches it as appController.additionalVideos.
class AdditionalVideoController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList videos READ videoList NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int maximum READ maximum CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString layout READ layout WRITE setLayout NOTIFY changed)

public:
    // The main video's normalized path and its sync, for duplicate checks,
    // the default sync of a new video and the alignment aid.
    struct MainVideo {
        QString path;
        SyncTransform sync;
    };
    using CurrentMainVideo = std::function<MainVideo()>;

    explicit AdditionalVideoController(CurrentMainVideo mainVideo, QObject *parent = nullptr);
    ~AdditionalVideoController() override;

    // The videos as saved; runtime state is not part of them.
    [[nodiscard]] QVector<AdditionalVideo> videos() const;
    // What export composes: every video, by its checked file. Empty, with
    // `problem` saying which video and why, when one is not ready.
    struct ExportVideo {
        QString id;
        QString path;
        QString label;
        SyncTransform sync;
    };
    [[nodiscard]] QVector<ExportVideo> exportVideos(QString *problem) const;
    [[nodiscard]] VideoLayoutMode layoutMode() const { return m_layout.mode; }
    // The whole layout, picture-in-picture options and camera switching
    // included (KAN-245). Setting a different one is an edit.
    [[nodiscard]] const VideoLayout &videoLayout() const { return m_layout; }
    void setVideoLayout(const VideoLayout &layout);
    [[nodiscard]] QVariantList videoList() const;
    [[nodiscard]] int count() const { return static_cast<int>(m_entries.size()); }
    [[nodiscard]] static int maximum() { return AdditionalVideosCodec::maximumAdditionalVideos; }
    [[nodiscard]] bool loading() const;
    [[nodiscard]] QString layout() const;
    void setLayout(const QString &layout);

    // Videos read from a project (not an edit, emits changed() only): each
    // one found is probed and checked against its fingerprint.
    void restore(const QVector<AdditionalVideo> &videos, VideoLayoutMode layout, const QString &projectPath);
    void restore(const QVector<AdditionalVideo> &videos, const VideoLayout &layout, const QString &projectPath);
    void clear();
    // After a save: the references as written (relative paths rebased).
    void updateReferences(const QVector<AdditionalVideo> &saved);

    Q_INVOKABLE void addVideo(const QUrl &url);
    Q_INVOKABLE void removeVideo(int index);
    Q_INVOKABLE void relinkVideo(int index, const QUrl &url);
    Q_INVOKABLE void setLabel(int index, const QString &label);
    Q_INVOKABLE void setOffset(int index, double seconds);
    Q_INVOKABLE void setTimeScale(int index, double scale);
    // The alignment aid: a moment seen at mainVideoSeconds in the main video
    // and at videoSeconds in this one is the same telemetry time. Keeps the
    // time scale and sets the offset.
    Q_INVOKABLE void alignAt(int index, double mainVideoSeconds, double videoSeconds);
    // Where this video is when the main video is at mainVideoSeconds.
    Q_INVOKABLE double videoSecondsFor(int index, double mainVideoSeconds) const;
    // Seconds of this video per second of the main video, for playback.
    Q_INVOKABLE double playbackRateFor(int index) const;
    // The preview's rectangles in a frame of width x height (the main video's
    // shape), main video first, by the same rules as export.
    Q_INVOKABLE QVariantList previewRects(double width, double height) const;

signals:
    void changed();
    // The user changed the list, a sync or the layout; the document is dirty.
    void edited();
    void statusMessage(const QString &status);

private:
    friend class ::SourceTests;
    struct Entry {
        AdditionalVideo video;
        QString path;           // resolved, normalized; empty when missing
        QString state;          // loading, ready, missing, mismatch, error
        QString stateBeforeRelink;
        QString problem;
        MediaInfo mediaInfo;
        quint64 request = 0;
    };
    struct ProbeResult {
        bool success = false;
        bool cancelled = false;
        QString error;
        MediaInfo mediaInfo;
        QJsonObject fingerprint;
    };
    enum class ProbePurpose { Restore, Add, Relink };

    // The saved fingerprint is checked when the result arrives.
    void probe(const QString &id, const QString &path, ProbePurpose purpose);
    void finishProbe(const QString &id, quint64 generation, quint64 request, const QString &path,
        ProbePurpose purpose, const ProbeResult &result);
    [[nodiscard]] qsizetype indexOf(const QString &id) const;
    [[nodiscard]] bool validIndex(int index) const { return index >= 0 && index < m_entries.size(); }
    [[nodiscard]] QString unusedId() const;
    [[nodiscard]] bool pathInUse(const QString &path, qsizetype except) const;
    void cancelProbes();

    CurrentMainVideo m_mainVideo;
    QVector<Entry> m_entries;
    VideoLayout m_layout;
    quint64 m_generation = 0;
    quint64 m_nextRequest = 0;
    int m_pendingAdds = 0;
    std::shared_ptr<std::atomic_bool> m_cancellation = std::make_shared<std::atomic_bool>(false);
};

} // namespace FlappedEar
