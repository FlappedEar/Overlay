#pragma once

#include <QByteArray>
#include <QString>

namespace FlappedEar {

// External programs are untrusted input. Complete payloads fail on overflow;
// diagnostics retain a useful tail and record that earlier bytes were dropped.
class BoundedProcessOutput final {
public:
    enum class Mode { CompletePayload, ByteCountOnly, DiagnosticTail };
    BoundedProcessOutput(Mode mode, qint64 maximumBytes);

    void append(const QByteArray &chunk);
    [[nodiscard]] qint64 observedBytes() const;
    [[nodiscard]] bool exceeded() const;
    [[nodiscard]] bool truncated() const;
    [[nodiscard]] QByteArray bytes() const;
    [[nodiscard]] QString text() const;

private:
    Mode m_mode;
    qint64 m_maximumBytes;
    qint64 m_observedBytes = 0;
    bool m_exceeded = false;
    bool m_truncated = false;
    QByteArray m_bytes;
};

// The end of `text` within `maximumUtf8Bytes` of UTF-8, marked when shortened:
// for an error, the last lines are the ones that explain it.
[[nodiscard]] QString utf8Tail(const QString &text, qint64 maximumUtf8Bytes);

namespace ProcessOutputLimits {
inline constexpr qint64 ffprobeJsonBytes = 4 * 1024 * 1024;
inline constexpr qint64 ffmpegDiagnosticTailBytes = 128 * 1024;
inline constexpr qint64 encoderListingBytes = 2 * 1024 * 1024;
inline constexpr qint64 workerMessageBytes = 128 * 1024;
// A text field inside one worker message, well below workerMessageBytes (KAN-148).
inline constexpr qint64 workerMessageFieldBytes = 32 * 1024;
inline constexpr qint64 ffmpegProgressLineBytes = 16 * 1024;
}

} // namespace FlappedEar
