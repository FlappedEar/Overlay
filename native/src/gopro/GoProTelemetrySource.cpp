#include "gopro/GoProTelemetrySource.h"
#include "export/BoundedProcessOutput.h"
#include "export/FfmpegTools.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace FlappedEar {
namespace {

// A KLV record viewed in place: key and data point into the packet, which
// outlives the decode, so nesting never copies a payload (KAN-197).
struct Record {
    QByteArrayView key;
    char type = 0;
    int size = 0;
    int repeat = 0;
    QByteArrayView data;
};

struct GpsSample {
    double time = 0.0;
    float latitude = 0.0F;
    float longitude = 0.0F;
    float speedKmh = 0.0F;
};

struct ProbePacket {
    qint64 position = 0;
    qint64 size = 0;
    double pts = 0.0;
    double duration = 0.0;
};

struct DecodeState {
    qsizetype recordCount = 0;
    qsizetype packetIndex = 0;
    CancellationCheck cancelled;
};

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toStdString());
}

void stopProcess(QProcess &process)
{
    if (process.state() == QProcess::NotRunning) return;
    process.terminate();
    if (!process.waitForFinished(500)) {
        process.kill();
        static_cast<void>(process.waitForFinished(2'000));
    }
}

QJsonObject runProbe(
    const QString &executable,
    const QStringList &arguments,
    const CancellationCheck &cancelled)
{
    throwIfCancelled(cancelled);
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(executable, arguments);
    QElapsedTimer startTimer;
    startTimer.start();
    while (process.state() == QProcess::Starting && startTimer.elapsed() < 5'000) {
        if (cancelled && cancelled()) {
            stopProcess(process);
            throw OperationCancelled();
        }
        static_cast<void>(process.waitForStarted(100));
    }
    if (process.state() == QProcess::Starting || process.state() == QProcess::NotRunning) {
        fail(QStringLiteral("Could not start ffprobe: %1").arg(process.errorString()));
    }
    QByteArray output;
    QByteArray diagnostics;
    output.reserve(1024 * 1024);
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning) {
        if (cancelled && cancelled()) {
            stopProcess(process);
            throw OperationCancelled();
        }
        static_cast<void>(waitForOutputOrExit(process, 100));
        output.append(process.readAllStandardOutput());
        diagnostics.append(process.readAllStandardError());
        if (output.size() > GoProTelemetrySource::kMaximumProbeOutputBytes) {
            stopProcess(process);
            throw ResourceLimitError("ffprobe output exceeds the supported 64 MiB limit.");
        }
        if (diagnostics.size() > 1024 * 1024) diagnostics = diagnostics.right(1024 * 1024);
        if (timer.elapsed() >= 120'000) {
            stopProcess(process);
            fail("ffprobe timed out while indexing the recording.");
        }
    }
    output.append(process.readAllStandardOutput());
    diagnostics.append(process.readAllStandardError());
    if (output.size() > GoProTelemetrySource::kMaximumProbeOutputBytes) {
        throw ResourceLimitError("ffprobe output exceeds the supported 64 MiB limit.");
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        fail(QStringLiteral("ffprobe failed: %1")
                 .arg(QString::fromUtf8(diagnostics).trimmed()));
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("Invalid ffprobe response: %1").arg(error.errorString()));
    }
    return document.object();
}

QVector<Record> records(const QByteArrayView bytes, DecodeState &state, const QString &context)
{
    QVector<Record> result;
    qsizetype offset = 0;
    while (offset + 8 <= bytes.size()) {
        if ((state.recordCount & 0xff) == 0) throwIfCancelled(state.cancelled);
        if (++state.recordCount > GoProTelemetrySource::kMaximumRecordCount) {
            throw ResourceLimitError(
                QStringLiteral("GPMF metadata KLV header count %1 exceeds configured limit %2 "
                               "while parsing packet %3 (%4).")
                    .arg(state.recordCount)
                    .arg(GoProTelemetrySource::kMaximumRecordCount)
                    .arg(state.packetIndex + 1)
                    .arg(context)
                    .toStdString());
        }
        const char *header = bytes.constData() + offset;
        const int size = static_cast<unsigned char>(header[5]);
        const int repeat = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar *>(header + 6));
        const qint64 dataSize = static_cast<qint64>(size) * repeat;
        if (size <= 0 || dataSize < 0 || dataSize > bytes.size() - offset - 8) {
            break;
        }
        result.append({
            QByteArrayView(header, 4),
            header[4],
            size,
            repeat,
            bytes.sliced(offset + 8, dataSize),
        });
        offset += 8 + ((dataSize + 3) & ~qint64(3));
    }
    return result;
}

qint32 signed32(const char *data)
{
    return qFromBigEndian<qint32>(reinterpret_cast<const uchar *>(data));
}

