#include "export/FinalOutputValidation.h"

#include <array>
#include <limits>
#include <numeric>

namespace FlappedEar {
namespace {

bool checkedMultiply(const qint64 left, const qint64 right, qint64 *result)
{
    if (left < 0 || right < 0 || (left != 0 && right > std::numeric_limits<qint64>::max() / left)) return false;
    *result = left * right;
    return true;
}

bool productsAreEqual(std::array<qint64, 3> left, std::array<qint64, 3> right)
{
    for (qint64 &leftFactor : left) {
        for (qint64 &rightFactor : right) {
            const qint64 divisor = std::gcd(leftFactor, rightFactor);
            leftFactor /= divisor;
            rightFactor /= divisor;
        }
    }
    qint64 leftProduct = 1;
    qint64 rightProduct = 1;
    for (const qint64 factor : left) {
        if (!checkedMultiply(leftProduct, factor, &leftProduct)) return false;
    }
    for (const qint64 factor : right) {
        if (!checkedMultiply(rightProduct, factor, &rightProduct)) return false;
    }
    return leftProduct == rightProduct;
}

bool exactContiguousCfrTiming(const FinalOutputEvidence &evidence)
{
    const MediaInfo &media = evidence.finalMedia;
    if (evidence.finalFrameCount <= 0 || media.videoDurationTicks <= 0
        || !media.timeBase.isValid() || !evidence.frameRate.isValid()) return false;
    return productsAreEqual(
        {media.videoDurationTicks, media.timeBase.numerator, evidence.frameRate.numerator},
        {evidence.finalFrameCount, evidence.frameRate.denominator, media.timeBase.denominator});
}

} // namespace

FinalOutputValidationResult FinalOutputValidation::evaluate(const FinalOutputEvidence &evidence)
{
    FinalOutputValidationResult result;
    if (evidence.expectedFrames <= 0 || evidence.finalFrameCount <= 0) return result;

    result.frameDeficit = evidence.expectedFrames - evidence.finalFrameCount;
    result.stageCountsMatch = evidence.stageAGeneratedFrames == evidence.expectedFrames
        && evidence.stageASubmittedFrames == evidence.expectedFrames
        && evidence.temporaryOverlayFrames == evidence.expectedFrames;
    result.stageBProgressMatches = !evidence.stageBProgressFrames
        || *evidence.stageBProgressFrames == evidence.finalFrameCount;
    // An absent start cannot prove the output starts at zero (KAN-148).
    result.startsAtOrigin = evidence.finalMedia.videoStartKnown && evidence.finalMedia.videoStartTicks == 0;
    result.contiguousCfrTiming = exactContiguousCfrTiming(evidence);
    if (!result.stageCountsMatch || !result.stageBProgressMatches || !result.startsAtOrigin
        || !result.contiguousCfrTiming || !evidence.otherValidationPassed
        || !evidence.outputTransactionSafe || evidence.finalFrameCount > evidence.expectedFrames) return result;
    if (result.frameDeficit == 0) result.classification = FinalOutputClassification::Success;
    else if (result.frameDeficit >= 1 && result.frameDeficit <= 10)
        result.classification = FinalOutputClassification::SuccessWithWarning;
    return result;
}

} // namespace FlappedEar
