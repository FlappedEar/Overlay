#include "telemetry/RczParser.h"
#include <QFile>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QtEndian>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace FlappedEar {
namespace {
[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error((QStringLiteral("RCZ: ") + message).toStdString());
}
quint16 u16(const QByteArray &b, qsizetype at) { return qFromLittleEndian<quint16>(b.constData() + at); }
quint32 u32(const QByteArray &b, qsizetype at) { return qFromLittleEndian<quint32>(b.constData() + at); }
struct Member {
    QString name;
    quint32 compressed = 0, expanded = 0, crc = 0;
    qint64 dataOffset = 0;
    quint16 method = 0;
};

// ZIP32 stored/deflated members only. Never extract paths to the filesystem.
// Validate the directory AND local headers before allocating/decompressing data.
class Archive {
public:
    Archive(const QString &path, CancellationCheck cancelled) : file(path), cancel(std::move(cancelled))
    {
        throwIfCancelled(cancel);
        if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
        const qint64 size = file.size();
        if (size < 22 || size > RczParser::maximumArchiveBytes) fail("Archive size is unsupported.");
        const qint64 tailAt = std::max<qint64>(0, size - 65557);
        const QByteArray tail = readAt(tailAt, size - tailAt);
        qsizetype end = -1;
        for (qsizetype i = tail.size() - 22; i >= 0; --i) {
            if (u32(tail, i) == 0x06054b50 && i + 22 + u16(tail, i + 20) == tail.size()) { end = i; break; }
        }
        if (end < 0) fail("Missing ZIP directory.");
        const int count = u16(tail, end + 10);
        const qint64 directorySize = u32(tail, end + 12), directoryAt = u32(tail, end + 16);
        if (u16(tail, end + 4) || u16(tail, end + 6) || count != u16(tail, end + 8)
            || count < 1 || count > RczParser::maximumMembers || directorySize > 1024 * 1024
            || directoryAt + directorySize != tailAt + end) fail("Unsupported ZIP64, split archive or directory limits.");
        const QByteArray directory = readAt(directoryAt, directorySize);
        qsizetype at = 0;
        qint64 expandedTotal = 0;
        QVector<QPair<qint64, qint64>> spans;
        for (int index = 0; index < count; ++index) {
            throwIfCancelled(cancel);
            if (at + 46 > directory.size() || u32(directory, at) != 0x02014b50) fail("Invalid ZIP directory entry.");
            const auto flags = u16(directory, at + 8), method = u16(directory, at + 10);
            const auto nameSize = u16(directory, at + 28), extra = u16(directory, at + 30), comment = u16(directory, at + 32);
            if (u16(directory, at + 6) > 20 || (flags & ~quint16(0x080e))
                || (method != 0 && method != 8) || u16(directory, at + 34)
                || nameSize == 0 || nameSize > 512 || at + 46 + nameSize + extra + comment > directory.size())
                fail("Unsupported or malformed ZIP member.");
            const QByteArray rawName = directory.mid(at + 46, nameSize);
            const QString name = QString::fromUtf8(rawName);
            const auto parts = name.split('/');
            if (name.toUtf8() != rawName || name.contains(QChar::Null) || name.contains('\\') || name.contains(':')
                || name.startsWith('/') || parts.contains("..") || parts.contains(".") || name.contains("//"))
                fail("Unsafe archive member path.");
            const quint32 attributes = u32(directory, at + 38);
            if (((attributes >> 16) & 0170000) == 0120000) fail("Symbolic links are unsupported.");
            Member entry{name, u32(directory, at + 20), u32(directory, at + 24), u32(directory, at + 16), 0, method};
            if (members.contains(name) || entry.expanded > RczParser::maximumMemberBytes
                || entry.compressed > RczParser::maximumArchiveBytes
                || (expandedTotal += entry.expanded) > RczParser::maximumExpandedBytes)
                fail("Duplicate member or archive resource limit exceeded.");
            const qint64 localAt = u32(directory, at + 42);
            if (localAt + 30 > directoryAt) fail("Invalid ZIP member offset.");
            const QByteArray local = readAt(localAt, 30);
            const auto localNameSize = u16(local, 26), localExtra = u16(local, 28);
            entry.dataOffset = localAt + 30 + localNameSize + localExtra;
            if (u32(local, 0) != 0x04034b50 || u16(local, 4) != u16(directory, at + 6) || u16(local, 6) != flags || u16(local, 8) != method
                || localNameSize != nameSize || entry.dataOffset + entry.compressed > directoryAt
                || readAt(localAt + 30, localNameSize) != rawName)
                fail("ZIP local header does not match the directory.");
            if (!(flags & 8) && (u32(local, 14) != entry.crc || u32(local, 18) != entry.compressed || u32(local, 22) != entry.expanded))
                fail("ZIP size or checksum headers disagree.");
            if (method == 0 && entry.compressed != entry.expanded) fail("Invalid stored member size.");
            if (name.endsWith('/') && entry.expanded != 0) fail("Invalid directory member.");
            spans.append({localAt, entry.dataOffset + entry.compressed});
            members.insert(name, entry);
            at += 46 + nameSize + extra + comment;
        }
        if (at != directory.size()) fail("Unexpected ZIP directory data.");
        std::sort(spans.begin(), spans.end());
        for (qsizetype i = 1; i < spans.size(); ++i)
            if (spans[i].first < spans[i - 1].second) fail("Overlapping ZIP members.");
    }
    QByteArray data(const QString &name, qint64 limit = RczParser::maximumMemberBytes)
    {
        if (!members.contains(name)) fail(QStringLiteral("Missing %1.").arg(name));
        const Member &entry = members[name];
        if (entry.expanded > limit) fail(QStringLiteral("%1 exceeds its size limit.").arg(name));
        if (!file.seek(entry.dataOffset)) fail("Could not seek archive member.");
        QByteArray result;
        result.reserve(static_cast<qsizetype>(entry.expanded));
        quint32 checksum = static_cast<quint32>(crc32(0, nullptr, 0));
        qint64 remaining = entry.compressed;
        z_stream stream{};
        if (entry.method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK) fail("Could not initialize decompression.");
        const auto close = qScopeGuard([&] { if (entry.method == 8) inflateEnd(&stream); });
        std::array<unsigned char, 65536> output{};
        QByteArray input;
        int status = Z_OK;
        do {
            throwIfCancelled(cancel);
            if (stream.avail_in == 0 && remaining > 0) {
                input = file.read(std::min<qint64>(remaining, 65536));
                if (input.isEmpty()) fail("Truncated archive member.");
                remaining -= input.size();
                stream.next_in = reinterpret_cast<Bytef *>(input.data());
                stream.avail_in = static_cast<uInt>(input.size());
            }
            if (entry.method == 0) {
                result.append(input);
                checksum = static_cast<quint32>(crc32(checksum, reinterpret_cast<const Bytef *>(input.constData()), static_cast<uInt>(input.size())));
                stream.avail_in = 0;
                input.clear();
            } else {
                stream.next_out = output.data();
                stream.avail_out = static_cast<uInt>(output.size());
                const auto before = stream.total_in;
                status = inflate(&stream, Z_NO_FLUSH);
                const qsizetype produced = static_cast<qsizetype>(output.size() - stream.avail_out);
                if (result.size() + produced > entry.expanded) fail("Decompression exceeded declared size.");
                result.append(reinterpret_cast<const char *>(output.data()), produced);
                checksum = static_cast<quint32>(crc32(checksum, output.data(), static_cast<uInt>(produced)));
                if (status != Z_OK && status != Z_STREAM_END) fail("Corrupt compressed member.");
                if (status == Z_STREAM_END) break;
                if (produced == 0 && stream.total_in == before && remaining == 0) fail("Truncated deflate stream.");
            }
        } while (entry.method == 8 || remaining > 0);
        if (remaining || stream.avail_in || result.size() != entry.expanded || checksum != entry.crc)
            fail(QStringLiteral("Size or checksum mismatch in %1.").arg(name));
        return result;
    }
    QMap<QString, Member> members;
private:
    QByteArray readAt(qint64 at, qint64 count)
    {
        if (at < 0 || count < 0 || at + count > file.size() || !file.seek(at)) fail("Archive offset is outside the file.");
        const auto bytes = file.read(count);
        if (bytes.size() != count) fail("Truncated archive.");
        return bytes;
    }
    QFile file;
    CancellationCheck cancel;
};

void validateJson(const QJsonValue &value, int depth = 0)
{
    if (depth > 24 || (value.isString() && value.toString().size() > 4096)) fail("Metadata nesting/string limit exceeded.");
    if (value.isArray()) {
        const auto array = value.toArray();
        if (array.size() > 4096) fail("Metadata array limit exceeded.");
        for (const auto &item : array) validateJson(item, depth + 1);
    } else if (value.isObject()) {
        const auto object = value.toObject();
        if (object.size() > 4096) fail("Metadata object limit exceeded.");
        for (auto i = object.begin(); i != object.end(); ++i) validateJson(i.value(), depth + 1);
    }
}
QJsonObject parseMetadata(const QByteArray &bytes, const QString &name)
{
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) fail(QStringLiteral("Invalid %1 metadata.").arg(name));
    validateJson(doc.object());
    return doc.object();
}
QJsonObject metadata(Archive &archive, const QString &name)
{
    return parseMetadata(archive.data(name, 1024 * 1024), name);
}
qint64 integer(const QJsonObject &object, const QString &name)
{
    const auto value = object.value(name);
    const double number = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(number) || number < 0 || number > 9'007'199'254'740'991.0 || std::floor(number) != number)
        fail(QStringLiteral("Invalid %1 metadata.").arg(name));
    return static_cast<qint64>(number);
}
constexpr double degreesToRadians = std::numbers::pi / 180.0, earthRadiusMeters = 6'371'000;

