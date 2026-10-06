#pragma once

#include <QJsonValue>
#include <QRegularExpression>
#include <QString>

namespace FlappedEar::FormatVersion {

// KAN-170: FlappedEar Overlays and FlappedEar Telemetry share one .fetproject
// format. A versioned field ("channel-fusion-v1", or an identity such as
// "compatibility-v1:<hash>") that carries a well-formed tag this build does
// not know was written by a newer app. It is kept unchanged and ignored, not
// rejected, so the newer app's document still opens and survives a save.

inline constexpr qsizetype maximumTagCharacters = 128;

[[nodiscard]] inline bool isTag(const QString &tag)
{
    static const QRegularExpression pattern("^[a-z][a-z0-9-]*-v[1-9][0-9]*\\z");
    return tag.size() <= maximumTagCharacters && pattern.match(tag).hasMatch();
}

// A version tag other than the one this build implements.
[[nodiscard]] inline bool isNewerTag(const QJsonValue &value, const QString &knownTag)
{
    return value.isString() && value.toString() != knownTag && isTag(value.toString());
}

// "<tag>:<value>" identities whose tag is not the known one. The value is
// opaque: bounded, nonblank and free of NUL.
[[nodiscard]] inline bool isNewerTaggedIdentity(const QJsonValue &value, const QString &knownTag)
{
    if (!value.isString()) return false;
    const QString text = value.toString();
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon + 1 >= text.size() || text.size() > 4 * maximumTagCharacters
        || text.contains(QChar::Null))
        return false;
    const QString tag = text.left(colon);
    return tag != knownTag && isTag(tag) && !text.mid(colon + 1).trimmed().isEmpty();
}

} // namespace FlappedEar::FormatVersion