quint16 unsigned16(const char *data)
{
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data));
}

quint64 unsigned64(const char *data)
{
    return qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(data));
}

QVector<double> scalers(const Record &record)
{
    QVector<double> result;
    if (record.size != 4) {
        return result;
    }
    result.reserve(record.repeat);
    for (int index = 0; index < record.repeat; ++index) {
        const qint32 value = signed32(record.data.constData() + index * 4);
        result.append(value == 0 ? 1.0 : static_cast<double>(value));
    }
    return result;
}

void appendGpsStream(
    const QByteArrayView streamData,
    const double packetPts,
    const double packetDuration,
    QVector<GpsSample> &gps5,
    QVector<GpsSample> &gps9,
    DecodeState &state)
{
    const QVector<Record> streamRecords = records(streamData, state, QStringLiteral("STRM payload"));
    double streamPts = packetPts;
    const auto timestampIt =
        std::find_if(streamRecords.cbegin(), streamRecords.cend(), [](const Record &item) {
            return item.key == "STMP";
        });
    if (timestampIt != streamRecords.cend() && timestampIt->data.size() >= 8) {
        const double microsecondTime = unsigned64(timestampIt->data.constData()) / 1'000'000.0;
        if (std::isfinite(microsecondTime) && std::abs(microsecondTime - packetPts) <= 2.0) {
            streamPts = microsecondTime;
        }
    }
    const auto scaleIt = std::find_if(streamRecords.cbegin(), streamRecords.cend(), [](const Record &item) {
        return item.key == "SCAL";
    });
    const QVector<double> scale = scaleIt == streamRecords.cend() ? QVector<double>() : scalers(*scaleIt);
    const auto gpsIt = std::find_if(streamRecords.cbegin(), streamRecords.cend(), [](const Record &item) {
        return item.key == "GPS9" || item.key == "GPS5";
    });
    if (gpsIt == streamRecords.cend()) {
        return;
    }
    const bool isGps9 = gpsIt->key == "GPS9";
    const int expectedSize = isGps9 ? 32 : 20;
    const int dimensions = isGps9 ? 9 : 5;
    if (gpsIt->size != expectedSize || gpsIt->repeat <= 0 || scale.size() < dimensions) {
        return;
    }
    int gps5Fix = 3;
    if (!isGps9) {
        const auto fixIt = std::find_if(streamRecords.cbegin(), streamRecords.cend(), [](const Record &item) {
            return item.key == "GPSF";
        });
        if (fixIt != streamRecords.cend() && fixIt->data.size() >= 4) {
            gps5Fix = signed32(fixIt->data.constData());
        }
    }
    QVector<GpsSample> &destination = isGps9 ? gps9 : gps5;
    for (int sampleIndex = 0; sampleIndex < gpsIt->repeat; ++sampleIndex) {
        if ((sampleIndex & 0xff) == 0) throwIfCancelled(state.cancelled);
        const char *sample = gpsIt->data.constData() + sampleIndex * expectedSize;
        const double latitude = signed32(sample) / scale[0];
        const double longitude = signed32(sample + 4) / scale[1];
        const double speed = signed32(sample + 12) / scale[3];
        const int fix = isGps9 ? unsigned16(sample + 30) : gps5Fix;
        if (!std::isfinite(latitude) || !std::isfinite(longitude) || !std::isfinite(speed)
            || std::abs(latitude) > 90.0 || std::abs(longitude) > 180.0
            || (latitude == 0.0 && longitude == 0.0) || fix < 2) {
            continue;
        }
        destination.append({
            streamPts + packetDuration * sampleIndex / gpsIt->repeat,
            static_cast<float>(latitude),
            static_cast<float>(longitude),
            static_cast<float>(speed * 3.6),
        });
    }
}

void visitContainers(
    const QByteArrayView bytes,
    const double pts,
    const double duration,
    QVector<GpsSample> &gps5,
    QVector<GpsSample> &gps9,
    DecodeState &state,
    const int depth)
{
    if (depth > GoProTelemetrySource::kMaximumContainerDepth) {
        throw ResourceLimitError("GPMF metadata exceeds the supported container depth.");
    }
    for (const Record &record : records(bytes, state, depth == 0
            ? QStringLiteral("packet root")
            : QStringLiteral("container depth %1").arg(depth))) {
        if (record.key == "STRM") {
            appendGpsStream(record.data, pts, duration, gps5, gps9, state);
        } else if (record.type == 0) {
            visitContainers(record.data, pts, duration, gps5, gps9, state, depth + 1);
        }
    }
}

