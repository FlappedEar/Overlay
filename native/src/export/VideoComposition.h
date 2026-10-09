#pragma once

#include "project/AdditionalVideos.h"
#include "telemetry/TelemetrySession.h"

#include <QJsonArray>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace FlappedEar {

// KAN-131: where export and preview put the run's additional videos, and
// which of their moments each output frame shows. Both use these rules, so
// the preview matches the exported file.
namespace VideoComposition {

// Picture in picture: each additional video fits a box of this share of the
// output's width and height, stacked down the right edge from the top.
inline constexpr double pictureInPictureShare = 0.28;
inline constexpr double marginShare = 0.03;

// The rectangle of each video in an output frame: index 0 is the main video,
// then each additional video in order. `sourceSizes` are the videos' display
// sizes, main first. Each video keeps its aspect ratio, except the main video
// in picture in picture, which fills the frame as it does without additional
// videos. Every position and size is even, as 4:2:0 output needs. Empty when
// a size is not valid.
[[nodiscard]] QVector<QRect> layout(VideoLayoutMode mode, const QSize &output, const QVector<QSize> &sourceSizes);

// KAN-245: picture-in-picture options and camera switching. Cameras are
// numbered by the order of `cameraIds`: index 0 is the main video (id
// `mainCameraId`), then each additional video. Times are seconds on the main
// video's timeline.
struct Window {
    double start = 0.0;
    double end = 0.0; // exclusive
};

// One drawn rectangle of the output: a picture-in-picture box, or the
// on-air camera filling the frame. Layers are in drawing order, later on top;
// the main video fills the frame under all of them.
struct Layer {
    int camera = 0;
    bool onAir = false;
    // Outer rectangle, border included; `content` is where the picture goes
    // (inside the border, or the fitted picture of an on-air camera in the
    // full frame, whose surroundings are black).
    QRect rect;
    QRect content;
    int border = 0;
    QString borderColor;
    // When the layer is drawn. A fading layer keeps its window and ramps its
    // opacity from 0 to 1 over `fadeInSeconds` from each window's start.
    QVector<Window> windows;
    double fadeInSeconds = 0.0;
    // A camera box widget set to fill: the picture is scaled to cover `content`
    // and cropped to it. Otherwise the picture is scaled to fit `content`.
    bool crop = false;
};

// KAN-254: a camera box widget (type `cameraBox`). The box is the widget's
// rectangle, as shares of the output frame, and shows `camera` while another
// camera is on air.
struct CameraBox {
    QString camera;
    QRectF area;
    int borderWidth = 0; // pixels at 1080p scale
    QString borderColor = QStringLiteral("#FFFFFF");
    bool crop = true;
};

// The visible camera box widgets of a scene, in drawing order. A widget with
// no camera, or with a rectangle that is not finite or has no size, is left out.
[[nodiscard]] QVector<CameraBox> cameraBoxes(const QJsonArray &widgets);

// KAN-254: where a new camera box starts: the frame corner (top right first) that
// overlaps `occupied`, the rectangles of the visible widgets as frame shares, the
// least. 28 % of the frame in both directions, which is 28 % wide with the
// main video's aspect.
[[nodiscard]] QRectF startingBoxArea(const QVector<QRectF> &occupied);

// The camera on air at `time`: the last cut at or before it, the main video
// before the first cut or when a cut names a camera that is not in `cameraIds`.
[[nodiscard]] int onAirAt(const ProgramOptions &program, const QStringList &cameraIds, double time);

// The on-air stretches from the cuts, adjacent stretches of one camera joined;
// they cover [0, endSeconds). Empty cuts give the main video throughout.
struct Segment {
    int camera = 0;
    Window window;
};
//
// `available` (empty: every camera has footage throughout) holds, per camera
// in `cameraIds` order, the span of the main video's timeline with footage;
// outside it the main video is on air instead of that camera. Entry 0 is unused.
[[nodiscard]] QVector<Segment> segments(const ProgramOptions &program, const QStringList &cameraIds, double endSeconds,
    const QVector<Window> &available = {});

// Every layer of a picture-in-picture output for the whole run
// (`endSeconds` is when the last cut stays on air until). `sourceSizes` are the
// cameras' display sizes, main first. The boxes keep fixed sizes; the cameras
// off air are packed into slots from the corner, so a camera moves to the slot
// its place among the cameras off air gives. With the picture-in-picture
// switched off there are only on-air layers, and no layers at all while the
// main video is on air throughout. Empty when a size is not valid.
//
// With `boxes` (KAN-254) the picture-in-picture options (corner, size, margin,
// border, cameras) are not used: each box is drawn at its widget's rectangle,
// except boxes of a camera that is not in `cameraIds`, while its camera is not
// on air. The switch still hides every box.
[[nodiscard]] QVector<Layer> plan(const VideoLayout &layout, const QSize &output, const QStringList &cameraIds,
    const QVector<QSize> &sourceSizes, double endSeconds, const QVector<Window> &available = {},
    const QVector<CameraBox> &boxes = {});

// Which moment of an additional video an output frame shows. Both videos'
// syncs map their video time to the same telemetry time, so the additional
// video's time is linear in the main video's.
struct Timing {
    // Where FFmpeg starts reading the additional video, on its own timestamps
    // (its start time included), one second before the first frame needed.
    double inputSeekSeconds = 0.0;
    // Output time = input timestamp x timeFactor + timeShift, in seconds;
    // output time 0 is the export's first frame.
    double timeFactor = 1.0;
    double timeShift = 0.0;
};

// `mainStartSeconds` is the export's first frame on the main video's time,
// `exportSeconds` its duration. `videoStartSeconds` and `videoSeconds` are
// the additional video's first timestamp and duration. Empty when the
// additional video has no frame inside the export, or a value is not finite.
[[nodiscard]] std::optional<Timing> timing(const SyncTransform &mainSync, const SyncTransform &videoSync,
    double mainStartSeconds, double exportSeconds, double videoStartSeconds, double videoSeconds);

} // namespace VideoComposition
} // namespace FlappedEar
