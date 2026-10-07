#include "export/VideoComposition.h"

#include <algorithm>
#include <cmath>

namespace FlappedEar::VideoComposition {
namespace {

int evenFloor(const double value)
{
    return std::max(2, static_cast<int>(std::floor(value / 2.0)) * 2);
}

// The largest even size with the source's aspect ratio inside the box.
QSize fit(const QSize &source, const QSize &box)
{
    const double scale = std::min(static_cast<double>(box.width()) / source.width(),
                                  static_cast<double>(box.height()) / source.height());
    return {std::min(box.width(), evenFloor(source.width() * scale)),
            std::min(box.height(), evenFloor(source.height() * scale))};
}

// `size` centred in `cell`, at an even position.
QRect centred(const QSize &size, const QRect &cell)
{
    return {cell.x() + (cell.width() - size.width()) / 4 * 2,
            cell.y() + (cell.height() - size.height()) / 4 * 2, size.width(), size.height()};
}

} // namespace

QVector<QRect> layout(const VideoLayoutMode mode, const QSize &output, const QVector<QSize> &sourceSizes)
{
    if (output.width() < 4 || output.height() < 4 || sourceSizes.isEmpty()) return {};
    if (std::any_of(sourceSizes.cbegin(), sourceSizes.cend(), [](const QSize &size) { return size.isEmpty(); }))
        return {};
    const qsizetype additional = sourceSizes.size() - 1;
    QVector<QRect> rects;
    if (mode == VideoLayoutMode::PictureInPicture || additional == 0) {
        rects.append(QRect(QPoint(0, 0), output));
        const QSize box(evenFloor(output.width() * pictureInPictureShare),
                        evenFloor(output.height() * pictureInPictureShare));
        const int margin = evenFloor(std::min(output.width(), output.height()) * marginShare);
        for (qsizetype index = 1; index < sourceSizes.size(); ++index) {
            const QSize size = fit(sourceSizes[index], box);
            rects.append({output.width() - margin - size.width(),
                          margin + static_cast<int>(index - 1) * (box.height() + margin), size.width(), size.height()});
        }
        return rects;
    }
    // Side by side: the main video in the left half, the additional videos
    // one above the other in the right half.
    const int half = output.width() / 4 * 2;
    rects.append(centred(fit(sourceSizes[0], {half, output.height()}), {0, 0, half, output.height()}));
    const int cellHeight = output.height() / static_cast<int>(additional) / 2 * 2;
    for (qsizetype index = 1; index < sourceSizes.size(); ++index) {
        const QRect cell(half, static_cast<int>(index - 1) * cellHeight, output.width() - half, cellHeight);
        rects.append(centred(fit(sourceSizes[index], cell.size()), cell));
    }
    return rects;
}

std::optional<Timing> timing(const SyncTransform &mainSync, const SyncTransform &videoSync,
    const double mainStartSeconds, const double exportSeconds, const double videoStartSeconds, const double videoSeconds)
{
    if (!(mainSync.timeScale > 0.0) || !(videoSync.timeScale > 0.0) || !(exportSeconds > 0.0) || !(videoSeconds > 0.0))
        return std::nullopt;
    // Additional video time = rate x main video time + shift.
    const double rate = mainSync.timeScale / videoSync.timeScale;
    const double shift = (mainSync.offset - videoSync.offset) / videoSync.timeScale;
    const double first = rate * mainStartSeconds + shift;
    const double last = rate * (mainStartSeconds + exportSeconds) + shift;
    Timing result;
    result.inputSeekSeconds = std::max(0.0, first - 1.0) + videoStartSeconds;
    result.timeFactor = 1.0 / rate;
    result.timeShift = -(videoStartSeconds + shift) / rate - mainStartSeconds;
    if (!std::isfinite(first) || !std::isfinite(last) || !std::isfinite(result.inputSeekSeconds)
        || !std::isfinite(result.timeFactor) || !std::isfinite(result.timeShift))
        return std::nullopt;
    if (last <= 0.0 || first >= videoSeconds) return std::nullopt;
    return result;
}

} // namespace FlappedEar::VideoComposition