// A RaceChrono trap: its centre, its width and the travel bearing (degrees
// clockwise from north). The finite gate spans half the width on each side,
// perpendicular to travel.
TimingGate gateAcrossTravel(const double lat, const double lon, const double width, const double bearing,
    const QString &description)
{
    const double east = width * .5 * std::cos(bearing * degreesToRadians);
    const double north = -width * .5 * std::sin(bearing * degreesToRadians);
    const double deltaLat = north / (earthRadiusMeters * degreesToRadians);
    const double deltaLon = east / (earthRadiusMeters * degreesToRadians * std::cos(lat * degreesToRadians));
    TimingGate gate;
    gate.type = TimingGateType::Start; gate.sourceName = "Start";
    gate.sourceDescription = description;
    gate.endpointA = {lat - deltaLat, lon - deltaLon};
    gate.endpointB = {lat + deltaLat, lon + deltaLon};
    return gate;
}

// KAN-204: a session on a RaceChrono library track stores only the track's id
// in trackId.json, not its traps. RaceChrono's own lap boundaries in
// session.json are moments the car crossed the start/finish line, so the line
// is rebuilt there: centred on the mean crossing position, across the mean
// direction of travel, and wide enough to cover every crossing (at least
// RaceChrono's default 20 m trap).
std::optional<TimingGate> gateFromLapBoundaries(const TelemetrySession &session, const QJsonObject &info, const qint64 origin)
{
    QVector<double> times;
    for (const auto &value : info.value("laps").toArray()) {
        const auto lap = value.toObject();
        for (const char *key : {"startTimestamp", "finishTimestamp"}) {
            const double stamp = lap.value(QLatin1String(key)).toDouble(-1);
            if (stamp >= origin && stamp - origin <= 86'400'000 && std::floor(stamp) == stamp)
                times.append((stamp - origin) / 1000.0);
        }
        if (times.size() > 4096) return std::nullopt;
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    struct Crossing { double lat, lon, east, north; };
    QVector<Crossing> crossings;
    for (const double time : times) {
        const auto lat = session.valueAt("latitude", time), lon = session.valueAt("longitude", time);
        const auto beforeLat = session.valueAt("latitude", time - .5), beforeLon = session.valueAt("longitude", time - .5);
        const auto afterLat = session.valueAt("latitude", time + .5), afterLon = session.valueAt("longitude", time + .5);
        if (!lat || !lon || !beforeLat || !beforeLon || !afterLat || !afterLon) continue;
        const double east = (*afterLon - *beforeLon) * degreesToRadians * earthRadiusMeters * std::cos(*lat * degreesToRadians);
        const double north = (*afterLat - *beforeLat) * degreesToRadians * earthRadiusMeters;
        const double length = std::hypot(east, north);
        if (length < 1.0) continue; // standing still: no direction of travel
        crossings.append({*lat, *lon, east / length, north / length});
    }
    if (crossings.size() < 2) return std::nullopt;
    double lat = 0, lon = 0, east = 0, north = 0;
    for (const auto &crossing : crossings) {
        lat += crossing.lat; lon += crossing.lon; east += crossing.east; north += crossing.north;
    }
    lat /= crossings.size(); lon /= crossings.size();
    const double agreement = std::hypot(east, north) / crossings.size();
    if (agreement < .9) return std::nullopt; // crossings in different directions
    const double bearing = std::fmod(std::atan2(east, north) / degreesToRadians + 360.0, 360.0);
    // Offsets along the line (perpendicular to travel) and across it.
    const double alongEast = std::cos(bearing * degreesToRadians), alongNorth = -std::sin(bearing * degreesToRadians);
    double halfWidth = 10.0;
    for (const auto &crossing : crossings) {
        const double dx = (crossing.lon - lon) * degreesToRadians * earthRadiusMeters * std::cos(lat * degreesToRadians);
        const double dy = (crossing.lat - lat) * degreesToRadians * earthRadiusMeters;
        if (std::abs(dx * -alongNorth + dy * alongEast) > 25.0) return std::nullopt; // not one line
        halfWidth = std::max(halfWidth, std::abs(dx * alongEast + dy * alongNorth) + 5.0);
    }
    if (halfWidth > 100.0) return std::nullopt;
    return gateAcrossTravel(lat, lon, 2.0 * halfWidth, bearing, QStringLiteral("Rebuilt from RaceChrono lap times"));
}

struct Mapping { QString name, unit, alias; double scale = 1.0; };
Mapping mapping(int kind, int channel)
{
    if (kind == 1) {
        switch (channel) {
        case 4: return {"velocity", "km/h", "speed", .0036};
        case 5: return {"height", "m", {}, .001};
        case 6: return {"heading", "deg", {}, .001};
        case 46: return {"device_battery_level", "%", {}, .001};
        case 30002: return {"sats", "", {}, 1};
        case 30003: return {"fix_type", "", {}, 1};
        case 30007: return {"accuracy", "m", {}, .001};
        }
    }
    if (kind == 5) {
        switch (channel) {
        case 4: return {"velocity-obd", "km/h", {}, 3.6};
        case 1002: return {"brake_pos-obd", "%", "brake", 1};
        case 1005: return {"gearbox_temp-obd", "°C", {}, 1};
        case 10024: return {"rpm-obd", "rpm", "rpm", 1};
        case 10025: return {"throttle_pos-obd", "%", "throttle", 1};
        case 10026: return {"coolant_temp-obd", "°C", {}, 1};
        case 10029: return {"intake_temp-obd", "°C", {}, 1};
        case 10066: return {"engine_oil_temp-obd", "°C", {}, 1};
        case 10071: return {"accelerator_pos-obd", "%", {}, 1};
        }
    }
    if (kind == 12) {
        // KAN-203: RaceChrono CAN-bus tyre channels. The id is (position << 15)
        // | type: type 1013 is the temperature in °C, 1014 the pressure in kPa,
        // and positions 3 to 6 are RR, RL, FR, FL. Checked against RaceChrono's
        // own VBO export of the same session (owner's Silesia Ring recording,
        // 27 September 2026), whose tyre_*-canbus columns match sample for sample.
        static const char *const corners[] = {"rr", "rl", "fr", "fl"};
        const int position = channel >> 15, type = channel & 0x7fff;
        if (position >= 3 && position <= 6 && type == 1013)
            return {QStringLiteral("tyre_temp_%1-canbus").arg(QLatin1String(corners[position - 3])), "°C", {}, 1};
        if (position >= 3 && position <= 6 && type == 1014)
            return {QStringLiteral("tyre_pressure_%1-canbus").arg(QLatin1String(corners[position - 3])), "kPa", {}, 1};
    }
    if (kind == 6 && channel == 41) return {"heart_rate-hrm", "bpm", "heartRate", .001};
    if (kind == 2 && channel >= 9 && channel <= 11)
        return {QStringLiteral("%1_acc-acc").arg(QChar('x' + channel - 9)), "g", {}, .0001};
    if (kind == 3 && channel >= 12 && channel <= 14)
        return {QStringLiteral("%1_rate_of_rotation-gyro").arg(QChar('x' + channel - 12)), "deg/s", {}, .001};
    if (kind == 8 && channel >= 28 && channel <= 30)
        return {QStringLiteral("%1_magnetic_field-magn").arg(QChar('x' + channel - 28)), "µT", {}, .001};
    return {};
}
} // namespace

