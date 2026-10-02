#pragma once

#include "telemetry/TelemetrySession.h"
#include "telemetry/SourceOperation.h"

#include <QString>
#include <QStringView>
#include <stdexcept>

namespace FlappedEar {

class VboParseError final : public std::runtime_error {
public:
    explicit VboParseError(const QString &message);
};

class VboParser final {
public:
    static constexpr qint64 kMaximumFileBytes = 128LL * 1024 * 1024;
    static constexpr qsizetype kMaximumLines = 1'000'000;
    static constexpr qsizetype kMaximumDataRows = 500'000;
    static constexpr qsizetype kMaximumColumns = 512;
    static constexpr qsizetype kMaximumLineCharacters = 1'048'576;
    static constexpr qsizetype kMaximumFieldCharacters = 65'536;
    // KAN-147: header metadata and decoded values are bounded before they grow.
    static constexpr qsizetype kMaximumSectionNameCharacters = 256;
    static constexpr qsizetype kMaximumMetadataEntries = 10'000;
    static constexpr qsizetype kMaximumMetadataCharacters = 1'048'576;
    static constexpr qint64 kMaximumDecodedValues = 40'000'000; // rows x columns

    [[nodiscard]] static TelemetrySession parse(
        QStringView text, const CancellationCheck &cancelled = {},
        qint64 maximumDecodedBytes = std::numeric_limits<qint64>::max());
    [[nodiscard]] static TelemetrySession parseFile(
        const QString &path, const CancellationCheck &cancelled = {},
        qint64 maximumDecodedBytes = std::numeric_limits<qint64>::max());
};

} // namespace FlappedEar
