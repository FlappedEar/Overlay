#include "telemetry/TelemetrySource.h"
#include "telemetry/RczParser.h"
#include "telemetry/VboParser.h"
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <algorithm>
namespace FlappedEar {
bool TelemetrySource::supportsPath(const QString &path)
{
    const auto suffix = QFileInfo(path).suffix().toLower();
    return suffix == "vbo" || suffix == "rcz";
}
QByteArray TelemetrySource::contentSha256(const QString &path, const qint64 expectedBytes,
    const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    if (expectedBytes <= 0 || expectedBytes > 128LL * 1024 * 1024)
        throw ResourceLimitError("Telemetry file exceeds the content identity size limit.");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Cannot read telemetry source.");
    }
    if (file.size() != expectedBytes) {
        throw std::runtime_error("Telemetry source changed while reading; retry with a stable file.");
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 remaining = expectedBytes;
    while (remaining > 0) {
        throwIfCancelled(cancelled);
        const QByteArray bytes = file.read(std::min<qint64>(remaining, 64 * 1024));
        if (bytes.isEmpty()) throw std::runtime_error("Telemetry source read failed or was truncated.");
        hash.addData(bytes);
        remaining -= bytes.size();
    }
    throwIfCancelled(cancelled);
    if (file.size() != expectedBytes || !file.atEnd()) {
        throw std::runtime_error("Telemetry source changed while reading; retry with a stable file.");
    }
    return hash.result();
}
TelemetrySession TelemetrySource::load(const QString &path, const CancellationCheck &cancelled, const qint64 maximumDecodedBytes)
{
    throwIfCancelled(cancelled);
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix != "vbo" && suffix != "rcz") throw std::runtime_error("Choose a VBO or RaceChrono RCZ telemetry file.");
    TelemetrySession session = suffix == "vbo" ? VboParser::parseFile(path, cancelled, maximumDecodedBytes)
                                               : RczParser::parseFile(path, cancelled, maximumDecodedBytes);
    // Every value lookup reads the gap threshold (KAN-157): compute it before
    // the session is shared between threads.
    freezeCachedStatistics(session, cancelled);
    return session;
}
}
