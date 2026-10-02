#include "export/ExportMediaProfile.h"

namespace FlappedEar {

ExportMediaProfile ExportMediaProfile::derive(
    const MediaInfo &source, const QSize &outputSize,
    const MediaRational &frameRate, const qint64 videoBitrate,
    const QString &encoder)
{
    ExportMediaProfile profile;
    profile.outputSize = outputSize;
    profile.frameRate = frameRate;
    profile.videoBitrate = videoBitrate;
    profile.encoder = encoder;
    profile.colorRange = source.colorRange;
    profile.colorSpace = source.colorSpace;
    profile.colorTransfer = source.colorTransfer;
    profile.colorPrimaries = source.colorPrimaries;

    if (!outputSize.isValid() || !frameRate.isValid() || videoBitrate <= 0 || encoder.isEmpty()) {
        profile.error = QStringLiteral("Export media profile is missing a valid size, rate, bitrate, or encoder.");
        return profile;
    }
    if (const QString displayTransformError = unsupportedDisplayTransformError(source);
        !displayTransformError.isEmpty()) {
        profile.error = displayTransformError;
        return profile;
    }
    if (source.sourceColorClass == SourceColorClass::HdrHlg
        || source.sourceColorClass == SourceColorClass::HdrPq
        || source.sourceColorClass == SourceColorClass::LogOrExtended) {
        profile.error = QStringLiteral(
            "%1 source detected. Color-managed HDR/Log overlay preservation is not yet supported; "
            "Flapped Ear Overlays will not silently convert this material to SDR.")
                            .arg(sourceColorClassName(source.sourceColorClass));
        return profile;
    }
    if (!source.bitDepth) {
        profile.error = QStringLiteral(
            "Source bit depth is unknown (pixel format: %1). Export cannot safely preserve it.")
                            .arg(source.pixelFormat.isEmpty() ? QStringLiteral("unknown")
                                                             : source.pixelFormat);
        return profile;
    }
    profile.outputBitDepth = *source.bitDepth;
    if (profile.outputBitDepth == 8) {
        profile.outputPixelFormat = QStringLiteral("yuv420p");
        profile.encoderProfile = QStringLiteral("main");
    } else if (profile.outputBitDepth == 10) {
        profile.outputPixelFormat = encoder == QStringLiteral("hevc_videotoolbox")
            ? QStringLiteral("p010le") : QStringLiteral("yuv420p10le");
        profile.encoderProfile = QStringLiteral("main10");
    } else {
        profile.error = QStringLiteral(
            "%1-bit source preservation is not supported by the current HEVC export policy.")
                            .arg(profile.outputBitDepth);
        return profile;
    }
    profile.supported = true;
    return profile;
}

QString ExportMediaProfile::unsupportedDisplayTransformError(const MediaInfo &source)
{
    if (source.rotationDegrees && *source.rotationDegrees != 0) {
        return QStringLiteral("Export does not yet support videos with rotation metadata.");
    }
    if (source.sampleAspectRatio.isValid()
        && !source.sampleAspectRatio.isEquivalentTo({1, 1})) {
        return QStringLiteral(
            "Export does not yet support non-square pixel aspect ratio (SAR %1:%2).")
            .arg(source.sampleAspectRatio.numerator)
            .arg(source.sampleAspectRatio.denominator);
    }
    return {};
}

bool ExportMediaProfile::acceptsOutputPixelFormat(const QString &actualPixelFormat) const
{
    if (actualPixelFormat == outputPixelFormat) return true;
    if (outputBitDepth == 10 && actualPixelFormat == QStringLiteral("yuv420p10le")) {
        // p010le is VideoToolbox's accepted input surface; decoded HEVC Main10
        // is conventionally reported by ffprobe as planar yuv420p10le.
        return true;
    }
    return outputBitDepth == 8 && outputPixelFormat == QStringLiteral("yuv420p")
        && colorRange == QStringLiteral("pc")
        && actualPixelFormat == QStringLiteral("yuvj420p");
}

} // namespace FlappedEar
