#include "app/ExportSourceOptions.h"

#include "export/ExportEngine.h"
#include "export/ExportFormat.h"

#include <QVariantList>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace FlappedEar {

namespace {

std::optional<qint64> frameAtOrBeforePresentationTime(
    const double seconds, const MediaRational &frameRate)
{
    if (!std::isfinite(seconds) || seconds < 0.0 || !frameRate.isValid()) return std::nullopt;
    const long double frame = static_cast<long double>(seconds)
        * static_cast<long double>(frameRate.numerator) / static_cast<long double>(frameRate.denominator);
    if (frame < 0.0L || frame > static_cast<long double>(std::numeric_limits<qint64>::max())) {
        return std::nullopt;
    }
    // This selects the frame that contains the requested presentation time. The
    // resulting inclusive frame range is still the sole export authority.
    return static_cast<qint64>(frame);
}

double rangeSeconds(const ExportFrameRange &range, const MediaRational &rate)
{
    return static_cast<double>(range.frameCount())
        * static_cast<double>(rate.denominator) / static_cast<double>(rate.numerator);
}

} // namespace

ExportSourceOptions::ExportSourceOptions(const MediaInfo &source)
    : m_source(&source)
{
}

MediaRational ExportSourceOptions::sourceRate() const
{
    return m_source->averageFrameRate.isValid() ? m_source->averageFrameRate : m_source->frameRate;
}

QVariantMap ExportSourceOptions::sourceInfo() const
{
    if (!m_source->videoSize.isValid()) {
        return {};
    }
    const MediaRational rate = sourceRate();
    const QString colorSummary = m_source->sourceColorClass == SourceColorClass::Sdr
        && m_source->colorPrimaries == QStringLiteral("bt709")
        ? QStringLiteral("Rec.709 SDR")
        : sourceColorClassName(m_source->sourceColorClass);
    const bool unsupportedColorManagedSource =
        isUnsupportedColorManagedClass(m_source->sourceColorClass);
    return {
        {"width", m_source->videoSize.width()},
        {"height", m_source->videoSize.height()},
        {"codedWidth", m_source->codedVideoSize.width()},
        {"codedHeight", m_source->codedVideoSize.height()},
        {"displayWidth", m_source->displayVideoSize.width()},
        {"displayHeight", m_source->displayVideoSize.height()},
        {"duration", m_source->duration},
        {"frameRate", rate.value()},
        {"frameRateText", QStringLiteral("%1/%2 (%3 fps)")
                              .arg(rate.numerator)
                              .arg(rate.denominator)
                              .arg(rate.value(), 0, 'f', 3)},
        {"videoCodec", m_source->videoCodec},
        {"videoCodecProfile", m_source->videoCodecProfile},
        {"pixelFormat", m_source->pixelFormat},
        {"bitDepth", m_source->bitDepth
                         ? QVariant(*m_source->bitDepth) : QVariant()},
        {"sourceVideoBitrate", m_source->sourceVideoBitrate
                                  ? QVariant(*m_source->sourceVideoBitrate) : QVariant()},
        {"sampleAspectRatio", m_source->sampleAspectRatio.isValid()
                                  ? QStringLiteral("%1:%2")
                                        .arg(m_source->sampleAspectRatio.numerator)
                                        .arg(m_source->sampleAspectRatio.denominator)
                                  : QString()},
        {"rotationDegrees", m_source->rotationDegrees
                                ? QVariant(*m_source->rotationDegrees) : QVariant()},
        {"colorRange", m_source->colorRange},
        {"colorSpace", m_source->colorSpace},
        {"colorTransfer", m_source->colorTransfer},
        {"colorPrimaries", m_source->colorPrimaries},
        {"colorClass", sourceColorClassName(m_source->sourceColorClass)},
        {"colorSummary", colorSummary},
        {"unsupportedColorManagedSource", unsupportedColorManagedSource},
        {"audioCodecs", m_source->audioCodecs.join(QStringLiteral(", "))},
        {"likelyVariableFrameRate", m_source->likelyVariableFrameRate},
    };
}

