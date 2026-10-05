#include "app/BundledFonts.h"

#include <QFontDatabase>
#include <QString>
#include <QStringList>
#include <QDebug>

namespace FlappedEar {

bool registerBundledFonts()
{
    const QStringList fonts{
        QStringLiteral("Sora-400"), QStringLiteral("Sora-500"), QStringLiteral("Sora-600"),
        QStringLiteral("Sora-700"), QStringLiteral("JetBrainsMono-500"),
        QStringLiteral("JetBrainsMono-600"), QStringLiteral("JetBrainsMono-700"),
        // KAN-193: the Tech widget style.
        QStringLiteral("ChakraPetch-500"), QStringLiteral("ChakraPetch-600"),
        QStringLiteral("ChakraPetch-700")};
    bool loaded = true;
    for (const QString &font : fonts) {
        const QString path = QStringLiteral(":/flappedear/resources/fonts/%1.ttf").arg(font);
        if (QFontDatabase::addApplicationFont(path) < 0) {
            qWarning().noquote() << "Bundled font not loaded:" << path;
            loaded = false;
        }
    }
    return loaded;
}

} // namespace FlappedEar