TelemetrySession RczParser::parseFile(const QString &path, const CancellationCheck &cancelled, const qint64 maximumDecodedBytes)
{
    Archive archive(path, cancelled);
    const bool boundedDecode = maximumDecodedBytes != std::numeric_limits<qint64>::max();
    if (boundedDecode) {
        // Up to three output samples per input (gap markers), two position
        // channels per eight-byte input, and twelve bytes per output sample.
        // All expanded members are counted, including shared timestamp files.
        qint64 expanded = 0;
        for (const auto &member : archive.members) expanded += member.expanded;
        if (maximumDecodedBytes < 16LL * 1024 * 1024
            || expanded > (maximumDecodedBytes - 16LL * 1024 * 1024) / 9)
            throw ResourceLimitError("Recording exceeds the remaining shared analysis memory budget. Clear an unused lap or close its inspector.");
    }
    // A shared single session has flat members. Backups/resumed archives need a
    // selection/timeline model; never import only the first fragment silently.
    for (auto i = archive.members.cbegin(); i != archive.members.cend(); ++i)
        if (i.key().contains('/')) fail("Multi-session or resumed archives are not supported; share one uninterrupted session.");
    const auto info = metadata(archive, "session.json");
    const auto fragment = metadata(archive, "sessionfragment.json");
    if (integer(info, "version") != 1 || integer(fragment, "version") != 1) fail("Unsupported session version.");
    for (const auto &lap : info.value("laps").toArray())
        if (lap.toObject().value("sessionResume").toInt() != 0) fail("Resumed sessions are not supported yet.");
    const qint64 origin = integer(info, "firstTimestamp");
    if (integer(fragment, "firstTimestamp") != origin) fail("Fragment does not start at the session origin.");
    const qint64 primaryGps = integer(fragment, "primaryGpsDeviceIndex");
    TelemetrySession session;
    session.startTime = (origin % 86'400'000) / 1000.0;
    session.metadata.insert("format", "RaceChrono RCZ v1");
    session.metadata.insert("session", info.value("trackName").toString());
    session.metadata.insert("firstTimestampMilliseconds", QString::number(origin));
    QHash<QString, QVector<double>> clocks;
    qsizetype totalSamples = 0;
    const auto timestamps = [&](const QString &name) -> QVector<double> {
        if (clocks.contains(name)) return clocks.value(name);
        const auto bytes = archive.data(name);
        if (bytes.isEmpty() || bytes.size() % 8 || bytes.size() / 8 > maximumSamples) fail("Invalid timestamp channel length.");
        QVector<double> result;
        result.reserve(bytes.size() / 8);
        qint64 previous = -1;
        for (qsizetype at = 0; at < bytes.size(); at += 8) {
            if ((at & 0x7fff) == 0) throwIfCancelled(cancelled);
            const qint64 tick = qFromLittleEndian<qint64>(bytes.constData() + at);
            if (tick < origin || tick <= previous || tick - origin > 86'400'000) fail("Timestamp channel is nonmonotonic or outside the supported 24-hour session.");
            previous = tick;
            result.append((tick - origin) / 1000.0);
        }
        clocks.insert(name, result);
        return result;
    };
    const auto add = [&](Mapping map, const QVector<double> &times, QVector<float> values, bool primary) {
        if (times.size() != values.size()) fail("Timestamp/value channel lengths differ.");
        totalSamples += values.size();
        if (totalSamples > 8'000'000 || session.channels.size() >= 256) fail("Decoded channel/sample budget exceeded.");
        const QString base = map.name;
        for (int suffix = 2; session.channels.contains(map.name); ++suffix)
            map.name = QStringLiteral("%1 (%2)").arg(base).arg(suffix);
        TelemetryChannel channel;
        channel.name = map.name; channel.unit = map.unit; channel.setSamples(times, std::move(values));
        session.sampleCount = std::max(session.sampleCount, channel.timestamps().size());
        const auto isGap = [&channel](const qsizetype i) {
            return telemetryIsGap(channel, channel.timestamps()[i - 1], channel.timestamps()[i]);
        };
        QVector<double> gapTimes;
        QVector<float> gapValues;
        if (boundedDecode) {
            qsizetype count = channel.timestamps().size();
            for (qsizetype i = 1; i < channel.timestamps().size(); ++i) {
                if ((i & 0xfff) == 0) throwIfCancelled(cancelled);
                if (isGap(i)) count += 2;
            }
            if (count > 8'000'000 - totalSamples + channel.values().size())
                fail("Decoded gap/sample budget exceeded.");
            gapTimes.reserve(count); gapValues.reserve(count);
        }
        for (qsizetype index = 0; index < channel.timestamps().size(); ++index) {
            if ((index & 0xfff) == 0) throwIfCancelled(cancelled);
            const double time = channel.timestamps()[index];
            if (index && isGap(index)) {
                const double previous = channel.timestamps()[index - 1];
                gapTimes.append(std::nextafter(previous, time));
                gapTimes.append(std::nextafter(time, previous));
                gapValues.append(std::numeric_limits<float>::quiet_NaN());
                gapValues.append(std::numeric_limits<float>::quiet_NaN());
                totalSamples += 2;
                if (totalSamples > 8'000'000) fail("Decoded gap/sample budget exceeded.");
            }
            gapTimes.append(time); gapValues.append(channel.values()[index]);
        }
        channel.setSamples(std::move(gapTimes), std::move(gapValues));
        session.duration = std::max(session.duration, channel.timestamps().constLast());
        if (!map.alias.isEmpty() && primary) session.aliases.insert(map.alias, channel.name);
        session.channels.insert(channel.name, std::move(channel));
    };
    static const QRegularExpression channelName("^(channel2?)_([0-9]+)_([0-9]+)_([0-9]+)_([0-9]+)_([0-9]+)$");
    for (auto i = archive.members.cbegin(); i != archive.members.cend(); ++i) {
        throwIfCancelled(cancelled);
        const auto match = channelName.match(i.key());
        if (!match.hasMatch()) {
            if (i.key().startsWith("channel")) fail("Malformed channel filename.");
            continue;
        }
        const int kind = match.captured(2).toInt(), device = match.captured(3).toInt();
        const int channelId = match.captured(5).toInt(), storage = match.captured(6).toInt();
        if (channelId == 1 || channelId == 2) continue; // Timestamps/distance are not sensor values.
        if (kind == 1 && device != primaryGps) continue; // Declared GPS, not a guessed device.
        Mapping map = mapping(kind, channelId);
        const bool position = kind == 1 && channelId == 3;
        if (map.name.isEmpty() && !position) {
            session.warnings.append(QStringLiteral("Unsupported recorded channel: %1").arg(i.key()));
            continue;
        }
        const bool doubleValued = kind == 5 || kind == 12; // OBD and CAN-bus channels
        if ((doubleValued && (storage != 3 || match.captured(1) != "channel2"))
            || (!doubleValued && !position && storage != 0) || (position && storage != 1))
            fail("Unsupported channel value encoding.");
        const QString timeName = QStringLiteral("channel_%1_%2_%3_1_1")
            .arg(match.captured(2), match.captured(3), match.captured(4));
        const auto times = timestamps(timeName);
        const auto bytes = archive.data(i.key());
        const qsizetype stride = position || storage == 3 ? 8 : 4;
        if (bytes.size() != times.size() * stride) fail(QStringLiteral("Timestamp/value length mismatch: %1").arg(i.key()));
        QVector<float> values, longitudes;
        values.reserve(times.size());
        if (position) longitudes.reserve(times.size());
        for (qsizetype at = 0; at < bytes.size(); at += stride) {
            if ((at & 0x7fff) == 0) throwIfCancelled(cancelled);
            double value;
            if (storage == 3) value = std::bit_cast<double>(qFromLittleEndian<quint64>(bytes.constData() + at)) * map.scale;
            else {
                const auto raw = qFromLittleEndian<qint32>(bytes.constData() + at);
                value = raw == std::numeric_limits<qint32>::max() || raw == std::numeric_limits<qint32>::min()
                    ? std::numeric_limits<double>::quiet_NaN() : raw * (position ? 1.0 / 6'000'000.0 : map.scale);
            }
            if (position) {
                const auto raw = qFromLittleEndian<qint32>(bytes.constData() + at + 4);
                double longitude = raw / 6'000'000.0;
                if (!std::isfinite(value) || std::abs(value) > 90 || std::abs(longitude) > 180) {
                    value = std::numeric_limits<double>::quiet_NaN(); longitude = value;
                }
                longitudes.append(static_cast<float>(longitude));
            }
            values.append(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max()
                ? static_cast<float>(value) : std::numeric_limits<float>::quiet_NaN());
        }
        if (position) {
            if (session.aliases.contains("latitude")) fail("Multiple position channels are unsupported.");
            add({"lat", "deg", "latitude", 1}, times, std::move(values), true);
            add({"long", "deg", "longitude", 1}, times, std::move(longitudes), true);
        } else {
            // No device-selection UI exists for ambiguous semantic channels yet.
            if (!map.alias.isEmpty() && session.aliases.contains(map.alias))
                fail(QStringLiteral("Multiple sources for %1 are unsupported.").arg(map.alias));
            add(map, times, std::move(values), true);
        }
    }
    if (!session.aliases.contains("speed") || !session.aliases.contains("latitude")) fail("Declared GPS channels are missing.");
    preferAcceleratorPedalForThrottle(session);
    if (archive.members.contains("trackId.json")) {
        // Archive integrity/resource failures remain fatal even for optional metadata.
        const auto trackBytes = archive.data("trackId.json", 1024 * 1024);
        try {
            const auto track = parseMetadata(trackBytes, "trackId.json").value("track").toObject();
            const auto traps = track.value("traps").toArray();
            if (traps.size() > 64) fail("Too many timing gates.");
            for (const auto &value : traps) {
                const auto trap = value.toObject();
                if (trap.value("type").toInt() != 3 || !trap.value("uniDirectional").toBool()) {
                    session.warnings.append("Unsupported timing gate ignored."); continue;
                }
                const double lat = trap.value("centerLatitude").toDouble(1e20) / 6'000'000.0;
                const double lon = trap.value("centerLongitude").toDouble(1e20) / 6'000'000.0;
                const double width = trap.value("width").toDouble(-1) / 1000.0;
                const double bearing = trap.value("bearing").toDouble(-1) / 1000.0;
                if (std::abs(lat) >= 89.0 || std::abs(lon) > 180 || width <= 0 || width > 1000 || bearing < 0 || bearing >= 360)
                    fail("Invalid timing gate coordinates or geometry.");
                const TimingGate gate = gateAcrossTravel(lat, lon, width, bearing, trap.value("name").toString());
                if (!isValidCoordinate(gate.endpointA) || !isValidCoordinate(gate.endpointB)) fail("Invalid timing gate endpoint.");
                session.timingGates.append(gate);
            }
        } catch (const OperationCancelled &) { throw; }
        catch (const std::exception &error) { session.warnings.append(QString::fromUtf8(error.what())); }
    }
    if (session.timingGates.isEmpty()) {
        if (const auto gate = gateFromLapBoundaries(session, info, origin); gate && isValidCoordinate(gate->endpointA)
            && isValidCoordinate(gate->endpointB)) {
            session.timingGates.append(*gate);
            session.warnings.append("The session's track is from RaceChrono's track library, so the archive has no start/finish line. It was rebuilt from RaceChrono's own lap times.");
        }
    }
    markImplausibleHeartRate(session);
    session.warnings.append("Recorded accelerometer channels, when present, are available as x_acc-acc, y_acc-acc and z_acc-acc in g. These are device axes, not calibrated vehicle lateral/longitudinal G. Calculated G and lean channels are not reconstructed.");
    throwIfCancelled(cancelled);
    return session;
}
} // namespace FlappedEar
