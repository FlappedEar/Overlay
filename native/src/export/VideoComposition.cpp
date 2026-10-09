#include "export/VideoComposition.h"

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QSet>

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

int cameraIndex(const QStringList &cameraIds, const QString &id)
{
    const qsizetype index = cameraIds.indexOf(id);
    return index < 0 ? 0 : static_cast<int>(index);
}

QRect centredRect(const QSize &size, const QSize &frame)
{
    return centred(size, QRect(QPoint(0, 0), frame));
}

void addWindow(QVector<Window> &windows, const Window &window)
{
    if (!(window.end > window.start)) return;
    if (!windows.isEmpty() && windows.last().end >= window.start) windows.last().end = std::max(windows.last().end, window.end);
    else windows.append(window);
}

} // namespace

QRectF startingBoxArea(const QVector<QRectF> &occupied)
{
    constexpr double size = 0.28;
    constexpr double margin = 0.02;
    const double far = 1.0 - size - margin;
    const QRectF corners[] = {{far, margin, size, size}, {margin, margin, size, size},
                              {far, far, size, size}, {margin, far, size, size}};
    QRectF best = corners[0];
    double least = -1.0;
    for (const QRectF &corner : corners) {
        double overlap = 0.0;
        for (const QRectF &other : occupied) {
            const QRectF common = corner.intersected(other);
            if (common.width() > 0 && common.height() > 0) overlap += common.width() * common.height();
        }
        if (least < 0.0 || overlap < least) { least = overlap; best = corner; }
    }
    return best;
}

int onAirAt(const ProgramOptions &program, const QStringList &cameraIds, const double time)
{
    int camera = 0;
    double latest = -1.0;
    for (const ProgramCut &cut : program.cuts) {
        if (cut.time <= time && cut.time >= latest) {
            latest = cut.time;
            camera = cameraIndex(cameraIds, cut.camera);
        }
    }
    return camera;
}

QVector<Segment> segments(const ProgramOptions &program, const QStringList &cameraIds, const double endSeconds,
    const QVector<Window> &available)
{
    if (!(endSeconds > 0.0) || !std::isfinite(endSeconds)) return {};
    QVector<ProgramCut> cuts = program.cuts;
    std::stable_sort(cuts.begin(), cuts.end(), [](const ProgramCut &a, const ProgramCut &b) { return a.time < b.time; });
    QVector<Segment> result;
    int camera = 0;
    double start = 0.0;
    const auto close = [&](const double end) {
        if (!(end > start)) return;
        if (!result.isEmpty() && result.last().camera == camera) result.last().window.end = end;
        else result.append(Segment{camera, Window{start, end}});
    };
    for (const ProgramCut &cut : cuts) {
        if (!std::isfinite(cut.time) || cut.time >= endSeconds) break;
        const double at = std::max(0.0, cut.time);
        close(at);
        start = std::max(start, at);
        camera = cameraIndex(cameraIds, cut.camera);
    }
    close(endSeconds);
    if (available.isEmpty()) return result;

    // A camera with no footage for part of its stretch cannot be shown there:
    // the main video is on air for that part.
    QVector<Segment> shown;
    const auto append = [&shown](const int cameraIndex, const double start, const double end) {
        if (!(end > start)) return;
        if (!shown.isEmpty() && shown.last().camera == cameraIndex && shown.last().window.end >= start)
            shown.last().window.end = end;
        else shown.append(Segment{cameraIndex, Window{start, end}});
    };
    for (const Segment &stretch : std::as_const(result)) {
        if (stretch.camera == 0 || stretch.camera >= available.size()) {
            append(stretch.camera, stretch.window.start, stretch.window.end);
            continue;
        }
        const Window &footage = available[stretch.camera];
        const double from = std::clamp(footage.start, stretch.window.start, stretch.window.end);
        const double to = std::clamp(footage.end, from, stretch.window.end);
        append(0, stretch.window.start, from);
        append(stretch.camera, from, to);
        append(0, to, stretch.window.end);
    }
    return shown;
}

