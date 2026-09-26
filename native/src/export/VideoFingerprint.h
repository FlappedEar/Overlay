#pragma once

#include "export/MediaProbe.h"

#include <QJsonObject>
#include <QString>

namespace FlappedEar {

// A video source's fingerprint (kind video-v1), stored in the event document
// next to telemetry fingerprints. Lives on the overlay side so the document
// layer has no video dependency (KAN-123); the JSON is unchanged.
[[nodiscard]] QJsonObject videoSourceFingerprint(const QString &path, const MediaInfo &mediaInfo);

} // namespace FlappedEar
