#include "FuzzSupport.h"
#include "gopro/GoProTelemetrySource.h"

#include <QByteArray>
#include <QVector>

#include <cstddef>
#include <cstdint>
#include <cstring>

// The first byte splits the input into up to four packets, so the decoder
// also sees GPS records continued across packet boundaries.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    if (size == 0) return 0;
    const int count = (data[0] & 0x03) + 1;
    ++data; --size;
    QVector<FlappedEar::GpmfPacket> packets;
    const std::size_t chunk = size / static_cast<std::size_t>(count);
    for (int i = 0; i < count; ++i) {
        const std::size_t begin = chunk * static_cast<std::size_t>(i);
        const std::size_t length = i == count - 1 ? size - begin : chunk;
        packets.append({QByteArray(reinterpret_cast<const char *>(data + begin), static_cast<qsizetype>(length)),
                         static_cast<double>(i), 1.0});
    }
    fuzzParse([&] { (void) FlappedEar::GoProTelemetrySource::decodeGpsPackets(packets, static_cast<double>(count)); });
    return 0;
}
