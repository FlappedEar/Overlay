#pragma once

#include "telemetry/SourceOperation.h"
#include "telemetry/TelemetrySession.h"

#include <QByteArray>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace FlappedEar {

// Conservative decoded-source accounting. Shared channel buffers are counted
// per channel; parser input/decompression scratch retains its separate limits.
[[nodiscard]] qint64 telemetrySessionMemoryBytes(const TelemetrySession &session);

class TelemetrySessionCache final {
public:
    static constexpr qint64 maximumBytes = 256LL * 1024 * 1024;
    explicit TelemetrySessionCache(qint64 limit = maximumBytes);
    [[nodiscard]] qint64 usedBytes() const;
    [[nodiscard]] qint64 limitBytes() const;
    [[nodiscard]] std::shared_ptr<const TelemetrySession> load(const QByteArray &key,
        const CancellationCheck &cancelled,
        const std::function<TelemetrySession(qint64)> &decode,
        const std::function<void(const TelemetrySession &)> &validate);

private:
    struct Budget {
        qint64 limit;
        std::atomic<qint64> used{0};
    };
    struct Reservation {
        std::shared_ptr<Budget> budget;
        qint64 bytes;
        Reservation(std::shared_ptr<Budget> owner, qint64 amount)
            : budget(std::move(owner)), bytes(amount) { budget->used.fetch_add(bytes); }
        ~Reservation() { budget->used.fetch_sub(bytes); }
    };
    struct Entry {
        QByteArray key;
        std::shared_ptr<const TelemetrySession> session;
    };
    std::shared_ptr<Budget> m_budget;
    std::mutex m_mutex;
    QList<Entry> m_entries; // Most recently used first; at most two cache entries.
};
}
