#include "project/VideoChapters.h"
#include "project/ProjectLimits.h"
#include "project/EventProjectCodec.h"

#include <QJsonArray>

namespace FlappedEar::ProjectLimits {
namespace {

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool validateValue(const QJsonValue &value, const qsizetype depth, QString *error)
{
    if (depth > maximumJsonDepth) return fail(error, QStringLiteral("JSON nesting exceeds %1 levels.").arg(maximumJsonDepth));
    if (value.isString() && value.toString().size() > maximumStringCharacters) {
        return fail(error, QStringLiteral("JSON string exceeds %1 characters.").arg(maximumStringCharacters));
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) if (!validateValue(item, depth + 1, error)) return false;
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key().size() > maximumStringCharacters || !validateValue(it.value(), depth + 1, error)) return false;
        }
    }
    return true;
}

bool validateWidgets(const QJsonArray &widgets, QString *error)
{
    if (widgets.size() > maximumWidgets) return fail(error, QStringLiteral("Widget count exceeds %1.").arg(maximumWidgets));
    qsizetype totalCues = 0;
    for (const QJsonValue &value : widgets) {
        if (!value.isObject()) return fail(error, QStringLiteral("Each widget must be a JSON object."));
        const QJsonObject widget = value.toObject();
        const QString id = widget.value(QStringLiteral("id")).toString();
        const QString type = widget.value(QStringLiteral("type")).toString();
        if (id.isEmpty() || id.size() > maximumIdCharacters || type.isEmpty() || type.size() > maximumIdCharacters) {
            return fail(error, QStringLiteral("Widget id or type is missing or too long."));
        }
        const QJsonValue settingsValue = widget.value(QStringLiteral("settings"));
        if (!settingsValue.isUndefined() && !settingsValue.isObject()) return fail(error, QStringLiteral("Widget settings must be an object."));
        if (settingsValue.toObject().size() > maximumSettingsEntries) {
            return fail(error, QStringLiteral("Widget settings exceed %1 entries.").arg(maximumSettingsEntries));
        }
        const QJsonValue elements = settingsValue.toObject().value(QStringLiteral("elements"));
        if (elements.isArray() && elements.toArray().size() > maximumDesignElements) {
            return fail(error, QStringLiteral("A designed widget has more than %1 elements.").arg(maximumDesignElements));
        }
        const QJsonValue cuesValue = widget.value(QStringLiteral("cues"));
        if (!cuesValue.isUndefined() && !cuesValue.isArray()) return fail(error, QStringLiteral("Widget cues must be an array."));
        const qsizetype cues = cuesValue.toArray().size();
        if (cues > maximumCuesPerWidget || (totalCues += cues) > maximumTotalCues) {
            return fail(error, QStringLiteral("Widget cue count exceeds the project limit."));
        }
    }
    return true;
}
}

bool validateProject(const QJsonObject &project, QString *error)
{
    if (!validateValue(project, 0, error)) return false;
    const double version = project.value(QStringLiteral("version")).toDouble();
    if (version != 2.0 && version != 3.0) return fail(error, QStringLiteral("Unsupported project version."));
    if (version == 3.0 && !EventProjectCodec::validate(project, error)) return false;
    if (version == 2.0 && project.contains(QStringLiteral("event"))) return fail(error, QStringLiteral("Events require project version 3."));
    if (version == 2.0 && !VideoChaptersCodec::valid(project.value(QStringLiteral("sources")).toObject().value(QStringLiteral("video")).toObject()))
        return fail(error, QStringLiteral("Video chapters are malformed or do not start with the video itself."));
    const QJsonValue sceneValue = project.value(QStringLiteral("scene"));
    // KAN-123: an event document may be analysis-only (the Telemetry app
    // writes no overlay scene). A scene that is present must still be valid,
    // and single-recording (v2) editor projects always need one.
    if (version == 3.0 && sceneValue.isUndefined()) return true;
    if (!sceneValue.isObject() || !sceneValue.toObject().value(QStringLiteral("widgets")).isArray()) {
        return fail(error, QStringLiteral("Project scene/widgets structure is missing."));
    }
    return validateWidgets(sceneValue.toObject().value(QStringLiteral("widgets")).toArray(), error);
}

