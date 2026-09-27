#include "export/ExportFormat.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace FlappedEar {
namespace {
QSize evenFit(const QSize &source, int targetWidth)
{
    if (targetWidth > source.width()) return {};
    const int width = targetWidth - targetWidth % 2;
    const long double scaledHeight = static_cast<long double>(source.height()) * width / source.width();
    if (scaledHeight > std::numeric_limits<int>::max()) return {};
    const int height = qMax(2, static_cast<int>(std::llround(scaledHeight)) & ~1);
    return {width, height};
}
}

QList<QSize> ExportFormat::resolutionOptions(const QSize &source)
{
    QList<QSize> result;
    if (!source.isValid()) return result;
    const QSize normalized{source.width() & ~1, source.height() & ~1};
    result.append(normalized);
    for (const int width : {3840, 2560, 1920, 1280}) {
        const QSize candidate = evenFit(source, width);
        if (candidate.isValid() && candidate != normalized && !result.contains(candidate)) result.append(candidate);
    }
    return result;
}

QList<MediaRational> ExportFormat::frameRateOptions(const MediaRational &source)
{
    QList<MediaRational> result;
    if (!source.isValid()) return result;
    result.append(source);
    if (source.numerator % 2 == 0) {
        const MediaRational half{source.numerator / 2, source.denominator};
        if (half.isValid()) result.append(half);
    }
    return result;
}

qint64 ExportFormat::recommendedVideoBitrate(
    const QSize &size, const MediaRational &rate, const int bitDepth)
{
    if (!size.isValid() || !rate.isValid()) return 0;
    constexpr long double referencePixelRate = 1920.0L * 1080.0L * 30.0L;
    constexpr long double referenceBitrate = 12'500'000.0L;
    // Sublinear scaling reflects the increased compression efficiency of
    // larger pictures while retaining the established 1080p/4K ballpark.
    constexpr long double scalingExponent = 0.66L;
    const long double pixelRate = static_cast<long double>(size.width()) * size.height()
        * rate.numerator / rate.denominator;
    if (!std::isfinite(static_cast<double>(pixelRate)) || pixelRate <= 0.0L) return 0;
    const long double depthMultiplier = bitDepth > 8 ? 1.15L : 1.0L;
    const long double recommendation = referenceBitrate
        * std::pow(pixelRate / referencePixelRate, scalingExponent) * depthMultiplier;
    if (!std::isfinite(static_cast<double>(recommendation))) return maximumCustomVideoBitrate;
    return qBound<qint64>(1'000'000, static_cast<qint64>(std::llround(recommendation)),
                          maximumCustomVideoBitrate);
}

qint64 ExportFormat::bitrateForQuality(
    const QString &quality, const QSize &size, const MediaRational &rate,
    const int bitDepth)
{
    const double multiplier = quality == QStringLiteral("smaller") ? 0.70
        : quality == QStringLiteral("high") ? 1.30 : 1.0;
    return qMin(maximumCustomVideoBitrate,
                qRound64(recommendedVideoBitrate(size, rate, bitDepth) * multiplier));
}

bool ExportFormat::validCustomBitrate(const qint64 bitrate)
{
    return bitrate >= 500'000 && bitrate <= maximumCustomVideoBitrate;
}

std::optional<qint64> ExportFormat::rgbaFrameBytes(const QSize &size)
{
    if (!size.isValid()) return std::nullopt;
    constexpr qint64 channels = 4;
    const qint64 width = size.width();
    const qint64 height = size.height();
    if (width > std::numeric_limits<qint64>::max() / height
        || width * height > std::numeric_limits<qint64>::max() / channels) {
        return std::nullopt;
    }
    return width * height * channels;
}

qint64 ExportFormat::estimatedBytes(const qint64 videoBitrate, const bool audioEnabled, const double seconds)
{
    if (!validCustomBitrate(videoBitrate) || !std::isfinite(seconds) || seconds < 0) return 0;
    const long double estimate = (static_cast<long double>(videoBitrate)
        + (audioEnabled ? audioBitrate : 0)) * seconds / 8.0L * 1.03L;
    // 2^63 exactly: every smaller estimate rounds into qint64.
    return estimate >= std::ldexp(1.0L, 63)
        ? std::numeric_limits<qint64>::max() : static_cast<qint64>(std::llround(estimate));
}

QString ExportFormat::formatEstimatedSize(const qint64 bytes)
{
    if (bytes <= 0) return QStringLiteral("~0 MiB");
    const double mebibytes = bytes / (1024.0 * 1024.0);
    if (mebibytes < 1024.0)
        return QStringLiteral("~%1 MiB").arg(QString::number(mebibytes, 'f', mebibytes < 100.0 ? 1 : 0));
    const double gibibytes = mebibytes / 1024.0;
    return QStringLiteral("~%1 GiB").arg(QString::number(gibibytes, 'f', gibibytes < 10.0 ? 2 : 1));
}
} // namespace FlappedEar