TelemetryChannel channel(
    const QString &name,
    const QString &unit,
    const QVector<GpsSample> &samples,
    const auto value,
    const CancellationCheck &cancelled)
{
    TelemetryChannel result;
    result.name = name;
    result.unit = unit;
    result.timestamps.reserve(samples.size());
    result.values.reserve(samples.size());
    for (const GpsSample &sample : samples) {
        if ((result.timestamps.size() & 0xfff) == 0) throwIfCancelled(cancelled);
        result.timestamps.append(sample.time);
        result.values.append(value(sample));
    }
    return result;
}

} // namespace

GoProTelemetryResult GoProTelemetrySource::load(
    const QString &videoPath,
    const CancellationCheck &cancelled,
    const QString &ffprobeExecutable)
{
    throwIfCancelled(cancelled);
    const QFileInfo info(videoPath);
    if (!info.isFile()) {
        fail("The selected video does not exist.");
    }
    const QString executable = ffprobeExecutable.isEmpty()
        ? FfmpegTools::ffprobePath() : ffprobeExecutable;
    if (executable.isEmpty()) {
        fail("ffprobe was not found. Install FFmpeg to read GoPro telemetry.");
    }
    const QJsonObject probe = runProbe(executable, {
        QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_format"),
        QStringLiteral("-show_streams"), QStringLiteral("-of"), QStringLiteral("json"),
        info.absoluteFilePath(),
    }, cancelled);
    const QJsonArray streams = probe.value("streams").toArray();
    int streamIndex = -1;
    for (qsizetype i = 0; i < streams.size(); ++i) {
        if ((i & 0xff) == 0) throwIfCancelled(cancelled);
        const QJsonValue &value = streams[i];
        const QJsonObject stream = value.toObject();
        if (stream.value("codec_type").toString() == "data"
            && stream.value("codec_tag_string").toString() == "gpmd") {
            streamIndex = stream.value("index").toInt(-1);
            break;
        }
    }
    if (streamIndex < 0) {
        fail("No GoPro GPMF telemetry track was found.");
    }
    const double videoDuration = probe.value("format").toObject().value("duration").toString().toDouble();
    const QJsonObject packetProbe = runProbe(executable, {
        QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"),
        QString::number(streamIndex), QStringLiteral("-show_packets"), QStringLiteral("-show_entries"),
        QStringLiteral("packet=pts_time,duration_time,size,pos"), QStringLiteral("-of"),
        QStringLiteral("json"), info.absoluteFilePath(),
    }, cancelled);
    const QJsonArray packetArray = packetProbe.value("packets").toArray();
    if (packetArray.size() > kMaximumPacketCount) {
        throw ResourceLimitError("The GPMF packet index contains too many packets.");
    }
    QVector<ProbePacket> index;
    qint64 totalSize = 0;
    index.reserve(packetArray.size());
    const qint64 fileSize = info.size();
    for (qsizetype packetIndex = 0; packetIndex < packetArray.size(); ++packetIndex) {
        if ((packetIndex & 0xff) == 0) throwIfCancelled(cancelled);
        const QJsonValue &value = packetArray[packetIndex];
        const QJsonObject item = value.toObject();
        bool positionValid = false;
        bool sizeValid = false;
        bool ptsValid = false;
        bool durationValid = false;
        ProbePacket packet{
            item.value("pos").toString().toLongLong(&positionValid),
            item.value("size").toString().toLongLong(&sizeValid),
            item.value("pts_time").toString().toDouble(&ptsValid),
            item.value("duration_time").toString().toDouble(&durationValid),
        };
        if (!positionValid || !sizeValid || !ptsValid || !durationValid
            || packet.position < 0 || packet.size <= 0 || !std::isfinite(packet.pts)
            || !std::isfinite(packet.duration) || packet.duration < 0.0) {
            fail("The GPMF packet index is missing or invalid.");
        }
        if (packet.position > fileSize || packet.size > fileSize - packet.position) {
            fail("A GPMF packet lies outside the media file.");
        }
        if (packet.size > kMaximumPacketBytes) {
            throw ResourceLimitError("A GPMF packet exceeds the supported 16 MiB limit.");
        }
        if (packet.size > kMaximumMetadataBytes - totalSize) {
            throw ResourceLimitError("The GPMF metadata track exceeds the supported 512 MiB limit.");
        }
        totalSize += packet.size;
        index.append(packet);
    }
    if (index.isEmpty()) {
        fail("The GoPro telemetry track is empty.");
    }
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("Could not read video: %1").arg(file.errorString()));
    }
    QVector<GpmfPacket> packets;
    packets.reserve(index.size());
    for (qsizetype packetIndex = 0; packetIndex < index.size(); ++packetIndex) {
        if ((packetIndex & 0x3f) == 0) throwIfCancelled(cancelled);
        const ProbePacket &entry = index[packetIndex];
        if (!file.seek(entry.position)) {
            fail("Could not seek to a GPMF packet.");
        }
        QByteArray data;
        data.reserve(static_cast<qsizetype>(entry.size));
        qint64 remaining = entry.size;
        while (remaining > 0) {
            throwIfCancelled(cancelled);
            const QByteArray chunk = file.read(qMin<qint64>(remaining, 1024 * 1024));
            if (chunk.isEmpty()) break;
            data.append(chunk);
            remaining -= chunk.size();
        }
        if (data.size() != entry.size) {
            fail("Unexpected end of file in a GPMF packet.");
        }
        packets.append({std::move(data), entry.pts, entry.duration});
    }
    return decodeGpsPackets(packets, videoDuration, cancelled);
}

