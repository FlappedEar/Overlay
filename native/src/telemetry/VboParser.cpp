#include "telemetry/VboParser.h"
#include "telemetry/TelemetrySessionCache.h"
#include "telemetry/TelemetryGeometry.h"

#include <QFile>
#include <QDateTime>
#include <QTimeZone>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <algorithm>
#include <utility>
#include <cmath>
#include <limits>
#include <numbers>

namespace FlappedEar {
namespace {

struct AliasPattern {
    QString alias;
    QList<QRegularExpression> patterns;
};

QRegularExpression expression(const QString &pattern)
{
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

const QList<AliasPattern> aliasPatterns = {
    {"speed", {expression("^velocity$"), expression("^gps.?speed"), expression("^speed$"), expression("^obd.?speed")}},
    {"rpm", {expression("^rpm(?:[-_].*)?$"), expression("engine.?speed")}},
    {"throttle", {expression("throttle"), expression("accelerator.?pedal")}},
    {"brake", {expression("^brake(?:[-_].*)?$"), expression("brake.?pressure"), expression("brake.?pedal")}},
    {"heartRate", {expression("heart.?rate"), expression("^hr$"), expression("^bpm$")}},
    {"latitude", {expression("^latitude$"), expression("^lat$")}},
    {"longitude", {expression("^longitude$"), expression("^lon(?:g)?$")}},
    {"lateralAcceleration", {expression("^latacc$"), expression("lat(?:eral)?.?(?:accel|acceleration|g)"), expression("^g.?x$")}},
    {"longitudinalAcceleration", {expression("^longacc$"), expression("long(?:itudinal)?.?(?:accel|acceleration|g)"), expression("^g.?y$")}},
};

const QRegularExpression timeName("^(?:time|timestamp|utc.?time)$", QRegularExpression::CaseInsensitiveOption);

QString normalizeName(QString name)
{
    name = name.trimmed();
    if (name.size() >= 2 && ((name.front() == '\'' && name.back() == '\'')
                             || (name.front() == '"' && name.back() == '"'))) {
        name = name.sliced(1, name.size() - 2);
    }
    return name.replace(QRegularExpression("\\s+"), " ");
}

// Visit the logical row without allocating columnSection.join(' '). Section
// lines are already trimmed; a multiline header has one virtual space between lines.
template<typename Visitor>
void visitRow(const QStringList &lines, const CancellationCheck &cancelled, Visitor visit)
{
    qsizetype visited = 0;
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (index && !visit(QLatin1Char(' '))) return;
        for (const QChar character : lines[index]) {
            if ((visited++ & 0xfff) == 0) throwIfCancelled(cancelled);
            if (!visit(character)) return;
        }
    }
}

struct ScannedRow {
    QStringList cells;
    qsizetype count = 0;
};

ScannedRow scanRow(const QStringList &lines, qsizetype retainedColumns,
                   bool columnNames, const CancellationCheck &cancelled)
{
    bool commaSeparated = false;
    visitRow(lines, cancelled, [&](QChar character) {
        commaSeparated = character == ',';
        return !commaSeparated;
    });

    ScannedRow row;
    QString field;
    qsizetype length = 0;
    qsizetype trimmedLength = 0;
    const auto finishField = [&] {
        if (!commaSeparated && !length) return;
        if (columnNames && row.count >= retainedColumns)
            throw ResourceLimitError("VBO contains too many columns.");
        if (row.count < retainedColumns) {
            field.truncate(trimmedLength);
            row.cells.append(std::move(field));
            field = QString{};
        }
        ++row.count;
        length = trimmedLength = 0;
    };
    visitRow(lines, cancelled, [&](QChar character) {
        // Preserve QRegularExpression("\\s+") without Unicode properties.
        const auto code = character.unicode();
        const bool separator = commaSeparated ? character == ','
            : code == ' ' || (code >= '\t' && code <= '\r');
        if (separator) {
            finishField();
            return true;
        }
        if (columnNames && row.count >= retainedColumns)
            throw ResourceLimitError("VBO contains too many columns.");
        // Comma fields are trimmed. Count pending trailing whitespace without
        // allocating it beyond the field budget; reject it only if it is interior.
        if (commaSeparated && !length && character.isSpace()) return true;
        ++length;
        if (!commaSeparated || !character.isSpace()) {
            trimmedLength = length;
            if (trimmedLength > VboParser::kMaximumFieldCharacters)
                throw ResourceLimitError("VBO contains a field longer than the supported 64 KiB limit.");
        }
        if (row.count < retainedColumns && length <= VboParser::kMaximumFieldCharacters)
            field.append(character);
        return true;
    });
    finishField();
    return row;
}

// Every column gets a unique, non-empty name (KAN-147): an empty header
// cell becomes "column N", and a generated "name (n)" never takes a name
// that the header itself uses, so no column overwrites another.
QStringList uniqueNames(const QStringList &input)
{
    QSet<QString> used;
    for (const QString &name : input) if (!name.isEmpty()) used.insert(name);
    QSet<QString> taken;
    QHash<QString, int> counts;
    QStringList output;
    output.reserve(input.size());
    for (qsizetype index = 0; index < input.size(); ++index) {
        const QString base = input[index].isEmpty() ? QStringLiteral("column %1").arg(index + 1) : input[index];
        QString name = base;
        if (taken.contains(name) || (input[index].isEmpty() && used.contains(name))) {
            int count = std::max(1, counts.value(base));
            do name = QStringLiteral("%1 (%2)").arg(base).arg(++count);
            while (taken.contains(name) || used.contains(name));
            counts.insert(base, count);
        }
        taken.insert(name);
        output.append(name);
    }
    return output;
}

enum class TimestampFormat { RelativeSeconds, Clock };

struct ParsedTimestamp {
    double seconds = 0.0;
    TimestampFormat format = TimestampFormat::RelativeSeconds;
};

double checkedTime(const double seconds)
{
    // ProjectSourceReferenceCodec fingerprints durations with llround(seconds
    // * 1e6). Use strict bounds: double(qint64::max()) rounds up to 2^63.
    constexpr double integerLimit = 0x1p63;
    const double microseconds = seconds * 1'000'000.0;
    if (!std::isfinite(seconds) || !std::isfinite(microseconds)
        || microseconds <= -integerLimit || microseconds >= integerLimit) {
        throw VboParseError("VBO timestamp exceeds the supported signed 64-bit microsecond range.");
    }
    return seconds;
}

// Clock syntax is deliberately recognized from the original field text. In
// particular, 003059.500 is 00:30:59.500, not 3,059.5 relative seconds.
std::optional<ParsedTimestamp> parseTimestamp(const QString &value)
{
    const QString text = value.trimmed();
    if (text.contains(':')) {
        static const QRegularExpression colonClock(
            "^(\\d{1,2}):(\\d{2}):(\\d{2}(?:\\.\\d+)?)$");
        const QRegularExpressionMatch match = colonClock.match(text);
        if (!match.hasMatch()) {
            return std::nullopt;
        }
        bool secondsValid = false;
        const int hours = match.captured(1).toInt();
        const int minutes = match.captured(2).toInt();
        const double seconds = match.captured(3).toDouble(&secondsValid);
        if (!secondsValid || !std::isfinite(seconds) || hours < 0 || hours >= 24
            || minutes < 0 || minutes >= 60 || seconds < 0.0 || seconds >= 60.0) {
            return std::nullopt;
        }
        return ParsedTimestamp{hours * 3600.0 + minutes * 60.0 + seconds,
                               TimestampFormat::Clock};
    }

    // A six-digit integer component is always compact HHMMSS syntax. Keep
    // this check textual so leading-zero timestamps before 01:00 are handled.
    static const QRegularExpression compactClockCandidate("^\\d{6}(?:\\..*)?$");
    if (compactClockCandidate.match(text).hasMatch()) {
        static const QRegularExpression compactClock(
            "^(\\d{2})(\\d{2})(\\d{2})(?:\\.(\\d+))?$");
        const QRegularExpressionMatch match = compactClock.match(text);
        if (!match.hasMatch()) {
            return std::nullopt;
        }
        const int hours = match.captured(1).toInt();
        const int minutes = match.captured(2).toInt();
        const int wholeSeconds = match.captured(3).toInt();
        const QString fraction = match.captured(4);
        const double seconds = wholeSeconds + (fraction.isEmpty()
            ? 0.0 : QStringLiteral("0.%1").arg(fraction).toDouble());
        if (!std::isfinite(seconds) || hours >= 24 || minutes >= 60 || seconds >= 60.0) {
            return std::nullopt;
        }
        return ParsedTimestamp{hours * 3600.0 + minutes * 60.0 + seconds,
                               TimestampFormat::Clock};
    }

    bool valid = false;
    const double numeric = text.toDouble(&valid);
    if (!valid || !std::isfinite(numeric)) {
        return std::nullopt;
    }
    return ParsedTimestamp{numeric, TimestampFormat::RelativeSeconds};
}

std::optional<CoordinateAxis> coordinateAxisForName(const QString &name)
{
    const bool latitude = name.compare("lat", Qt::CaseInsensitive) == 0
        || name.compare("latitude", Qt::CaseInsensitive) == 0;
    const bool longitude = name.compare("lon", Qt::CaseInsensitive) == 0
        || name.compare("long", Qt::CaseInsensitive) == 0
        || name.compare("longitude", Qt::CaseInsensitive) == 0;
    if (latitude) return CoordinateAxis::Latitude;
    if (longitude) return CoordinateAxis::Longitude;
    return std::nullopt;
}

struct CoordinateEvidence {
    std::optional<CoordinateUnit> unit;
    QString source;
    QString error;
    bool raceChrono = false;
    bool centreDirection = false;
};

CoordinateEvidence resolveCoordinateEvidence(const QHash<QString, QStringList> &sections,
                                             const CancellationCheck &cancelled)
{
    CoordinateEvidence evidence;
    QString exporter;
    bool conflictingExporters = false;
    qsizetype visited = 0;
    for (const auto &comment : sections.value(QStringLiteral("comments"))) {
        if ((visited++ & 0xff) == 0) throwIfCancelled(cancelled);
        if (!comment.startsWith("Generated by ", Qt::CaseInsensitive)) continue;
        if (!exporter.isEmpty() && exporter.compare(comment, Qt::CaseInsensitive) != 0)
            conflictingExporters = true;
        exporter = comment;
        evidence.raceChrono |= comment.startsWith("Generated by RaceChrono", Qt::CaseInsensitive);
    }
    evidence.centreDirection = !conflictingExporters
        && exporter.compare("Generated by RaceChrono Pro v10.2.4", Qt::CaseInsensitive) == 0;
    if (evidence.centreDirection) {
        // Verified paired RaceChrono Pro 10.2.4 RCZ/VBO export (docs/testing.md).
        // Both samples and gate records use signed total arc-minutes.
        evidence.unit = CoordinateUnit::ArcMinutes;
        evidence.source = "racechrono-pro-10.2.4";
    }
    if (conflictingExporters) evidence.error = "conflicting exporter declarations";
    static const QRegularExpression unitSeparator("[:=]");
    for (const auto &entry : sections.value(QStringLiteral("header"))) {
        if ((visited++ & 0xff) == 0) throwIfCancelled(cancelled);
        const qsizetype separator = entry.indexOf(unitSeparator);
        if (separator < 0 || entry.first(separator).trimmed().compare(
                "coordinate units", Qt::CaseInsensitive) != 0) continue;
        // FlappedEar extension, not an inferred or universal VBO convention.
        const QString value = entry.sliced(separator + 1).trimmed().toLower();
        std::optional<CoordinateUnit> declared;
        if (value == "degrees") declared = CoordinateUnit::Degrees;
        else if (value == "arc-minutes") declared = CoordinateUnit::ArcMinutes;
        if (!declared) evidence.error = "unsupported coordinate units declaration";
        else if (evidence.unit && evidence.unit != declared)
            evidence.error = "conflicting coordinate unit evidence";
        else {
            evidence.unit = declared;
            if (evidence.source.isEmpty()) evidence.source = "header-coordinate-units";
        }
    }
    if (!evidence.unit && evidence.error.isEmpty())
        evidence.error = "no validated exporter or explicit coordinate units declaration";
    if (!evidence.error.isEmpty()) {
        evidence.unit.reset();
        evidence.source = "unresolved";
    }
    return evidence;
}

double normalizeChannelValue(const QString &name, const double value,
                             const std::optional<CoordinateUnit> unit)
{
    const auto axis = coordinateAxisForName(name);
    if (!axis) return value;
    return (unit ? normalizeCoordinateDegrees(*axis, value, *unit) : std::nullopt).value_or(
        std::numeric_limits<double>::quiet_NaN());
}

struct TimingGateParseResult {
    std::optional<TimingGate> gate;
    QString error;
};

TimingGateParseResult parseTimingGate(const QString &line, const bool centreDirection,
                                     const CoordinateUnit unit)
{
    static const QRegularExpression gateLine(
        QStringLiteral("^(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)(?:\\s+(.*))?$"));
    const QRegularExpressionMatch match = gateLine.match(line);
    if (!match.hasMatch()) {
        return {{}, QStringLiteral("expected a gate name and four coordinates")};
    }
    if (match.captured(1).size() > VboParser::kMaximumFieldCharacters) {
        return {{}, QStringLiteral("gate name exceeds the supported field limit")};
    }
    const QString description = match.captured(6).trimmed();
    if (description.size() > 4'096) {
        return {{}, QStringLiteral("gate description exceeds 4096 characters")};
    }
    double sourceCoordinates[4]{};
    for (int index = 0; index < 4; ++index) {
        const QString token = match.captured(index + 2);
        if (token.size() > VboParser::kMaximumFieldCharacters) {
            return {{}, QStringLiteral("coordinate exceeds the supported field limit")};
        }
        bool valid = false;
        sourceCoordinates[index] = token.toDouble(&valid);
        if (!valid || !std::isfinite(sourceCoordinates[index])) {
            return {{}, QStringLiteral("coordinate %1 is not finite").arg(index + 1)};
        }
    }
    const auto longitudeA = normalizeCoordinateDegrees(
        CoordinateAxis::Longitude, sourceCoordinates[0], unit);
    const auto latitudeA = normalizeCoordinateDegrees(
        CoordinateAxis::Latitude, sourceCoordinates[1], unit);
    const auto longitudeB = normalizeCoordinateDegrees(
        CoordinateAxis::Longitude, sourceCoordinates[2], unit);
    const auto latitudeB = normalizeCoordinateDegrees(
        CoordinateAxis::Latitude, sourceCoordinates[3], unit);
    if (!longitudeA || !latitudeA || !longitudeB || !latitudeB) {
        return {{}, QStringLiteral("coordinate is outside the supported degree range")};
    }
    GeoCoordinate endpointA{*latitudeA, *longitudeA};
    GeoCoordinate endpointB{*latitudeB, *longitudeB};
    if (endpointA.latitudeDegrees == endpointB.latitudeDegrees
        && endpointA.longitudeDegrees == endpointB.longitudeDegrees) {
        return {{}, QStringLiteral("gate endpoints must be distinct")};
    }
    if (centreDirection) {
        // Verified RaceChrono Pro 10.2.4 export: A is the centre; A->B is
        // backward travel with magnitude equal to the complete gate width.
        // Rotate in metric space, then extend half that width on either side.
        const auto vector = projectCoordinate(endpointB, endpointA);
        const double width = std::hypot(vector.eastMeters, vector.northMeters);
        if (std::abs(endpointA.latitudeDegrees) >= 89.0 || width < 0.01 || width > 1000.0)
            return {{}, QStringLiteral("unsupported RaceChrono gate width or latitude")};
        constexpr double metersPerDegree = 6'371'000.0 * std::numbers::pi / 180.0;
        const double deltaLat = vector.eastMeters * .5 / metersPerDegree;
        const double deltaLon = -vector.northMeters * .5
            / (metersPerDegree * std::cos(endpointA.latitudeDegrees * std::numbers::pi / 180.0));
        const GeoCoordinate centre = endpointA;
        endpointA = {centre.latitudeDegrees - deltaLat, wrapLongitudeDegrees(centre.longitudeDegrees - deltaLon)};
        endpointB = {centre.latitudeDegrees + deltaLat, wrapLongitudeDegrees(centre.longitudeDegrees + deltaLon)};
        if (!isValidCoordinate(endpointA) || !isValidCoordinate(endpointB))
            return {{}, QStringLiteral("invalid converted RaceChrono gate endpoints")};
    }
    const QString sourceName = match.captured(1);
    TimingGateType type = TimingGateType::Unknown;
    if (sourceName.compare(QStringLiteral("Start"), Qt::CaseInsensitive) == 0) {
        type = TimingGateType::Start;
    } else if (sourceName.compare(QStringLiteral("Split"), Qt::CaseInsensitive) == 0) {
        type = TimingGateType::Split;
    }
    return {TimingGate{type, sourceName, endpointA, endpointB, description}, {}};
}

qsizetype finiteSampleCount(const TelemetrySession &session, const QString &name)
{
    // A const QHash's operator[] returns a copy, so look the channel up in place.
    const auto channel = session.channels.constFind(name);
    if (channel == session.channels.cend()) return 0;
    const auto &values = channel->values();
    return std::count_if(values.cbegin(), values.cend(), [](const float value) { return std::isfinite(value); });
}

// The first matching column by name wins, unless a later match has more than
// twice its finite samples (KAN-230, Telemetry FET-207): a mostly empty first
// column must not hide a full one.
QHash<QString, QString> resolveAliases(const TelemetrySession &session)
{
    const QStringList names = session.channelNames();
    QHash<QString, QString> aliases;
    for (const AliasPattern &aliasPattern : aliasPatterns) {
        // RaceChrono's generic columns may be zero placeholders. Prefer its
        // explicitly calculated vehicle acceleration; preserve every raw column.
        const QString calculated = aliasPattern.alias == QStringLiteral("lateralAcceleration")
            ? QStringLiteral("latacc-calc")
            : aliasPattern.alias == QStringLiteral("longitudinalAcceleration")
                ? QStringLiteral("longacc-calc") : QString();
        if (!calculated.isEmpty()) {
            const auto found = std::find_if(names.cbegin(), names.cend(), [&calculated](const QString &name) {
                return name.compare(calculated, Qt::CaseInsensitive) == 0;
            });
            if (found != names.cend()) {
                aliases.insert(aliasPattern.alias, *found);
                continue;
            }
        }
        QString chosen;
        qsizetype chosenSamples = 0;
        for (const QString &name : names) {
            const bool matched = std::any_of(aliasPattern.patterns.cbegin(), aliasPattern.patterns.cend(),
                [&name](const QRegularExpression &pattern) { return pattern.match(name).hasMatch(); });
            if (!matched) continue;
            const qsizetype samples = finiteSampleCount(session, name);
            if (chosen.isEmpty() || samples > 2 * chosenSamples) {
                chosen = name;
                chosenSamples = samples;
            }
        }
        if (!chosen.isEmpty()) aliases.insert(aliasPattern.alias, chosen);
    }
    return aliases;
}

} // namespace

VboParseError::VboParseError(const QString &message)
    : std::runtime_error(message.toStdString())
{
}

TelemetrySession VboParser::parseFile(const QString &path, const CancellationCheck &cancelled, const qint64 maximumDecodedBytes)
{
    throwIfCancelled(cancelled);
    QFile file(path);
    // Not Text mode: parse() handles CRLF itself, so a file reads exactly as its text.
    if (!file.open(QIODevice::ReadOnly)) {
        throw VboParseError(QStringLiteral("Could not open VBO: %1").arg(file.errorString()));
    }
    if (file.size() > kMaximumFileBytes) {
        throw ResourceLimitError("VBO exceeds the supported 128 MiB file size limit.");
    }
    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(file.size()));
    constexpr qint64 chunkSize = 1024 * 1024;
    while (!file.atEnd()) {
        throwIfCancelled(cancelled);
        const QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            throw VboParseError(QStringLiteral("Could not read VBO: %1").arg(file.errorString()));
        }
        if (chunk.size() > kMaximumFileBytes - bytes.size()) {
            throw ResourceLimitError("VBO exceeds the supported 128 MiB file size limit.");
        }
        bytes.append(chunk);
    }
    throwIfCancelled(cancelled);
    return parse(QString::fromUtf8(bytes), cancelled, maximumDecodedBytes);
}