bool validateTemplate(const QJsonObject &templateObject, QString *error)
{
    if (!validateValue(templateObject, 0, error)) return false;
    const QString id = templateObject.value(QStringLiteral("id")).toString();
    const QString name = templateObject.value(QStringLiteral("name")).toString();
    const QString description = templateObject.value(QStringLiteral("description")).toString();
    if (id.isEmpty() || id.size() > maximumIdCharacters || name.trimmed().isEmpty() || name.size() > maximumTemplateNameCharacters
        || description.size() > maximumTemplateDescriptionCharacters || !templateObject.value(QStringLiteral("widgets")).isArray()) {
        return fail(error, QStringLiteral("Template metadata is malformed or exceeds its limits."));
    }
    return validateWidgets(templateObject.value(QStringLiteral("widgets")).toArray(), error);
}

bool validateTemplateStore(const QJsonObject &store, QString *error)
{
    if (!validateValue(store, 0, error)) return false;
    if (store.value(QStringLiteral("schemaVersion")).toInt() != 1 || !store.value(QStringLiteral("templates")).isArray()) {
        return fail(error, QStringLiteral("Unsupported template store."));
    }
    const QJsonArray templates = store.value(QStringLiteral("templates")).toArray();
    if (templates.size() > maximumTemplateCount) return fail(error, QStringLiteral("Template store exceeds %1 templates.").arg(maximumTemplateCount));
    for (const QJsonValue &value : templates) if (!value.isObject() || !validateTemplate(value.toObject(), error)) return false;
    return true;
}

bool validateLibraryWidget(const QJsonObject &entry, QString *error)
{
    if (!validateValue(entry, 0, error)) return false;
    const QString id = entry.value(QStringLiteral("id")).toString();
    const QString name = entry.value(QStringLiteral("name")).toString();
    const QString type = entry.value(QStringLiteral("type")).toString();
    if (id.isEmpty() || id.size() > maximumIdCharacters || name.trimmed().isEmpty()
        || name.size() > maximumTemplateNameCharacters || type.isEmpty() || type.size() > maximumIdCharacters) {
        return fail(error, QStringLiteral("Library widget metadata is malformed or exceeds its limits."));
    }
    const QJsonValue settingsValue = entry.value(QStringLiteral("settings"));
    if (!settingsValue.isObject()) return fail(error, QStringLiteral("Library widget settings must be an object."));
    if (settingsValue.toObject().size() > maximumSettingsEntries) {
        return fail(error, QStringLiteral("Widget settings exceed %1 entries.").arg(maximumSettingsEntries));
    }
    const QJsonValue elements = settingsValue.toObject().value(QStringLiteral("elements"));
    if (elements.isArray() && elements.toArray().size() > maximumDesignElements) {
        return fail(error, QStringLiteral("A designed widget has more than %1 elements.").arg(maximumDesignElements));
    }
    return true;
}

bool validateWidgetLibrary(const QJsonObject &library, QString *error)
{
    if (!validateValue(library, 0, error)) return false;
    if (library.value(QStringLiteral("schemaVersion")).toInt() != 1 || !library.value(QStringLiteral("widgets")).isArray()) {
        return fail(error, QStringLiteral("Unsupported widget library."));
    }
    const QJsonArray widgets = library.value(QStringLiteral("widgets")).toArray();
    if (widgets.size() > maximumLibraryWidgets) {
        return fail(error, QStringLiteral("The widget library exceeds %1 widgets.").arg(maximumLibraryWidgets));
    }
    for (const QJsonValue &value : widgets) if (!value.isObject() || !validateLibraryWidget(value.toObject(), error)) return false;
    return true;
}

} // namespace FlappedEar::ProjectLimits