GoProTelemetryResult GoProTelemetrySource::decodeGpsPackets(
    const QVector<GpmfPacket> &packets,
    const double videoDuration,
    const CancellationCheck &cancelled)
{
    if (packets.size() > kMaximumPacketCount) {
        throw ResourceLimitError("GPMF metadata contains too many packets.");
    }
    qint64 totalBytes = 0;
    QVector<GpsSample> gps5;
    QVector<GpsSample> gps9;
    DecodeState state{0, 0, cancelled};
    for (qsizetype packetIndex = 0; packetIndex < packets.size(); ++packetIndex) {
        if ((packetIndex & 0x3f) == 0) throwIfCancelled(cancelled);
        const GpmfPacket &packet = packets[packetIndex];
        if (!std::isfinite(packet.pts) || !std::isfinite(packet.duration) || packet.duration < 0.0) {
            fail("A GPMF packet has invalid timing metadata.");
        }
        if (packet.data.size() > kMaximumPacketBytes) {
            throw ResourceLimitError("A GPMF packet exceeds the supported 16 MiB limit.");
        }
        if (packet.data.size() > kMaximumMetadataBytes - totalBytes) {
            throw ResourceLimitError("GPMF metadata exceeds the supported 512 MiB limit.");
        }
        totalBytes += packet.data.size();
        state.packetIndex = packetIndex;
        visitContainers(packet.data, packet.pts, packet.duration, gps5, gps9, state, 0);
    }
    const bool useGps9 = !gps9.isEmpty();
    QVector<GpsSample> samples = useGps9 ? std::move(gps9) : std::move(gps5);
    const QString streamName = useGps9 ? QStringLiteral("GPS9") : QStringLiteral("GPS5");
    if (samples.isEmpty()) {
        fail("The GPMF track contains no usable GPS speed samples.");
    }
    throwIfCancelled(cancelled);
    for (qsizetype index = 0; index < samples.size(); ++index) {
        if ((index & 0xfff) == 0) throwIfCancelled(cancelled);
        if (!std::isfinite(samples[index].time)) {
            fail("The GPMF track contains invalid sample timestamps.");
        }
    }
    std::stable_sort(samples.begin(), samples.end(), [](const GpsSample &left, const GpsSample &right) {
        return left.time < right.time;
    });
    const auto uniqueEnd = std::unique(samples.begin(), samples.end(), [](const GpsSample &left, const GpsSample &right) {
        return left.time == right.time;
    });
    samples.erase(uniqueEnd, samples.end());
    for (qsizetype index = 1; index < samples.size(); ++index) {
        if ((index & 0xfff) == 0) throwIfCancelled(cancelled);
        if (!std::isfinite(samples[index].time) || !(samples[index].time > samples[index - 1].time)) {
            fail("The GPMF track contains non-monotonic sample timestamps.");
        }
    }
    TelemetrySession session;
    session.duration = videoDuration > 0.0 ? videoDuration : samples.constLast().time;
    session.startTime = 0.0;
    session.metadata.insert("source", "GoPro GPMF");
    session.metadata.insert("gpsStream", streamName);
    session.sampleCount = samples.size();
    session.channels.insert(
        "GoPro latitude", channel("GoPro latitude", "deg", samples, [](const GpsSample &item) {
            return item.latitude;
        }, cancelled));
    session.channels.insert(
        "GoPro longitude", channel("GoPro longitude", "deg", samples, [](const GpsSample &item) {
            return item.longitude;
        }, cancelled));
    session.channels.insert(
        "GoPro GPS speed", channel("GoPro GPS speed", "km/h", samples, [](const GpsSample &item) {
            return item.speedKmh;
        }, cancelled));
    session.aliases.insert("latitude", "GoPro latitude");
    session.aliases.insert("longitude", "GoPro longitude");
    session.aliases.insert("speed", "GoPro GPS speed");
    freezeCachedStatistics(session, cancelled);
    return {std::move(session), packets.size(), state.recordCount, streamName};
}

} // namespace FlappedEar
