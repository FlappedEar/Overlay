#include "export/VideoFingerprint.h"

#include "project/ProjectSourceReference.h"

#include <QFileInfo>

namespace FlappedEar {

QJsonObject videoSourceFingerprint(const QString &path, const MediaInfo &mediaInfo)
{
    const MediaRational rate = mediaInfo.averageFrameRate.isValid()
        ? mediaInfo.averageFrameRate : mediaInfo.frameRate;
    return {
        {QStringLiteral("kind"), QStringLiteral("video-v1")},
        {QStringLiteral("size"), QFileInfo(path).size()},
        {QStringLiteral("sampledSha256"), ProjectSourceReferenceCodec::sampledDigest(path)},
        {QStringLiteral("durationUs"), ProjectSourceReferenceCodec::roundedMicroseconds(mediaInfo.duration)},
        {QStringLiteral("width"), mediaInfo.videoSize.width()},
        {QStringLiteral("height"), mediaInfo.videoSize.height()},
        {QStringLiteral("frameRateNumerator"), rate.numerator},
        {QStringLiteral("frameRateDenominator"), rate.denominator},
        {QStringLiteral("codec"), mediaInfo.videoCodec},
    };
}

} // namespace FlappedEar