QVector<CameraBox> cameraBoxes(const QJsonArray &widgets)
{
    QVector<CameraBox> result;
    for (const QJsonValue &entry : widgets) {
        const QJsonObject widget = entry.toObject();
        if (widget.value(QStringLiteral("type")).toString() != QStringLiteral("cameraBox")) continue;
        const QJsonObject settings = widget.value(QStringLiteral("settings")).toObject();
        CameraBox box;
        box.shown = widget.value(QStringLiteral("visible")).toBool(true);
        box.camera = settings.value(QStringLiteral("camera")).toString();
        if (box.camera.isEmpty()) box.shown = false;
        box.area = QRectF(widget.value(QStringLiteral("x")).toDouble(), widget.value(QStringLiteral("y")).toDouble(),
            widget.value(QStringLiteral("width")).toDouble(), widget.value(QStringLiteral("height")).toDouble());
        if (!std::isfinite(box.area.x()) || !std::isfinite(box.area.y()) || !std::isfinite(box.area.width())
            || !std::isfinite(box.area.height()) || !(box.area.width() > 0.0) || !(box.area.height() > 0.0))
            box.shown = false;
        const double border = settings.value(QStringLiteral("borderWidth")).toDouble(0.0);
        box.borderWidth = std::isfinite(border) ? static_cast<int>(std::lround(std::clamp(border, 0.0, 12.0))) : 0;
        const QString color = settings.value(QStringLiteral("borderColor")).toString();
        if (QColor(color).isValid() && color.size() == 7) box.borderColor = color.toUpper();
        box.crop = settings.value(QStringLiteral("fill")).toString() != QStringLiteral("fit");
        result.append(box);
    }
    return result;
}

