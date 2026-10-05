#include "FuzzSupport.h"
#include "telemetry/RczParser.h"

#include <QDir>
#include <QFile>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>

// RczParser reads files only, so each input goes through one scratch file.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    static const QString path = QDir::temp().filePath(
        QStringLiteral("flappedear-fuzz-rcz-%1.rcz").arg(QString::number(getpid())));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) std::abort();
    if (file.write(reinterpret_cast<const char *>(data), static_cast<qint64>(size)) != static_cast<qint64>(size)) std::abort();
    file.close();
    fuzzParse([&] { (void) FlappedEar::RczParser::parseFile(path); });
    return 0;
}
