#include "export/BoundedProcessOutput.h"

#include <QElapsedTimer>
#include <QProcess>

namespace FlappedEar {

BoundedProcessOutput::BoundedProcessOutput(const Mode mode, const qint64 maximumBytes)
    : m_mode(mode), m_maximumBytes(maximumBytes) {}

void BoundedProcessOutput::append(const QByteArray &chunk)
{
    m_observedBytes += chunk.size();
    if (m_mode == Mode::ByteCountOnly) return;
    if (m_mode == Mode::CompletePayload) {
        if (m_bytes.size() + chunk.size() > m_maximumBytes) { m_exceeded = true; return; }
        m_bytes += chunk;
        return;
    }
    m_bytes += chunk;
    if (m_bytes.size() > m_maximumBytes) {
        m_bytes.remove(0, m_bytes.size() - m_maximumBytes);
        m_truncated = true;
    }
}

qint64 BoundedProcessOutput::observedBytes() const { return m_observedBytes; }
bool BoundedProcessOutput::exceeded() const { return m_exceeded; }
bool BoundedProcessOutput::truncated() const { return m_truncated; }
QByteArray BoundedProcessOutput::bytes() const { return m_bytes; }
QString BoundedProcessOutput::text() const { return QString::fromUtf8(m_bytes).trimmed(); }

QString utf8Tail(const QString &text, const qint64 maximumUtf8Bytes)
{
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() <= maximumUtf8Bytes) return text;
    static const QByteArray marker = QByteArrayLiteral("[… earlier output omitted]\n");
    qsizetype start = utf8.size() - std::max<qint64>(0, maximumUtf8Bytes - marker.size());
    // Start on a character boundary, then at the next line when there is one.
    while (start < utf8.size() && (static_cast<unsigned char>(utf8[start]) & 0xC0) == 0x80) ++start;
    const qsizetype line = utf8.indexOf('\n', start);
    if (line >= 0 && line + 1 < utf8.size() && line - start < 4096) start = line + 1;
    return QString::fromUtf8(marker + utf8.sliced(start));
}

bool waitForOutputOrExit(QProcess &process, const int milliseconds)
{
    QElapsedTimer waited;
    waited.start();
    if (process.waitForReadyRead(milliseconds)) return false;
    if (process.state() == QProcess::NotRunning) return true;
    // A closed standard output returns at once and can bring no more bytes, so
    // waiting for the exit cannot grow it.
    if (waited.elapsed() < milliseconds / 2) return process.waitForFinished(milliseconds);
    return false;
}

} // namespace FlappedEar
