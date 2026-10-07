#include "app/PreviewTimeline.h"

#include "app/PreviewPlayback.h"

#include <QtGlobal>

#include <cmath>

namespace FlappedEar {

PreviewTimeline::PreviewTimeline(const MediaInfo &source, const std::optional<double> chapteredDurationSeconds)
    : m_source(&source), m_chapteredDurationSeconds(chapteredDurationSeconds)
{
}

MediaRational PreviewTimeline::frameRate() const
{
    return m_source->averageFrameRate.isValid() ? m_source->averageFrameRate : m_source->frameRate;
}

QRect PreviewTimeline::viewport(const QSize &available) const
{
    const QSize sourceSize = m_source->displayVideoSize.isValid() ? m_source->displayVideoSize : m_source->videoSize;
    return PreviewPlayback::aspectFitViewport(available, sourceSize);
}

std::optional<qint64> PreviewTimeline::chapteredLastFrame() const
{
    // KAN-105: the whole chapter timeline, framed at the first chapter's rate.
    const MediaRational rate = frameRate();
    if (!m_chapteredDurationSeconds || !rate.isValid()) return std::nullopt;
    const auto frames = static_cast<qint64>(
        std::floor(*m_chapteredDurationSeconds * rate.numerator / rate.denominator + 1e-6));
    return frames >= 1 ? std::optional<qint64>(frames - 1) : std::nullopt;
}

std::optional<ExportFrameRange> PreviewTimeline::frameRange() const
{
    if (const auto last = chapteredLastFrame()) return ExportFrameRange{0, *last};
    return ExportEngine::fullVideoFrameRange(*m_source, frameRate());
}

qint64 PreviewTimeline::endPositionMilliseconds() const
{
    const auto range = frameRange();
    const auto position = range ? PreviewPlayback::framePositionMilliseconds(range->lastFrame, frameRate()) : std::nullopt;
    return position.value_or(0);
}

qint64 PreviewTimeline::initialPositionMilliseconds() const
{
    const MediaRational rate = frameRate();
    const auto range = ExportEngine::fullVideoFrameRange(*m_source, rate);
    if (!range || range->lastFrame < 1) return 0;
    return PreviewPlayback::firstTimelineFramePositionMilliseconds(rate).value_or(0);
}

qint64 PreviewTimeline::clampPositionMilliseconds(const qint64 requestedMilliseconds) const
{
    const auto range = frameRange();
    const auto position = range
        ? PreviewPlayback::clampPositionMilliseconds(requestedMilliseconds, range->lastFrame, frameRate())
        : std::nullopt;
    return position.value_or(0);
}

QString PreviewTimeline::timecodeForPositionMilliseconds(const qint64 positionMilliseconds) const
{
    const MediaRational rate = frameRate();
    const auto range = frameRange();
    if (!range || !rate.isValid() || positionMilliseconds < 0) return {};
    const qint64 bounded = clampPositionMilliseconds(positionMilliseconds);
    if (bounded >= endPositionMilliseconds()) {
        return ExportEngine::formatSmpteTimecode(range->lastFrame, rate);
    }
    const qint64 frame = static_cast<qint64>(bounded) * rate.numerator / (rate.denominator * 1'000);
    return ExportEngine::formatSmpteTimecode(qBound(range->firstFrame, frame, range->lastFrame), rate);
}

QString PreviewTimeline::endTimecode() const
{
    const auto range = frameRange();
    return range ? ExportEngine::formatSmpteTimecode(range->lastFrame, frameRate()) : QString();
}

} // namespace FlappedEar