TelemetrySession VboParser::parse(QStringView text, const CancellationCheck &cancelled, const qint64 maximumDecodedBytes)
{
    throwIfCancelled(cancelled);
    if (text.size() > kMaximumFileBytes) {
        throw ResourceLimitError("VBO text exceeds the supported complexity limit.");
    }
    if (text.startsWith(QChar::ByteOrderMark)) {
        text = text.sliced(1);
    }
    QHash<QString, QStringList> sections;
    // Sections in first-appearance order: metadata is built in this order, so
    // generated keys and repeated keys never depend on QHash's per-process
    // seed (KAN-182).
    QStringList sectionOrder{QString()};
    QString section;
    QStringList *current = &sections[section];
    qsizetype dataRows = 0;
    QString dataSectionName, columnSectionName;
    qsizetype lineIndex = 0;
    qsizetype start = 0;
    static const QRegularExpression sectionPattern("^\\[([^\\]]+)\\]$");
    // Include the final empty line, as split("\\r?\\n") did. Check each limit
    // before creating a line string or growing a section's list.
    for (qsizetype position = 0; position <= text.size(); ++position) {
        if ((position & 0xfff) == 0) throwIfCancelled(cancelled);
        if (position < text.size() && text[position] != '\n') {
            const qsizetype length = position - start + 1;
            const bool crlf = text[position] == '\r' && position + 1 < text.size()
                && text[position + 1] == '\n';
            if (length > kMaximumLineCharacters && !crlf)
                throw ResourceLimitError("VBO contains a line longer than the supported 1 MiB limit.");
            continue;
        }
        if (++lineIndex > kMaximumLines)
            throw ResourceLimitError("VBO contains too many lines.");
        if ((lineIndex & 0xff) == 0) throwIfCancelled(cancelled);
        qsizetype end = position;
        if (position < text.size() && end > start && text[end - 1] == '\r') --end;
        const QStringView view = text.sliced(start, end - start).trimmed();
        start = position + 1;
        if (view.isEmpty() || view.startsWith(';') || view.startsWith('#')) {
            continue;
        }
        const auto match = sectionPattern.matchView(view);
        if (match.hasMatch()) {
            if (match.capturedLength(1) > kMaximumSectionNameCharacters)
                throw ResourceLimitError("VBO contains a section name longer than the supported 256 characters.");
            section = match.captured(1).trimmed().toLower();
            // One data and one column-names section. A repeated header of the
            // same section continues it; two differently named ones would be
            // chosen by hash order, so they are rejected (KAN-147).
            const bool data = section.startsWith("data"), columns = section.contains("column");
            if ((data && !dataSectionName.isNull() && dataSectionName != section)
                || (columns && !columnSectionName.isNull() && columnSectionName != section))
                throw VboParseError("VBO contains more than one data or column-names section.");
            if (data) dataSectionName = section;
            if (columns) columnSectionName = section;
            if (!sections.contains(section)) sectionOrder.append(section);
            current = &sections[section]; // a reference, refreshed whenever the hash may grow
        } else {
            if (section.startsWith("data") && ++dataRows > kMaximumDataRows) {
                throw ResourceLimitError("VBO contains too many data rows.");
            }
            current->append(view.toString());
        }
    }

    TelemetrySession session;
    constexpr qsizetype warningLimit = 200;
    qsizetype omittedWarnings = 0;
    const auto appendWarning = [&session, &omittedWarnings](QString warning) {
        if (session.warnings.size() < warningLimit) {
            session.warnings.append(std::move(warning));
        } else {
            ++omittedWarnings;
        }
    };
    constexpr qsizetype maximumTimingGates = 128;
    const QStringList timingLines = sections.value(QStringLiteral("laptiming"));
    const auto coordinates = resolveCoordinateEvidence(sections, cancelled);
    const bool raceChrono = coordinates.raceChrono;
    const bool centreDirection = coordinates.centreDirection;
    if (raceChrono && !centreDirection && !timingLines.isEmpty())
        appendWarning("Timing gates ignored: this RaceChrono exporter version has not been validated. Telemetry remains available.");
    bool timingLimitWarningAdded = false;
    for (qsizetype index = 0; index < timingLines.size(); ++index) {
        if ((index & 0x3f) == 0) throwIfCancelled(cancelled);
        if (!coordinates.unit || (raceChrono && !centreDirection)) break;
        const TimingGateParseResult parsed = parseTimingGate(timingLines[index], centreDirection, *coordinates.unit);
        if (!parsed.gate) {
            appendWarning(QStringLiteral("Timing line %1 ignored: %2.")
                              .arg(index + 1)
                              .arg(parsed.error));
            continue;
        }
        if (session.timingGates.size() >= maximumTimingGates) {
            if (!timingLimitWarningAdded) {
                appendWarning(QStringLiteral(
                    "Additional timing gates ignored after the supported limit of 128."));
                timingLimitWarningAdded = true;
            }
            continue;
        }
        session.timingGates.append(*parsed.gate);
    }
    qsizetype metadataEntries = 0;
    qsizetype metadataCharacters = 0;
    for (const QString &name : std::as_const(sectionOrder)) {
        throwIfCancelled(cancelled);
        if (name.contains("column") || name.contains("data") || name == QStringLiteral("laptiming")) {
            continue;
        }
        for (const QString &entry : *sections.constFind(name)) {
            if ((metadataEntries & 0xff) == 0) throwIfCancelled(cancelled);
            // Bounded before a key or value is built (KAN-147).
            metadataCharacters += entry.size() + name.size() + 16;
            if (++metadataEntries > kMaximumMetadataEntries || metadataCharacters > kMaximumMetadataCharacters)
                throw ResourceLimitError("VBO header metadata exceeds the supported size.");
            const qsizetype separator = entry.indexOf(QRegularExpression("[:=]"));
            if (separator >= 0) {
                session.metadata.insert(normalizeName(entry.first(separator)), entry.sliced(separator + 1).trimmed());
            } else {
                session.metadata.insert(QStringLiteral("%1.%2").arg(name).arg(session.metadata.size()), entry);
            }
        }
    }

    QStringList columnSection;
    QStringList dataSection;
    for (auto iterator = sections.cbegin(); iterator != sections.cend(); ++iterator) {
        if (columnSection.isEmpty() && iterator.key().contains("column")) {
            columnSection = iterator.value();
        }
        if (dataSection.isEmpty() && iterator.key().startsWith("data")) {
            dataSection = iterator.value();
        }
    }
    if (columnSection.isEmpty()) {
        throw VboParseError("VBO has no [column names] section.");
    }
    if (dataSection.isEmpty()) {
        throw VboParseError("VBO has no [data] rows.");
    }

    QStringList names = scanRow(columnSection, kMaximumColumns, true, cancelled).cells;
    if (names.isEmpty()) {
        throw VboParseError("VBO contains no column names.");
    }
    for (QString &name : names) {
        name = normalizeName(name);
    }
    const bool hasCoordinates = std::any_of(names.cbegin(), names.cend(), [](const QString &name) {
        return coordinateAxisForName(name).has_value();
    });
    if (!coordinates.unit && (hasCoordinates || !timingLines.isEmpty()))
        appendWarning(QStringLiteral("GPS coordinates and timing gates unavailable: %1. "
                                     "Other telemetry remains available.").arg(coordinates.error));
    // Derived metadata cannot be replaced by arbitrary input metadata.
    session.metadata.insert("gpsCoordinateUnit", coordinates.unit
        ? (*coordinates.unit == CoordinateUnit::Degrees ? "degrees" : "arc-minutes") : "unresolved");
    session.metadata.insert("gpsCoordinateEvidence", coordinates.source);
    session.metadata.remove("timingGateFormat");
    if (centreDirection && coordinates.unit)
        session.metadata.insert("timingGateFormat", "racechrono-pro-10.2.4-centre-direction");
    names = uniqueNames(names);
    qsizetype timeIndex = -1;
    for (qsizetype index = 0; index < names.size(); ++index) {
        if (timeName.match(names[index]).hasMatch()) {
            timeIndex = index;
            break;
        }
    }
    // Never invent timing (KAN-207): without a recognised time column the
    // sample rate is unknown, and row numbers would silently pass as seconds.
    if (timeIndex < 0)
        throw VboParseError("VBO has no recognised time column (time, timestamp or utc time); "
                            "its sample timing is unknown.");

    // Every load path decodes through here: budget rows x columns before any
    // value vector grows (KAN-147).
    if (static_cast<qint64>(dataSection.size()) * names.size() > kMaximumDecodedValues)
        throw ResourceLimitError("VBO has more values (rows x columns) than the supported 40 million.");
    // The analysis cache reserves one shared allowance before decoding. Charge
    // every channel's timestamp/value capacity conservatively, even though VBO
    // timestamps share a buffer. Reject before the large sample vectors grow.
    const bool boundedDecode = maximumDecodedBytes != std::numeric_limits<qint64>::max();
    if (boundedDecode) {
        qint64 overhead = telemetrySessionMemoryBytes(session) + 1024 * 1024;
        for (const auto &name : names) overhead += 6 * name.capacity() + 1024;
        const qint64 rowBytes = names.size() * (sizeof(double) + sizeof(float));
        if (maximumDecodedBytes <= overhead || dataSection.size() > (maximumDecodedBytes - overhead) / rowBytes)
            throw ResourceLimitError("Recording exceeds the remaining shared analysis memory budget. Clear an unused lap or close its inspector.");
    }
    QVector<QVector<float>> rawValues(names.size());
    QVector<double> rawTimes;
    if (boundedDecode) {
        rawTimes.reserve(dataSection.size());
        for (auto &values : rawValues) values.reserve(dataSection.size());
    }
    std::optional<double> origin;
    bool originIsClock = false;
    std::optional<double> previousAbsoluteTime;
    std::optional<double> previousClockTime;
    double clockDayOffset = 0.0;
    // A backward clock is a midnight rollover when, read that way, the time
    // moved forward by at most three hours (FlappedEar Telemetry FET-211).
    constexpr double secondsPerDay = 24.0 * 3600.0;
    constexpr double maximumRolloverGap = 3.0 * 3600.0;
    for (qsizetype rowIndex = 0; rowIndex < dataSection.size(); ++rowIndex) {
        if ((rowIndex & 0xff) == 0) throwIfCancelled(cancelled);
        const auto row = scanRow({dataSection[rowIndex]}, names.size(), false, cancelled);
        const QStringList &cells = row.cells;
        if (cells.size() < names.size()) {
            appendWarning(QStringLiteral("Row %1: missing %2 value(s).")
                              .arg(rowIndex + 1)
                              .arg(names.size() - cells.size()));
        }
        if (row.count > names.size()) {
            appendWarning(QStringLiteral("Row %1: ignored %2 extra value(s).")
                              .arg(rowIndex + 1)
                              .arg(row.count - names.size()));
        }
        const auto parsedTime = timeIndex < cells.size() ? parseTimestamp(cells[timeIndex])
                                                         : std::optional<ParsedTimestamp>{};
        if (!parsedTime) {
            const QString timestampText = timeIndex < cells.size() ? cells[timeIndex] : QString();
            appendWarning(QStringLiteral("Row %1: invalid timestamp \"%2\"; row skipped.")
                              .arg(rowIndex + 1)
                              .arg(timestampText));
            continue;
        }
        double absoluteTime = checkedTime(parsedTime->seconds);
        if (parsedTime->format == TimestampFormat::Clock) {
            if (previousClockTime && previousAbsoluteTime
                && parsedTime->seconds < *previousClockTime
                && parsedTime->seconds + secondsPerDay - *previousClockTime <= maximumRolloverGap) {
                clockDayOffset = checkedTime(clockDayOffset + secondsPerDay);
                appendWarning(QStringLiteral("Row %1: midnight rollover detected.").arg(rowIndex + 1));
            }
            absoluteTime = checkedTime(absoluteTime + clockDayOffset);
        }
        if (!origin) {
            origin = absoluteTime;
            originIsClock = parsedTime->format == TimestampFormat::Clock;
        }
        const double timestamp = checkedTime(absoluteTime - *origin);
        if (previousAbsoluteTime) {
            if (absoluteTime == *previousAbsoluteTime) {
                // Keep the first row and skip later duplicates so every
                // emitted channel remains aligned on strictly increasing time.
                appendWarning(QStringLiteral("Row %1: duplicate timestamp %2; later row skipped.")
                                  .arg(rowIndex + 1)
                                  .arg(timestamp, 0, 'f', 3));
                continue;
            }
            if (absoluteTime < *previousAbsoluteTime) {
                appendWarning(QStringLiteral("Row %1: timestamp moved backward from %2 to %3; row skipped.")
                                  .arg(rowIndex + 1)
                                  .arg(rawTimes.back(), 0, 'f', 3)
                                  .arg(timestamp, 0, 'f', 3));
                continue;
            }
        }
        // Distinct absolute doubles can collapse to the same elapsed double
        // after subtraction. Reject before publishing any channel sample.
        if (timestamp < 0.0 || (!rawTimes.isEmpty() && timestamp <= rawTimes.back()))
            throw VboParseError("VBO derived timestamps are not strictly increasing.");
        rawTimes.append(timestamp);
        for (qsizetype column = 0; column < names.size(); ++column) {
            bool valid = false;
            const double parsed = column < cells.size() ? cells[column].toDouble(&valid) : 0.0;
            // Beyond float range is no data, not infinity or undefined behaviour.
            const double normalized = valid && std::isfinite(parsed)
                ? normalizeChannelValue(names[column], parsed, coordinates.unit)
                : std::numeric_limits<double>::quiet_NaN();
            rawValues[column].append(std::isfinite(normalized) && std::abs(normalized) <= std::numeric_limits<float>::max()
                                         ? static_cast<float>(normalized)
                                         : std::numeric_limits<float>::quiet_NaN());
        }
        previousAbsoluteTime = absoluteTime;
        previousClockTime = parsedTime->format == TimestampFormat::Clock
            ? std::optional<double>(parsedTime->seconds) : std::nullopt;
    }
    if (omittedWarnings > 0) {
        session.warnings.append(
            QStringLiteral("… %1 additional parser warnings omitted.").arg(omittedWarnings));
    }
    if (rawTimes.isEmpty()) {
        throw VboParseError("VBO contains no valid timestamped data rows.");
    }
    for (qsizetype index = 1; index < rawTimes.size(); ++index) {
        if ((index & 0xfff) == 0) throwIfCancelled(cancelled);
        if (!(rawTimes[index] > rawTimes[index - 1])) {
            throw VboParseError("VBO parser produced non-monotonic timestamps.");
        }
    }

    for (qsizetype column = 0; column < names.size(); ++column) {
        if ((column & 0x1f) == 0) throwIfCancelled(cancelled);
        if (column == timeIndex) {
            continue;
        }
        bool containsNumericValue = false;
        qsizetype valueIndex = 0;
        for (const float value : rawValues[column]) {
            if ((valueIndex++ & 0xfff) == 0) throwIfCancelled(cancelled);
            if (std::isfinite(value)) {
                containsNumericValue = true;
                break;
            }
        }
        if (!containsNumericValue) {
            continue;
        }
        TelemetryChannel channel;
        channel.name = names[column];
        channel.setSamples(rawTimes, std::move(rawValues[column]));
        session.channels.insert(channel.name, std::move(channel));
    }
    session.duration = checkedTime(rawTimes.back() - rawTimes.front());
    session.startTime = checkedTime(origin.value_or(0.0));
    // Only a recognized RaceChrono export with a valid date and clock establishes
    // UTC chronology. Relative seconds and arbitrary filenames do not establish it.
    session.metadata.remove("firstTimestampMilliseconds");
    session.metadata.remove("gpsLongitudeConvention");
    if (centreDirection && coordinates.unit) session.metadata.insert("gpsLongitudeConvention", "west-positive");
    if (raceChrono && originIsClock) {
        static const QRegularExpression createdPattern(
            "^File created on (\\d{2}/\\d{2}/\\d{4}) at (\\d{2}:\\d{2}:\\d{2})$");
        QStringList creationLines;
        for (const auto &line : sections.value(QString{}))
            if (line.startsWith("File created on ")) creationLines.append(line);
        if (creationLines.size() == 1) {
            const auto match = createdPattern.match(creationLines.first());
            QDate date = QDate::fromString(match.captured(1), "dd/MM/yyyy");
            const QTime createdTime = QTime::fromString(match.captured(2), "HH:mm:ss");
            if (match.hasMatch() && date.isValid() && createdTime.isValid()) {
                if (checkedTime(session.startTime + 12 * 3600) < QTime(0, 0).secsTo(createdTime))
                    date = date.addDays(1);
                const QDateTime midnight(date, QTime(0, 0), QTimeZone::UTC);
                // checkedTime establishes a stricter microsecond bound before
                // this millisecond conversion; the first clock is nonnegative.
                const qint64 offset = std::llround(session.startTime * 1000.0);
                if (!midnight.isValid() || offset < 0
                    || midnight.toMSecsSinceEpoch() > std::numeric_limits<qint64>::max() - offset)
                    throw VboParseError("VBO UTC chronology exceeds the supported date range.");
                session.metadata.insert("firstTimestampMilliseconds",
                    QString::number(midnight.toMSecsSinceEpoch() + offset));
            }
        }
    }
    session.sampleCount = rawTimes.size();
    session.aliases = resolveAliases(session);
    preferAcceleratorPedalForThrottle(session);
    markImplausibleHeartRate(session);
    throwIfCancelled(cancelled);
    return session;
}

} // namespace FlappedEar
