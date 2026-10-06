#include "telemetry/TelemetrySessionCache.h"

#include <chrono>
#include <limits>

namespace FlappedEar {
qint64 telemetrySessionMemoryBytes(const TelemetrySession &session)
{
    qint64 bytes = 4096;
    const auto add = [&bytes](qsizetype count, qint64 width) {
        if (count < 0 || count > (std::numeric_limits<qint64>::max() - bytes) / width)
            throw ResourceLimitError("Decoded telemetry memory accounting overflow.");
        bytes += count * width;
    };
    const auto string = [&add](const QString &value) { add(value.capacity(), sizeof(QChar)); add(1, 64); };
    for (auto it = session.channels.cbegin(); it != session.channels.cend(); ++it) {
        add(1, 256); string(it.key()); string(it->name); string(it->unit);
        add(it->timestamps().capacity(), sizeof(double)); add(it->values().capacity(), sizeof(float));
    }
    for (const auto *map : {&session.metadata, &session.aliases})
        for (auto it = map->cbegin(); it != map->cend(); ++it) { add(1, 128); string(it.key()); string(it.value()); }
    for (const auto &warning : session.warnings) string(warning);
    add(session.timingGates.capacity(), sizeof(TimingGate));
    for (const auto &gate : session.timingGates) { string(gate.sourceName); string(gate.sourceDescription); }
    return bytes;
}

TelemetrySessionCache::TelemetrySessionCache(const qint64 limit) : m_budget(std::make_shared<Budget>())
{
    if (limit <= 0 || limit > maximumBytes) throw std::invalid_argument("Invalid analysis memory budget.");
    m_budget->limit = limit;
}
qint64 TelemetrySessionCache::usedBytes() const { return m_budget->used.load(); }
qint64 TelemetrySessionCache::limitBytes() const { return m_budget->limit; }

std::shared_ptr<const TelemetrySession> TelemetrySessionCache::load(const QByteArray &key,
    const CancellationCheck &cancelled, const std::function<TelemetrySession(qint64)> &decode,
    const std::function<void(const TelemetrySession &)> &validate)
{
    // Serialize decoding across A/B and the inspector, with cancellable waiting.
    std::unique_lock lock(m_mutex, std::defer_lock);
    while (!lock.try_lock_for(std::chrono::milliseconds(10))) throwIfCancelled(cancelled);
    throwIfCancelled(cancelled);
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].key != key) continue;
        const auto session = m_entries[i].session;
        try { validate(*session); throwIfCancelled(cancelled); }
        catch (...) { m_entries.removeAt(i); throw; }
        const auto entry = m_entries.takeAt(i); m_entries.prepend(entry);
        return session;
    }
    // Idle entries may be evicted. Pinned sessions remain charged until their
    // last slot/inspector/worker reference is released, even after cache eviction.
    m_entries.removeIf([](const Entry &entry) { return entry.session.use_count() == 1; });
    const qint64 available = m_budget->limit - m_budget->used.load();
    if (available <= 0) throw ResourceLimitError("Analysis memory budget exhausted. Clear an unused lap or close its inspector.");
    auto reservation = std::make_shared<Reservation>(m_budget, available);
    auto decoded = decode(available);
    throwIfCancelled(cancelled);
    const auto cost = telemetrySessionMemoryBytes(decoded);
    if (cost > available) throw ResourceLimitError("Recording exceeds the remaining shared analysis memory budget. Clear an unused lap or close its inspector.");
    // Freeze lazy cadence statistics before the session can be shared by the
    // GUI and another geometry worker. Subsequent presentation reads are const.
    freezeCachedStatistics(decoded, cancelled);
    validate(decoded); // Never cache a parse that failed the post-read identity check.
    throwIfCancelled(cancelled);
    m_budget->used.fetch_sub(available - cost); reservation->bytes = cost;
    std::shared_ptr<const TelemetrySession> session(new TelemetrySession(std::move(decoded)),
        [reservation](const TelemetrySession *value) { delete value; });
    m_entries.prepend({key, session});
    while (m_entries.size() > 2) m_entries.removeLast();
    return session;
}
}
