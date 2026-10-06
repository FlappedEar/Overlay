#pragma once

#include "telemetry/TelemetrySession.h"
#include "telemetry/SourceOperation.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace FlappedEar {

struct GpmfPacket {
    QByteArray data;
    double pts = 0.0;
    double duration = 0.0;
};

struct GoProTelemetryResult {
    TelemetrySession session;
    qsizetype packetCount = 0;
    qsizetype recordCount = 0;
    QString gpsStream;
};

class GoProTelemetrySource {
public:
    static constexpr qsizetype kMaximumProbeOutputBytes = 64 * 1024 * 1024;
    static constexpr qsizetype kMaximumPacketCount = 100'000;
    static constexpr qint64 kMaximumMetadataBytes = 512LL * 1024 * 1024;
    // Real GoPro packets hold about a second of metadata, a few KiB; the cap only
    // stops one crafted packet from taking the whole metadata budget (KAN-197).
    static constexpr qint64 kMaximumPacketBytes = 16LL * 1024 * 1024;
    // Counts every parsed KLV header across the metadata track. The byte,
    // packet, and depth limits independently bound storage and nesting; this
    // cap bounds header-processing work with substantial headroom over real
    // multi-sensor GoPro recordings.
    static constexpr qsizetype kMaximumRecordCount = 1'000'000;
    static constexpr int kMaximumContainerDepth = 32;

    [[nodiscard]] static GoProTelemetryResult load(
        const QString &videoPath,
        const CancellationCheck &cancelled = {},
        const QString &ffprobeExecutable = {});
    [[nodiscard]] static GoProTelemetryResult decodeGpsPackets(
        const QVector<GpmfPacket> &packets,
        double videoDuration = 0.0,
        const CancellationCheck &cancelled = {});
};

} // namespace FlappedEar
