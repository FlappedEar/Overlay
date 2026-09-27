#pragma once

#include "gopro/GoProChapters.h"

#include <QFutureWatcher>
#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <atomic>
#include <memory>

namespace FlappedEar {

// KAN-104: reviewing video files as GoPro chapter groups before one is
// used. Each file is probed off the UI thread (cooperatively cancellable,
// guarded by a request number so a stale result never lands), grouped and
// ordered by name, and checked against its metadata. The user can move
// chapters; the group then reports whether the clocks agree with that order.
class VideoChapterReview final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)

public:
    explicit VideoChapterReview(QObject *parent = nullptr);
    ~VideoChapterReview() override;

    [[nodiscard]] QString state() const { return m_state; }  // idle, probing, ready, error
    [[nodiscard]] QString message() const { return m_message; }
    [[nodiscard]] QVariantList groups() const;

    Q_INVOKABLE bool review(const QList<QUrl> &urls);
    Q_INVOKABLE bool moveChapter(int group, int from, int to);
    // Emits groupChosen with the group's files in order, then resets.
    Q_INVOKABLE bool choose(int group);
    Q_INVOKABLE void cancel();

    // A shortcut for callers: one file that is not a GoPro chapter needs no review.
    [[nodiscard]] static bool needsReview(const QList<QUrl> &urls);

signals:
    void changed();
    void groupChosen(const QList<QUrl> &orderedFiles, bool goPro);

private:
    struct ProbeResult {
        quint64 request = 0;
        QVector<ChapterCandidate> candidates;
        bool cancelled = false;
    };
    QString m_state = QStringLiteral("idle");
    QString m_message;
    QVector<ChapterGroup> m_groups;
    quint64 m_request = 0;
    std::shared_ptr<std::atomic_bool> m_cancellation;
    QFutureWatcher<ProbeResult> m_watcher;
};

} // namespace FlappedEar