QVector<Layer> plan(const VideoLayout &layout, const QSize &output, const QStringList &cameraIds,
    const QVector<QSize> &sourceSizes, const double endSeconds, const QVector<Window> &available,
    const QVector<CameraBox> &cameraBoxList)
{
    if (output.width() < 4 || output.height() < 4 || sourceSizes.size() != cameraIds.size() || cameraIds.isEmpty()
        || !(endSeconds > 0.0) || !std::isfinite(endSeconds))
        return {};
    if (std::any_of(sourceSizes.cbegin(), sourceSizes.cend(), [](const QSize &size) { return size.isEmpty(); })) return {};
    const int cameras = static_cast<int>(cameraIds.size());
    if (cameras < 2) return {};
    const QVector<Segment> stretches = segments(layout.program, cameraIds, endSeconds, available);
    const PipOptions &pip = layout.pip;
    const bool fading = layout.program.transition == ProgramTransition::Crossfade;
    const double fade = layout.program.crossfadeSeconds;
    QVector<Layer> result;

    // On-air layers: a camera other than the main video fills the frame
    // (letterboxed) while it is on air. The main video is the base under
    // everything; it gets a layer only to fade back in.
    for (qsizetype index = 0; index < stretches.size(); ++index) {
        const Segment &stretch = stretches[index];
        if (stretch.camera == 0 && !(fading && index > 0)) continue;
        Layer *layer = nullptr;
        if (!fading) {
            for (Layer &existing : result)
                if (existing.camera == stretch.camera) layer = &existing;
        }
        if (layer == nullptr) {
            Layer created;
            created.camera = stretch.camera;
            created.onAir = true;
            created.rect = QRect(QPoint(0, 0), output);
            created.content = stretch.camera == 0 ? created.rect
                : centredRect(fit(sourceSizes[stretch.camera], output), output);
            created.borderColor = QStringLiteral("#000000");
            created.fadeInSeconds = fading && (index > 0 || stretch.camera != 0) ? fade : 0.0;
            result.append(created);
            layer = &result.last();
        }
        Window window = stretch.window;
        // The next camera fades in over this one, so this one stays until it has.
        if (fading && stretch.camera != 0 && index + 1 < stretches.size()) window.end += fade;
        addWindow(layer->windows, window);
    }

    // Picture-in-picture boxes.
    if (!pip.enabled) return result;
    if (!cameraBoxList.isEmpty()) {
        // KAN-254: each box is a widget's rectangle.
        for (const CameraBox &widget : cameraBoxList) {
            if (!widget.shown) continue;
            const qsizetype camera = cameraIds.indexOf(widget.camera);
            if (camera < 0) continue;
            const int border = widget.borderWidth <= 0 ? 0
                : std::max(2, static_cast<int>(std::lround(widget.borderWidth * output.height() / 1080.0 / 2.0)) * 2);
            const int x = std::clamp(static_cast<int>(std::lround(widget.area.x() * output.width() / 2.0)) * 2, 0,
                (output.width() - 2) / 2 * 2);
            const int y = std::clamp(static_cast<int>(std::lround(widget.area.y() * output.height() / 2.0)) * 2, 0,
                (output.height() - 2) / 2 * 2);
            const QSize outer(std::min(evenFloor(widget.area.width() * output.width()), (output.width() - x) / 2 * 2),
                std::min(evenFloor(widget.area.height() * output.height()), (output.height() - y) / 2 * 2));
            if (outer.width() <= 2 * border + 2 || outer.height() <= 2 * border + 2) continue;
            const QSize inner(outer.width() - 2 * border, outer.height() - 2 * border);
            Layer layer;
            layer.camera = static_cast<int>(camera);
            layer.border = border;
            layer.borderColor = widget.borderColor;
            layer.crop = widget.crop;
            if (widget.crop) {
                layer.rect = QRect(x, y, outer.width(), outer.height());
                layer.content = layer.rect.adjusted(border, border, -border, -border);
            } else {
                // The box closes up on the picture, centred in the widget's rectangle.
                const QSize picture = fit(sourceSizes[camera], inner);
                const QRect cell(x, y, outer.width(), outer.height());
                layer.content = centred(picture, cell.adjusted(border, border, -border, -border));
                layer.rect = layer.content.adjusted(-border, -border, border, border);
            }
            for (const Segment &stretch : stretches)
                if (stretch.camera != layer.camera) addWindow(layer.windows, stretch.window);
            if (!layer.windows.isEmpty()) result.append(layer);
        }
        return result;
    }
    QVector<int> eligible;
    for (int camera = 0; camera < cameras; ++camera)
        if (!pip.cameras || pip.cameras->contains(cameraIds[camera])) eligible.append(camera);
    int stackSize = 1;
    for (const Segment &stretch : stretches)
        stackSize = std::max(stackSize, static_cast<int>(eligible.size()) - (eligible.contains(stretch.camera) ? 1 : 0));
    const int margin = evenFloor(std::min(output.width(), output.height()) * pip.margin * 1.0);
    const int marginPx = pip.margin <= 0.0 ? 0 : margin;
    QSize box(evenFloor(output.width() * pip.size), evenFloor(output.height() * pip.size));
    if (stackSize * box.height() + (stackSize + 1) * marginPx > output.height())
        box.setHeight(std::max(2, (output.height() - (stackSize + 1) * marginPx) / stackSize / 2 * 2));
    if (box.width() > output.width() - 2 * marginPx) box.setWidth(std::max(2, (output.width() - 2 * marginPx) / 2 * 2));
    const int border = pip.borderWidth <= 0 ? 0
        : std::max(2, static_cast<int>(std::lround(pip.borderWidth * output.height() / 1080.0 / 2.0)) * 2);
    if (box.width() <= 2 * border + 2 || box.height() <= 2 * border + 2) return {};
    const bool right = pip.corner == PipCorner::TopRight || pip.corner == PipCorner::BottomRight;
    const bool bottom = pip.corner == PipCorner::BottomLeft || pip.corner == PipCorner::BottomRight;

    QMap<QPair<int, int>, Layer> boxes; // (slot, camera)
    for (const Segment &stretch : stretches) {
        int slot = 0;
        for (const int camera : std::as_const(eligible)) {
            if (camera == stretch.camera) continue;
            Layer &layer = boxes[{slot, camera}];
            if (layer.windows.isEmpty()) {
                const QSize picture = fit(sourceSizes[camera], QSize(box.width() - 2 * border, box.height() - 2 * border));
                const int width = picture.width() + 2 * border;
                const int height = picture.height() + 2 * border;
                const int slotTop = bottom
                    ? output.height() - marginPx - box.height() - slot * (box.height() + marginPx)
                    : marginPx + slot * (box.height() + marginPx);
                layer.camera = camera;
                layer.rect = QRect(right ? output.width() - marginPx - width : marginPx,
                    bottom ? slotTop + box.height() - height : slotTop, width, height);
                layer.content = layer.rect.adjusted(border, border, -border, -border);
                layer.border = border;
                layer.borderColor = pip.borderColor;
            }
            addWindow(layer.windows, stretch.window);
            ++slot;
        }
    }
    for (const Layer &layer : std::as_const(boxes)) result.append(layer);
    return result;
}

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