QVariantMap ExportSourceOptions::formatOptions() const
{
    QVariantList sizes, rates;
    int index = 0;
    for (const QSize &size : ExportFormat::resolutionOptions(m_source->videoSize)) {
        const QString label = QStringLiteral("%1×%2%3").arg(size.width()).arg(size.height())
                                  .arg(index++ == 0 ? QStringLiteral(" (Source)") : QString());
        sizes.append(QVariantMap{{"width", size.width()}, {"height", size.height()}, {"label", label}});
    }
    index = 0;
    for (const MediaRational &rate : ExportFormat::frameRateOptions(sourceRate())) {
        const QString label = QString::number(rate.value(), 'f', 2) + QStringLiteral(" fps")
                              + (index++ == 0 ? QStringLiteral(" (Source)") : QString());
        rates.append(QVariantMap{{"numerator", rate.numerator}, {"denominator", rate.denominator},
                                 {"label", label}});
    }
    return {{"sizes", sizes}, {"rates", rates}};
}

qint64 ExportSourceOptions::recommendedBitrate(
    const QSize &size, const MediaRational &rate, const QString &quality) const
{
    return ExportFormat::bitrateForQuality(quality, size, rate, m_source->bitDepth.value_or(8));
}

QString ExportSourceOptions::fullRangeTimecode(const MediaRational &rate, const bool outPoint) const
{
    const auto range = ExportEngine::fullVideoFrameRange(*m_source, rate);
    return range ? ExportEngine::formatSmpteTimecode(
        outPoint ? range->lastFrame : range->firstFrame, rate) : QString();
}

double ExportSourceOptions::rangeDurationSeconds(
    const MediaRational &rate, const QString &rangeIn, const QString &rangeOut) const
{
    const auto range = ExportEngine::frameRangeForSourceTimecode(*m_source, rate, rangeIn, rangeOut);
    return range ? rangeSeconds(*range, rate) : 0.0;
}

QVariantMap ExportSourceOptions::lapRange(
    const int lapNumber, const double videoStart, const double videoEnd,
    const MediaRational &rate, const int handleSeconds) const
{
    const auto fullRange = ExportEngine::fullVideoFrameRange(*m_source, rate);
    if (!fullRange || handleSeconds < 0 || handleSeconds > 30 || videoEnd < videoStart) {
        return {{QStringLiteral("valid"), false}};
    }
    const auto requestedFirst = frameAtOrBeforePresentationTime(
        std::max(0.0, videoStart - static_cast<double>(handleSeconds)), rate);
    const auto requestedLast = frameAtOrBeforePresentationTime(
        std::max(0.0, videoEnd + static_cast<double>(handleSeconds)), rate);
    if (!requestedFirst || !requestedLast) return {{QStringLiteral("valid"), false}};

    const auto range = ExportEngine::frameRangeFromInclusiveFrames(
        qBound(fullRange->firstFrame, *requestedFirst, fullRange->lastFrame),
        qBound(fullRange->firstFrame, *requestedLast, fullRange->lastFrame));
    if (!range) return {{QStringLiteral("valid"), false}};
    return {{QStringLiteral("valid"), true}, {QStringLiteral("lapNumber"), lapNumber},
            {QStringLiteral("handleSeconds"), handleSeconds},
            {QStringLiteral("firstFrame"), range->firstFrame},
            {QStringLiteral("lastFrame"), range->lastFrame},
            {QStringLiteral("inTimecode"), ExportEngine::formatSmpteTimecode(range->firstFrame, rate)},
            {QStringLiteral("outTimecode"), ExportEngine::formatSmpteTimecode(range->lastFrame, rate)},
            {QStringLiteral("durationSeconds"), rangeSeconds(*range, rate)}};
}

} // namespace FlappedEar
