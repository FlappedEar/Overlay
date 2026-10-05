#include "FuzzSupport.h"
#include "telemetry/VboParser.h"

#include <QString>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(data), static_cast<qsizetype>(size));
    fuzzParse([&] { (void) FlappedEar::VboParser::parse(text); });
    return 0;
}
