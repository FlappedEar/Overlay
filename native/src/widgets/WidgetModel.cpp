#include "widgets/WidgetModel.h"
#include "project/BoundedJsonLoader.h"
#include "project/ProjectLimits.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJSValue>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QtGlobal>
#include <cmath>
#include <algorithm>
#include <array>
#include <functional>

static void initializeTemplateResources()
{
    Q_INIT_RESOURCE(flappedear_templates);
}

namespace FlappedEar {
namespace {

const QJsonObject &templateCatalog()
{
    static const QJsonObject catalog = [] {
        initializeTemplateResources();
        QFile file(QStringLiteral(":/flappedear/resources/widget-templates.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return QJsonObject();
        }
        const auto loaded = BoundedJsonLoader::loadFile(
            file.fileName(), ProjectLimits::templateBytes, QStringLiteral("Built-in template catalog"));
        return loaded.success() && loaded.document.isObject() ? loaded.document.object() : QJsonObject();
    }();
    return catalog;
}

QVariantList normalizeDesignElements(const QVariant &value);

QVariantMap defaultSettings(const QString &type)
{
    const QJsonObject defaults = templateCatalog().value("widgetDefaults").toObject();
    QVariantMap result = defaults.value("common").toObject().toVariantMap();
    const QVariantMap typeDefaults = defaults.value(type).toObject().toVariantMap();
    for (auto iterator = typeDefaults.cbegin(); iterator != typeDefaults.cend(); ++iterator) {
        result.insert(iterator.key(), iterator.value());
    }
    result.insert("name", type);
    if (result.contains(QStringLiteral("elements"))) {
        result.insert(QStringLiteral("elements"), normalizeDesignElements(result.value(QStringLiteral("elements"))));
    }
    return result;
}

const QStringList widgetTypes = {
    "speed", "heartRate", "pedals", "f1GForceRadar", "gForceMagnitudeBar", "retroCustomValue",
    "lapCurrent", "retroTachometer", "tyres", "designed"};

// KAN-192: types the owner retired on 5 October 2026. A saved project or
// template that still holds one opens without it; the editor reports how many
// were left out. Any other type is a newer version's and is kept unchanged
// (KAN-217).
const QStringList retiredWidgetTypes = {
    "rpm", "gForce", "track", "customValue", "arcGauge", "dialGauge", "telemetryOverlay",
    "lapBest", "lapDelta", "speedBest", "speedCurrent", "speedDelta", "retroGrandPrix",
    "retroGear", "retroPedal", "retroSpeedArc", "retroNameplate", "brandLogo"};

QPair<double, double> defaultSize(const QString &type)
{
    if (type == "heartRate") {
        return {0.12, 0.13};
    }
    if (type == "pedals") {
        return {0.25, 0.13};
    }
    if (type == "f1GForceRadar") {
        return {0.15, 0.20};
    }
    if (type == "gForceMagnitudeBar") {
        return {0.24, 0.10};
    }
    if (type == "retroCustomValue") {
        return {0.20, 0.13};
    }
    if (type == "lapCurrent") {
        return {0.17, 0.14};
    }
    if (type == "retroTachometer") {
        return {0.25, 0.36};
    }
    if (type == "tyres") {
        return {0.17, 0.20};
    }
    if (type == "designed") {
        return {0.20, 0.13};
    }
    return {0.15, 0.16};
}

double bounded(const double value, const double minimum, const double maximum)
{
    return qBound(minimum, value, maximum);
}

double maximumWidgetScale(const WidgetData &widget)
{
    return qMin(3.0, qMin(1.0 / widget.width, 1.0 / widget.height));
}

void constrainWidgetToCanvas(WidgetData *widget)
{
    widget->scale = bounded(widget->scale, 0.25, maximumWidgetScale(*widget));
    widget->x = bounded(widget->x, 0.0, qMax(0.0, 1.0 - widget->width * widget->scale));
    widget->y = bounded(widget->y, 0.0, qMax(0.0, 1.0 - widget->height * widget->scale));
}

double finiteBounded(
    const QVariant &value, const double fallback, const double minimum, const double maximum)
{
    bool ok = false;
    const double candidate = value.toDouble(&ok);
    return ok && std::isfinite(candidate) ? bounded(candidate, minimum, maximum) : fallback;
}

QString templateStorePath()
{
    const QString overridePath = qEnvironmentVariable("FLAPPEDEAR_TEMPLATE_STORE");
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("layout-templates.json"));
}

QString widgetLibraryPath()
{
    const QString overridePath = qEnvironmentVariable("FLAPPEDEAR_WIDGET_LIBRARY");
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("widget-library.json"));
}

bool validTemplateObject(const QJsonObject &item)
{
    if (!ProjectLimits::validateTemplate(item)) {
        return false;
    }
    for (const QJsonValue &value : item.value("widgets").toArray()) {
        if (!value.isObject()) return false;
        const QString type = value.toObject().value("type").toString();
        if (!widgetTypes.contains(type) && !retiredWidgetTypes.contains(type)) return false;
    }
    return true;
}

bool finiteNumber(const QVariant &value, double *result)
{
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number)) return false;
    if (result) *result = number;
    return true;
}

bool colorSetting(const QString &name)
{
    return name.endsWith(QStringLiteral("Color")) || name == QStringLiteral("gridColor")
        || name == QStringLiteral("trackColor") || name == QStringLiteral("panelColor")
        || name == QStringLiteral("valuePlateColor");
}

bool decimalSetting(const QString &name)
{
    return name == QStringLiteral("decimals") || name.endsWith(QStringLiteral("Decimals"));
}

bool opacitySetting(const QString &name)
{
    return name.endsWith(QStringLiteral("Opacity")) || name == QStringLiteral("opacity");
}

bool knownBooleanSetting(const QString &name)
{
    return name.startsWith(QStringLiteral("show")) || name.startsWith(QStringLiteral("invert"))
        || name == QStringLiteral("clampValue") || name == QStringLiteral("mirrorX")
        || name == QStringLiteral("mirrorY") || name == QStringLiteral("hotlapMode");
}

// KAN-191: one element of a designed widget. Geometry is a fraction of the
// widget box; unknown keys are dropped and every value is bounded, so a
// hand-edited project or library file cannot reach the renderer unchecked.
QVariantMap normalizeDesignElement(const QVariantMap &raw, const int index, QSet<QString> *ids)
{
    static const QStringList kinds = {QStringLiteral("text"), QStringLiteral("value"),
                                      QStringLiteral("bar"), QStringLiteral("lap"),
                                      QStringLiteral("shape")};
    const QString kind = raw.value(QStringLiteral("kind")).toString();
    if (!kinds.contains(kind)) return {};

    const auto number = [&raw](const QString &name, const double fallback, const double minimum,
                               const double maximum) {
        return finiteBounded(raw.value(name), fallback, minimum, maximum);
    };
    const auto text = [&raw](const QString &name, const QString &fallback, const qsizetype limit) {
        const QVariant value = raw.value(name);
        return value.isValid() && value.canConvert<QString>() ? value.toString().left(limit) : fallback;
    };
    const auto choice = [&raw](const QString &name, const QStringList &allowed) {
        const QString value = raw.value(name).toString();
        return allowed.contains(value) ? value : allowed.constFirst();
    };
    const auto colour = [&raw](const QString &name, const QString &fallback) {
        const QString value = raw.value(name).toString();
        return QColor(value).isValid() ? value : fallback;
    };
    const auto flag = [&raw](const QString &name, const bool fallback) {
        return raw.contains(name) ? raw.value(name).toBool() : fallback;
    };

    QString id = raw.value(QStringLiteral("id")).toString();
    const bool validId = !id.isEmpty() && id.size() <= 64
        && std::all_of(id.cbegin(), id.cend(), [](const QChar character) {
               return character.isLetterOrNumber() || character == QLatin1Char('-')
                   || character == QLatin1Char('_');
           });
    if (!validId || ids->contains(id)) {
        int suffix = index + 1;
        do {
            id = QStringLiteral("element-%1").arg(suffix++);
        } while (ids->contains(id));
    }
    ids->insert(id);

    const double width = number(QStringLiteral("w"), 0.5, 0.01, 1.0);
    const double height = number(QStringLiteral("h"), 0.3, 0.01, 1.0);
    double minimum = number(QStringLiteral("minValue"), 0.0, -1e9, 1e9);
    double maximum = number(QStringLiteral("maxValue"), 100.0, -1e9, 1e9);
    if (maximum <= minimum) {
        minimum = 0.0;
        maximum = 100.0;
    }
    const QString defaultFill = kind == QStringLiteral("shape") ? QStringLiteral("#16232d")
                                                                : QStringLiteral("#55d76a");
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("name"), text(QStringLiteral("name"), {}, 80)},
        {QStringLiteral("visible"), flag(QStringLiteral("visible"), true)},
        {QStringLiteral("x"), number(QStringLiteral("x"), 0.0, 0.0, 1.0 - width)},
        {QStringLiteral("y"), number(QStringLiteral("y"), 0.0, 0.0, 1.0 - height)},
        {QStringLiteral("w"), width},
        {QStringLiteral("h"), height},
        {QStringLiteral("opacity"), number(QStringLiteral("opacity"), 1.0, 0.0, 1.0)},
        {QStringLiteral("color"), colour(QStringLiteral("color"), QStringLiteral("#f2f5f7"))},
        {QStringLiteral("fillColor"), colour(QStringLiteral("fillColor"), defaultFill)},
        {QStringLiteral("trackColor"), colour(QStringLiteral("trackColor"), QStringLiteral("#2b3a46"))},
        {QStringLiteral("gainColor"), colour(QStringLiteral("gainColor"), QStringLiteral("#20d05a"))},
        {QStringLiteral("lossColor"), colour(QStringLiteral("lossColor"), QStringLiteral("#ef4f5f"))},
        {QStringLiteral("radius"), number(QStringLiteral("radius"), 0.0, 0.0, 200.0)},
        {QStringLiteral("text"), text(QStringLiteral("text"), QStringLiteral("TEXT"), 200)},
        {QStringLiteral("source"), text(QStringLiteral("source"), {}, 128)},
        {QStringLiteral("decimals"), qRound(number(QStringLiteral("decimals"), 0.0, 0.0, 6.0))},
        {QStringLiteral("multiplier"), number(QStringLiteral("multiplier"), 1.0, -1e6, 1e6)},
        {QStringLiteral("valueOffset"), number(QStringLiteral("valueOffset"), 0.0, -1e9, 1e9)},
        {QStringLiteral("prefix"), text(QStringLiteral("prefix"), {}, 40)},
        {QStringLiteral("suffix"), text(QStringLiteral("suffix"), {}, 40)},
        {QStringLiteral("fallbackText"), text(QStringLiteral("fallbackText"), QStringLiteral("—"), 40)},
        {QStringLiteral("minValue"), minimum},
        {QStringLiteral("maxValue"), maximum},
        {QStringLiteral("orientation"), choice(QStringLiteral("orientation"),
             {QStringLiteral("horizontal"), QStringLiteral("vertical")})},
        {QStringLiteral("lapField"), choice(QStringLiteral("lapField"),
             {QStringLiteral("current"), QStringLiteral("best"), QStringLiteral("last"),
              QStringLiteral("delta"), QStringLiteral("lastDelta"), QStringLiteral("lapNumber"),
              QStringLiteral("bestLapNumber")})},
        {QStringLiteral("colorBySign"), flag(QStringLiteral("colorBySign"), true)},
        {QStringLiteral("fontScale"), number(QStringLiteral("fontScale"), 0.75, 0.1, 2.0)},
        {QStringLiteral("align"), choice(QStringLiteral("align"),
             {QStringLiteral("center"), QStringLiteral("left"), QStringLiteral("right")})},
        {QStringLiteral("bold"), flag(QStringLiteral("bold"), true)},
    };
}

QVariantList normalizeDesignElements(const QVariant &value)
{
    QVariantList list = value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant().toList()
                                                     : value.toList();
    QVariantList result;
    QSet<QString> ids;
    for (const QVariant &item : std::as_const(list)) {
        if (result.size() >= ProjectLimits::maximumDesignElements) break;
        const QVariantMap element = normalizeDesignElement(
            item.canConvert<QJSValue>() ? item.value<QJSValue>().toVariant().toMap() : item.toMap(),
            result.size(), &ids);
        if (!element.isEmpty()) result.append(element);
    }
    return result;
}

QVariant normalizeSettingValue(
    const QVariantMap &defaults, const QString &name, const QVariant &value, bool *accepted)
{
    if (accepted) *accepted = true;
    const auto fallback = [&defaults, &name] { return defaults.value(name); };
    double number = 0.0;
    if (name == QStringLiteral("fontSize")) {
        return finiteNumber(value, &number) ? bounded(number, 0.0, 200.0) : fallback();
    }
    if (name == QStringLiteral("elements")) {
        return normalizeDesignElements(value);
    }
    if (decimalSetting(name)) {
        return finiteNumber(value, &number) ? qBound(0, qRound(number), 6) : fallback();
    }
    if (name == QStringLiteral("maxG")) {
        return finiteNumber(value, &number) ? bounded(number, 0.01, 20.0) : fallback();
    }
    if (name == QStringLiteral("deltaRangeSeconds")) {
        return finiteNumber(value, &number) ? bounded(number, 1.0, 60.0) : fallback();
    }
    if (name == QStringLiteral("speedDeltaRangeKmh")) {
        return finiteNumber(value, &number) ? bounded(number, 1.0, 300.0) : fallback();
    }
    if (name == QStringLiteral("style")) {
        // KAN-193: Classic is the original look; Tech the bottom-strip HUD look.
        const QString style = value.toString();
        return style == QStringLiteral("classic") || style == QStringLiteral("tech") ? QVariant(style) : fallback();
    }
    if (name == QStringLiteral("trailSeconds")) {
        return finiteNumber(value, &number) ? bounded(number, 0.0, 5.0) : fallback();
    }
    if (name == QStringLiteral("ringStepG")) {
        return finiteNumber(value, &number) ? bounded(number, 0.01, 10.0) : fallback();
    }
    if (opacitySetting(name)) {
        return finiteNumber(value, &number) ? bounded(number, 0.0, 1.0) : fallback();
    }
    if (name == QStringLiteral("logoScale")) {
        return finiteNumber(value, &number) ? bounded(number, 0.1, 1.0) : fallback();
    }
    if (name == QStringLiteral("valueFontScale") || name == QStringLiteral("labelFontScale")) {
        return finiteNumber(value, &number) ? bounded(number, 0.1, 4.0) : fallback();
    }
    if (name == QStringLiteral("barRadius") || name == QStringLiteral("cornerRadius")
        || name == QStringLiteral("padding") || name == QStringLiteral("borderWidth")
        || name == QStringLiteral("lineWidth") || name == QStringLiteral("markerSize")
        || name == QStringLiteral("dotSize") || name == QStringLiteral("arcWidth")
        || name == QStringLiteral("trackPadding")) {
        return finiteNumber(value, &number) ? bounded(number, 0.0, 200.0) : fallback();
    }
    if (name == QStringLiteral("hotlapLap")) {
        // 0 is the recording's best lap; otherwise a lap number.
        return finiteNumber(value, &number) ? qBound(0, qRound(number), 9999) : fallback();
    }
    if (name == QStringLiteral("segments")) {
        return finiteNumber(value, &number) ? qBound(5, qRound(number), 40) : fallback();
    }
    if (name == QStringLiteral("majorTicks")) {
        return finiteNumber(value, &number) ? qBound(2, qRound(number), 30) : fallback();
    }
    if (name == QStringLiteral("minorTicks")) {
        return finiteNumber(value, &number) ? qBound(0, qRound(number), 10) : fallback();
    }
    if (name == QStringLiteral("gRange")) {
        return finiteNumber(value, &number) ? bounded(number, 0.01, 20.0) : fallback();
    }
    if (name == QStringLiteral("speedMax") || name == QStringLiteral("rpmMax")) {
        return finiteNumber(value, &number) ? bounded(number, 0.01, 100'000.0) : fallback();
    }
    if (colorSetting(name)) {
        const QColor color(value.toString());
        return color.isValid() ? value : fallback();
    }
    if (knownBooleanSetting(name)) return value.toBool();
    if (value.metaType().id() == QMetaType::Double && !finiteNumber(value, &number)) {
        if (accepted) *accepted = false;
        return {};
    }
    return value;
}

bool invalidRangePair(const QVariantMap &settings, const QString &minimum, const QString &maximum)
{
    double minValue = 0.0;
    double maxValue = 0.0;
    return settings.contains(minimum) && settings.contains(maximum)
        && finiteNumber(settings.value(minimum), &minValue)
        && finiteNumber(settings.value(maximum), &maxValue) && maxValue <= minValue;
}

void repairRangePairs(QVariantMap *settings, const QVariantMap &defaults)
{
    for (const auto &[minimum, maximum] : std::array<std::pair<QString, QString>, 4>{
             std::pair{QStringLiteral("minValue"), QStringLiteral("maxValue")},
             std::pair{QStringLiteral("acceleratorMin"), QStringLiteral("acceleratorMax")},
             std::pair{QStringLiteral("brakeMin"), QStringLiteral("brakeMax")},
             std::pair{QStringLiteral("rpmMin"), QStringLiteral("rpmMax")}}) {
        if (invalidRangePair(*settings, minimum, maximum)) {
            settings->insert(minimum, defaults.value(minimum));
            settings->insert(maximum, defaults.value(maximum));
        }
    }
}

void mergeNormalizedSettings(
    QVariantMap *settings, const QVariantMap &incoming, const QVariantMap &defaults)
{
    for (auto iterator = incoming.cbegin(); iterator != incoming.cend(); ++iterator) {
        bool accepted = false;
        const QVariant normalized = normalizeSettingValue(defaults, iterator.key(), iterator.value(), &accepted);
        if (accepted) settings->insert(iterator.key(), normalized);
    }
    repairRangePairs(settings, defaults);
}

void normalizeWidgetGeometry(WidgetData *widget, const QVariantMap &values)
{
    widget->width = finiteBounded(values.value(QStringLiteral("width")), widget->width, 0.04, 1.0);
    widget->height = finiteBounded(values.value(QStringLiteral("height")), widget->height, 0.04, 1.0);
    widget->scale = finiteBounded(
        values.value(QStringLiteral("scale")), widget->scale, 0.25, maximumWidgetScale(*widget));
    widget->x = finiteBounded(
        values.value(QStringLiteral("x")), widget->x, 0.0,
        qMax(0.0, 1.0 - widget->width * widget->scale));
    widget->y = finiteBounded(
        values.value(QStringLiteral("y")), widget->y, 0.0,
        qMax(0.0, 1.0 - widget->height * widget->scale));
    widget->rotation = finiteBounded(values.value(QStringLiteral("rotation")), widget->rotation, -180.0, 180.0);
    widget->opacity = finiteBounded(values.value(QStringLiteral("opacity")), widget->opacity, 0.0, 1.0);
    constrainWidgetToCanvas(widget);
}

QVariantMap normalizeCue(const QVariantMap &raw)
{
    const auto number = [&raw](const QString &name, const double fallback, const double minimum) {
        double value = 0.0;
        return finiteNumber(raw.value(name), &value) ? qMax(minimum, value) : fallback;
    };
    const QString effect = raw.value(QStringLiteral("effect")).toString();
    return {{QStringLiteral("start"), number(QStringLiteral("start"), 0.0, 0.0)},
            {QStringLiteral("duration"), number(QStringLiteral("duration"), 0.1, 0.1)},
            {QStringLiteral("fadeIn"), number(QStringLiteral("fadeIn"), 0.0, 0.0)},
            {QStringLiteral("fadeOut"), number(QStringLiteral("fadeOut"), 0.0, 0.0)},
            {QStringLiteral("effect"), QStringList{QStringLiteral("fade"), QStringLiteral("pop"),
                                                      QStringLiteral("slideUp")}.contains(effect)
                                         ? effect : QStringLiteral("fade")}};
}

bool validPersistedWidgetId(const QString &id)
{
    if (id.isEmpty() || id.size() > 128) return false;
    return std::all_of(id.cbegin(), id.cend(), [](const QChar character) {
        return character.isLetterOrNumber() || character == QLatin1Char('-')
            || character == QLatin1Char('_');
    });
}

bool normalizeTemplateObject(QJsonObject *templateObject)
{
    QJsonArray widgets;
    for (const QJsonValue &value : templateObject->value(QStringLiteral("widgets")).toArray()) {
        QJsonObject widgetObject = value.toObject();
        const QString type = widgetObject.value(QStringLiteral("type")).toString();
        if (retiredWidgetTypes.contains(type)) continue;
        WidgetData widget;
        widget.type = type;
        const auto [defaultWidth, defaultHeight] = defaultSize(type);
        widget.width = defaultWidth;
        widget.height = defaultHeight;
        normalizeWidgetGeometry(&widget, widgetObject.toVariantMap());
        widgetObject.insert(QStringLiteral("x"), widget.x);
        widgetObject.insert(QStringLiteral("y"), widget.y);
        widgetObject.insert(QStringLiteral("width"), widget.width);
        widgetObject.insert(QStringLiteral("height"), widget.height);
        widgetObject.insert(QStringLiteral("scale"), widget.scale);
        widgetObject.insert(QStringLiteral("rotation"), widget.rotation);
        widgetObject.insert(QStringLiteral("opacity"), widget.opacity);

        QVariantMap settings = defaultSettings(type);
        mergeNormalizedSettings(
            &settings, widgetObject.value(QStringLiteral("settings")).toObject().toVariantMap(), settings);
        widgetObject.insert(QStringLiteral("settings"), QJsonObject::fromVariantMap(settings));

        QJsonArray cues;
        for (const QJsonValue &cue : widgetObject.value(QStringLiteral("cues")).toArray()) {
            if (!cue.isObject()) return false;
            cues.append(QJsonObject::fromVariantMap(normalizeCue(cue.toObject().toVariantMap())));
        }
        widgetObject.insert(QStringLiteral("cues"), cues);
        widgets.append(widgetObject);
    }
    templateObject->insert(QStringLiteral("widgets"), widgets);
    return true;
}

// A library entry keeps a widget's type, unscaled size and settings; position,
// cues and group belong to a scene, not to the reusable widget.
bool normalizeLibraryEntry(QJsonObject *entry)
{
    if (!ProjectLimits::validateLibraryWidget(*entry)) return false;
    const QString type = entry->value(QStringLiteral("type")).toString();
    if (!widgetTypes.contains(type)) return false;
    const auto [defaultWidth, defaultHeight] = defaultSize(type);
    entry->insert(QStringLiteral("width"),
                  finiteBounded(entry->value(QStringLiteral("width")).toVariant(), defaultWidth, 0.04, 1.0));
    entry->insert(QStringLiteral("height"),
                  finiteBounded(entry->value(QStringLiteral("height")).toVariant(), defaultHeight, 0.04, 1.0));
    entry->insert(QStringLiteral("name"), entry->value(QStringLiteral("name")).toString().trimmed().left(80));
    QVariantMap settings = defaultSettings(type);
    mergeNormalizedSettings(
        &settings, entry->value(QStringLiteral("settings")).toObject().toVariantMap(), settings);
    entry->insert(QStringLiteral("settings"), QJsonObject::fromVariantMap(settings));
    return true;
}

} // namespace

WidgetModel::WidgetModel(QObject *parent)
    : QAbstractListModel(parent)
{
    loadUserTemplates();
    loadLibrary();
}

int WidgetModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_widgets.size();
}

QVariant WidgetModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_widgets.size()) {
        return {};
    }
    const WidgetData &widget = m_widgets[index.row()];
    switch (role) {
    case WidgetIdRole:
        return widget.id;
    case WidgetTypeRole:
        return widget.type;
    case WidgetXRole:
        return widget.x;
    case WidgetYRole:
        return widget.y;
    case WidgetWidthRole:
        return widget.width;
    case WidgetHeightRole:
        return widget.height;
    case WidgetScaleRole:
        return widget.scale;
    case WidgetRotationRole:
        return widget.rotation;
    case WidgetOpacityRole:
        return widget.opacity;
    case WidgetVisibleRole:
        return widget.visible;
    case WidgetSettingsRole:
        return widget.settings;
    case WidgetCuesRole:
        return widget.cues;
    case WidgetGroupIdRole:
        return widget.groupId;
    default:
        return {};
    }
}

QHash<int, QByteArray> WidgetModel::roleNames() const
{
    return {
        {WidgetIdRole, "widgetId"},         {WidgetTypeRole, "widgetType"},
        {WidgetXRole, "widgetX"},           {WidgetYRole, "widgetY"},
        {WidgetWidthRole, "widgetWidth"},   {WidgetHeightRole, "widgetHeight"},
        {WidgetScaleRole, "widgetScale"},   {WidgetRotationRole, "widgetRotation"},
        {WidgetOpacityRole, "widgetOpacity"},
        {WidgetVisibleRole, "widgetVisible"},
        {WidgetSettingsRole, "widgetSettings"},
        {WidgetCuesRole, "widgetCues"},
        {WidgetGroupIdRole, "widgetGroupId"},
    };
}

int WidgetModel::count() const { return m_widgets.size(); }
int WidgetModel::revision() const { return m_revision; }

QVariantList WidgetModel::templates() const
{
    QVariantList result;
    const QJsonArray builtIns = templateCatalog().value("templates").toArray();
    const QJsonArray templates = allTemplates();
    for (qsizetype index = 0; index < templates.size(); ++index) {
        const QJsonValue value = templates[index];
        const QJsonObject item = value.toObject();
        result.append(QVariantMap{{"id", item.value("id").toString()},
                                  {"name", item.value("name").toString()},
                                  {"description", item.value("description").toString()},
                                  {"widgetCount", item.value("widgets").toArray().size()},
                                  {"builtIn", index < builtIns.size()}});
    }
    return result;
}

const WidgetData *WidgetModel::widgetAt(const int index) const
{
    return index >= 0 && index < m_widgets.size() ? &m_widgets[index] : nullptr;
}

int WidgetModel::addWidget(const QString &type)
{
    if (!validType(type)) {
        return -1;
    }
    if (sceneWidgetCount() >= ProjectLimits::maximumWidgets) {
        setLastError(tr("The scene limit is %1 widgets.").arg(ProjectLimits::maximumWidgets));
        return -1;
    }
    const int index = m_widgets.size();
    beginInsertRows({}, index, index);
    m_widgets.append(createWidget(type, index));
    endInsertRows();
    ++m_revision;
    emit countChanged();
    emit revisionChanged();
    return index;
}

void WidgetModel::removeWidget(const int index)
{
    if (index < 0 || index >= m_widgets.size()) {
        return;
    }
    beginRemoveRows({}, index, index);
    m_widgets.removeAt(index);
    endRemoveRows();
    ++m_revision;
    emit countChanged();
    emit revisionChanged();
}

void WidgetModel::removeWidgets(const QVariantList &indices)
{
    QSet<int> unique;
    for (const QVariant &value : indices) {
        const int index = value.toInt();
        if (index >= 0 && index < m_widgets.size()) {
            unique.insert(index);
        }
    }
    if (unique.isEmpty()) {
        return;
    }
    QList<int> sorted = unique.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    beginResetModel();
    for (const int index : sorted) {
        m_widgets.removeAt(index);
    }
    endResetModel();
    ++m_revision;
    emit countChanged();
    emit revisionChanged();
}

int WidgetModel::duplicateWidget(const int index)
{
    if (index < 0 || index >= m_widgets.size()) {
        return -1;
    }
    if (sceneWidgetCount() >= ProjectLimits::maximumWidgets
        || totalCueCount() + m_widgets[index].cues.size() > ProjectLimits::maximumTotalCues) {
        setLastError(tr("Duplicating this widget would exceed the scene's widget or animation limit."));
        return -1;
    }
    WidgetData copy = m_widgets[index];
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.x += 0.03;
    copy.y += 0.03;
    constrainWidgetToCanvas(&copy);
    copy.groupId.clear();
    const int destination = m_widgets.size();
    beginInsertRows({}, destination, destination);
    m_widgets.append(std::move(copy));
    endInsertRows();
    ++m_revision;
    emit countChanged();
    emit revisionChanged();
    return destination;
}

void WidgetModel::moveWidget(const int index, const double x, const double y)
{
    if (WidgetData *widget = index >= 0 && index < m_widgets.size() ? &m_widgets[index] : nullptr) {
        double deltaX = x - widget->x;
        double deltaY = y - widget->y;
        QList<int> members;
        for (int member = 0; member < m_widgets.size(); ++member) {
            if (member == index || (!widget->groupId.isEmpty() && m_widgets[member].groupId == widget->groupId)) {
                members.append(member);
                deltaX = qMax(deltaX, -m_widgets[member].x);
                deltaX = qMin(deltaX, 1.0 - m_widgets[member].width * m_widgets[member].scale - m_widgets[member].x);
                deltaY = qMax(deltaY, -m_widgets[member].y);
                deltaY = qMin(deltaY, 1.0 - m_widgets[member].height * m_widgets[member].scale - m_widgets[member].y);
            }
        }
        for (const int member : members) {
            m_widgets[member].x += deltaX;
            m_widgets[member].y += deltaY;
        }
        ++m_revision;
        emit dataChanged(this->index(0), this->index(m_widgets.size() - 1));
        emit revisionChanged();
    }
}

void WidgetModel::resizeWidget(const int index, const double width, const double height)
{
    if (WidgetData *widget = index >= 0 && index < m_widgets.size() ? &m_widgets[index] : nullptr) {
        const double maximumDimension = qMin(1.0, 1.0 / widget->scale);
        if (std::isfinite(width)) widget->width = bounded(width, 0.04, maximumDimension);
        if (std::isfinite(height)) widget->height = bounded(height, 0.04, maximumDimension);
        constrainWidgetToCanvas(widget);
        update(index);
    }
}

void WidgetModel::setWidgetProperty(
    const int index, const QString &name, const QVariant &value)
{
    if (index < 0 || index >= m_widgets.size()) {
        return;
    }
    WidgetData &widget = m_widgets[index];
    if (name == "scale") {
        widget.scale = finiteBounded(value, widget.scale, 0.25, maximumWidgetScale(widget));
        constrainWidgetToCanvas(&widget);
    } else if (name == "rotation") {
        widget.rotation = finiteBounded(value, widget.rotation, -180.0, 180.0);
    } else if (name == "opacity") {
        widget.opacity = finiteBounded(value, widget.opacity, 0.0, 1.0);
    } else if (name == "visible") {
        widget.visible = value.toBool();
    } else {
        return;
    }
    update(index);
}

void WidgetModel::setSetting(const int index, const QString &name, const QVariant &value)
{
    if (index < 0 || index >= m_widgets.size() || name.isEmpty()) {
        return;
    }
    WidgetData &widget = m_widgets[index];
    const QVariantMap defaults = defaultSettings(widget.type);
    QVariantMap updated = widget.settings;
    mergeNormalizedSettings(&updated, {{name, value}}, defaults);
    widget.settings = std::move(updated);
    update(index);
}

int WidgetModel::addCue(
    const int index, const double startTime, const double duration, const QString &effect)
{
    if (index < 0 || index >= m_widgets.size()) {
        return -1;
    }
    if (m_widgets[index].cues.size() >= ProjectLimits::maximumCuesPerWidget
        || totalCueCount() >= ProjectLimits::maximumTotalCues) {
        setLastError(tr("The scene's animation limit has been reached."));
        return -1;
    }
    m_widgets[index].cues.append(normalizeCue({{"start", startTime}, {"duration", duration},
                                                {"fadeIn", 0.3}, {"fadeOut", 0.3},
                                                {"effect", effect}}));
    update(index);
    return m_widgets[index].cues.size() - 1;
}

void WidgetModel::setCueProperty(
    const int index, const int cueIndex, const QString &name, const QVariant &value)
{
    if (index < 0 || index >= m_widgets.size() || cueIndex < 0
        || cueIndex >= m_widgets[index].cues.size()) {
        return;
    }
    QVariantMap cue = m_widgets[index].cues[cueIndex].toMap();
    if (name != "start" && name != "duration" && name != "fadeIn" && name != "fadeOut"
        && name != "effect") {
        return;
    }
    cue.insert(name, value);
    m_widgets[index].cues[cueIndex] = normalizeCue(cue);
    update(index);
}

void WidgetModel::removeCue(const int index, const int cueIndex)
{
    if (index < 0 || index >= m_widgets.size() || cueIndex < 0
        || cueIndex >= m_widgets[index].cues.size()) {
        return;
    }
    m_widgets[index].cues.removeAt(cueIndex);
    update(index);
}

void WidgetModel::clearCues(const int index)
{
    if (index < 0 || index >= m_widgets.size() || m_widgets[index].cues.isEmpty()) {
        return;
    }
    m_widgets[index].cues.clear();
    update(index);
}

QString WidgetModel::groupWidgets(const QVariantList &indices)
{
    QSet<int> unique;
    for (const QVariant &value : indices) {
        const int index = value.toInt();
        if (index >= 0 && index < m_widgets.size()) {
            unique.insert(index);
        }
    }
    if (unique.size() < 2) {
        return {};
    }
    const QString groupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const int index : unique) {
        m_widgets[index].groupId = groupId;
    }
    ++m_revision;
    emit dataChanged(this->index(0), this->index(m_widgets.size() - 1));
    emit revisionChanged();
    return groupId;
}

void WidgetModel::ungroupWidget(const int index)
{
    if (index < 0 || index >= m_widgets.size() || m_widgets[index].groupId.isEmpty()) {
        return;
    }
    const QString groupId = m_widgets[index].groupId;
    for (WidgetData &widget : m_widgets) {
        if (widget.groupId == groupId) {
            widget.groupId.clear();
        }
    }
    ++m_revision;
    emit dataChanged(this->index(0), this->index(m_widgets.size() - 1));
    emit revisionChanged();
}

QVariantList WidgetModel::groupMembers(const int index) const
{
    QVariantList result;
    if (index < 0 || index >= m_widgets.size()) {
        return result;
    }
    const QString groupId = m_widgets[index].groupId;
    if (groupId.isEmpty()) {
        result.append(index);
        return result;
    }
    for (int member = 0; member < m_widgets.size(); ++member) {
        if (m_widgets[member].groupId == groupId) {
            result.append(member);
        }
    }
    return result;
}

QVariantMap WidgetModel::widget(const int index) const
{
    const WidgetData *item = widgetAt(index);
    if (!item) {
        return {};
    }
    return {
        {"id", item->id},           {"type", item->type},
        {"x", item->x},             {"y", item->y},
        {"width", item->width},     {"height", item->height},
        {"scale", item->scale},     {"rotation", item->rotation},
        {"opacity", item->opacity}, {"visible", item->visible},
        {"settings", item->settings}, {"cues", item->cues}, {"groupId", item->groupId},
    };
}

void WidgetModel::resetDefaults()
{
    // KAN-192: a new scene starts as the Motorsport Broadcast HUD.
    if (applyTemplate(QStringLiteral("motorsport-broadcast-smoke"))) {
        return;
    }
    beginResetModel();
    m_unknownWidgets.clear();
    m_widgets = {createWidget("speed", 0), createWidget("lapCurrent", 1),
                 createWidget("heartRate", 2)};
    endResetModel();
    ++m_revision;
    emit countChanged();
    emit revisionChanged();
}

bool WidgetModel::applyTemplate(const QString &templateId)
{
    for (const QJsonValue &value : allTemplates()) {
        const QJsonObject item = value.toObject();
        if (item.value("id").toString() != templateId) {
            continue;
        }
        QList<WidgetData> widgets;
        for (const QJsonValue &widgetValue : item.value("widgets").toArray()) {
            const QJsonObject object = widgetValue.toObject();
            const QString type = object.value("type").toString();
            if (retiredWidgetTypes.contains(type)) continue;
            if (!validType(type)) {
                return false;
            }
            WidgetData widget = createWidget(type, widgets.size());
            normalizeWidgetGeometry(&widget, object.toVariantMap());
            widget.visible = object.value("visible").toBool(widget.visible);
            for (const QJsonValue &cue : object.value("cues").toArray()) {
                if (!cue.isObject()) return false;
                widget.cues.append(normalizeCue(cue.toObject().toVariantMap()));
            }
            widget.groupId = object.value("groupId").toString();
            mergeNormalizedSettings(&widget.settings,
                                    object.value("settings").toObject().toVariantMap(),
                                    defaultSettings(type));
            widgets.append(std::move(widget));
        }
        beginResetModel();
        // A template replaces the whole scene, unknown widgets included.
        m_unknownWidgets.clear();
        m_widgets = std::move(widgets);
        endResetModel();
        ++m_revision;
        emit countChanged();
        emit revisionChanged();
        return true;
    }
    return false;
}

QString WidgetModel::saveCurrentAsTemplate(const QString &name, const QString &description)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || m_widgets.isEmpty()) {
        return {};
    }
    const QString id = QStringLiteral("user-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_userTemplates.append(QJsonObject{{"id", id},
                                       {"name", cleanName.left(80)},
                                       {"description", description.trimmed().left(240)},
                                       {"widgets", toJson()}});
    if (!saveUserTemplates()) {
        m_userTemplates.removeLast();
        return {};
    }
    emit templatesChanged();
    return id;
}

bool WidgetModel::updateTemplate(const QString &templateId)
{
    if (templateId.isEmpty() || m_widgets.isEmpty()) {
        return false;
    }
    for (qsizetype index = 0; index < m_userTemplates.size(); ++index) {
        const QJsonObject previous = m_userTemplates[index].toObject();
        if (previous.value("id").toString() != templateId) {
            continue;
        }
        // Keep template identity, user-facing metadata, and unknown compatible fields intact.
        QJsonObject updated = previous;
        updated.insert("widgets", toJson());
        m_userTemplates[index] = updated;
        if (!saveUserTemplates()) {
            m_userTemplates[index] = previous;
            return false;
        }
        emit templatesChanged();
        return true;
    }
    return false;
}

bool WidgetModel::deleteTemplate(const QString &templateId)
{
    for (qsizetype index = 0; index < m_userTemplates.size(); ++index) {
        if (m_userTemplates[index].toObject().value("id").toString() != templateId) {
            continue;
        }
        const QJsonValue removed = m_userTemplates.takeAt(index);
        if (!saveUserTemplates()) {
            m_userTemplates.insert(index, removed);
            return false;
        }
        emit templatesChanged();
        return true;
    }
    return false;
}

bool WidgetModel::exportTemplate(const QString &templateId, const QUrl &url)
{
    QJsonObject selected;
    for (const QJsonValue &value : allTemplates()) {
        if (value.toObject().value("id").toString() == templateId) {
            selected = value.toObject();
            break;
        }
    }
    if (selected.isEmpty()) {
        setLastError(tr("The template to export no longer exists."));
        return false;
    }
    if (!url.isLocalFile()) {
        setLastError(tr("Templates can only be exported to a local file."));
        return false;
    }
    QString path = url.toLocalFile();
    if (!path.endsWith(".fettemplate", Qt::CaseInsensitive)) {
        path.append(".fettemplate");
    }
    QSaveFile file(path);
    const QJsonObject package{{"flappedEarTemplateVersion", 1}, {"template", selected}};
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(package).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        setLastError(tr("Could not export the template to %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    setLastError({});
    return true;
}

QString WidgetModel::importTemplate(const QUrl &url)
{
    if (!url.isLocalFile()) {
        setLastError(tr("Templates can only be imported from a local file."));
        return {};
    }
    if (!m_templateStoreWritable) {
        setLastError(tr("Custom templates could not be loaded, so nothing can be imported. "
                       "Restore the template file and reload templates first."));
        return {};
    }
    const QString path = QDir::toNativeSeparators(url.toLocalFile());
    const auto loaded = BoundedJsonLoader::loadFile(
        url.toLocalFile(), ProjectLimits::templateBytes, QStringLiteral("Template"));
    if (!loaded.success() || !loaded.document.isObject()) {
        setLastError(tr("Could not import %1: %2").arg(
            path, loaded.success() ? tr("the file is not a template") : loaded.error));
        return {};
    }
    const QJsonObject root = loaded.document.object();
    QJsonObject item = root.value("template").toObject();
    if (item.isEmpty()) {
        item = root;
    }
    item.insert("id", QStringLiteral("user-%1").arg(
                          QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!validTemplateObject(item) || !normalizeTemplateObject(&item)) {
        setLastError(tr("Could not import %1: it is not a valid template or uses an "
                       "unsupported widget.").arg(path));
        return {};
    }
    m_userTemplates.append(item);
    if (!saveUserTemplates()) {
        m_userTemplates.removeLast();
        return {};
    }
    emit templatesChanged();
    return item.value("id").toString();
}

void WidgetModel::reloadTemplates()
{
    loadUserTemplates();
    emit templatesChanged();
}

void WidgetModel::setLastError(const QString &error)
{
    if (m_lastError == error) return;
    m_lastError = error;
    emit lastErrorChanged();
}

qsizetype WidgetModel::totalCueCount() const
{
    qsizetype count = unknownCueCount();
    for (const WidgetData &widget : m_widgets) count += widget.cues.size();
    return count;
}

qsizetype WidgetModel::unknownCueCount() const
{
    qsizetype count = 0;
    for (const auto &[index, object] : m_unknownWidgets) count += object.value(QStringLiteral("cues")).toArray().size();
    return count;
}

void WidgetModel::loadUserTemplates()
{
    m_templateStoreWritable = false;
    const QString path = templateStorePath();
    if (!QFileInfo::exists(path)) {
        m_userTemplates = {};
        m_templateStoreWritable = true;
        setLastError({});
        return;
    }
    const auto loaded = BoundedJsonLoader::loadFile(
        path, ProjectLimits::templateStoreBytes, QStringLiteral("Template store"));
    QString error = loaded.error;
    const QJsonObject root = loaded.document.object();
    if (!loaded.success() || !loaded.document.isObject()
        || !ProjectLimits::validateTemplateStore(root, &error)) {
        setLastError(tr("Custom templates could not be loaded from %1: %2. "
                       "The file is preserved; restore it and reload templates before saving.")
                         .arg(QDir::toNativeSeparators(path), error));
        return;
    }
    QJsonArray templates;
    for (const QJsonValue &value : root.value("templates").toArray()) {
        QJsonObject item = value.toObject();
        if (!validTemplateObject(item) || !normalizeTemplateObject(&item)) {
            setLastError(tr("Custom templates contain an unsupported widget. "
                           "The file is preserved; restore it and reload templates before saving."));
            return;
        }
        templates.append(item);
    }
    m_userTemplates = templates;
    m_templateStoreWritable = true;
    setLastError({});
}

bool WidgetModel::saveUserTemplates()
{
    if (!m_templateStoreWritable) return false;
    const QJsonObject root{{"schemaVersion", 1}, {"templates", m_userTemplates}};
    QString error;
    if (!ProjectLimits::validateTemplateStore(root, &error)) {
        setLastError(error);
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (payload.size() > ProjectLimits::templateStoreBytes) {
        setLastError(tr("Custom templates exceed the %1 MiB storage limit. "
                       "Delete an unused template before saving another.")
                         .arg(ProjectLimits::templateStoreBytes / (1024 * 1024)));
        return false;
    }
    const QString path = templateStorePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        setLastError(tr("Could not create the custom template directory."));
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) {
        setLastError(tr("Could not save custom templates: %1").arg(file.errorString()));
        return false;
    }
    setLastError({});
    return true;
}

QJsonArray WidgetModel::allTemplates() const
{
    QJsonArray result = templateCatalog().value("templates").toArray();
    for (const QJsonValue &value : m_userTemplates) {
        result.append(value);
    }
    return result;
}

QJsonArray WidgetModel::toJson() const
{
    QJsonArray array;
    for (const WidgetData &widget : m_widgets) {
        array.append(QJsonObject{{"id", widget.id},
                                 {"type", widget.type},
                                 {"x", widget.x},
                                 {"y", widget.y},
                                 {"width", widget.width},
                                 {"height", widget.height},
                                 {"scale", widget.scale},
                                 {"rotation", widget.rotation},
                                 {"opacity", widget.opacity},
                                 {"visible", widget.visible},
                                 {"cues", QJsonArray::fromVariantList(widget.cues)},
                                 {"groupId", widget.groupId},
                                 {"settings", QJsonObject::fromVariantMap(widget.settings)}});
    }
    for (const auto &[index, object] : m_unknownWidgets) array.insert(qMin(index, array.size()), object);
    return array;
}

bool WidgetModel::fromJson(const QJsonArray &array)
{
    const QJsonObject document{{QStringLiteral("version"), 2},
                               {QStringLiteral("scene"), QJsonObject{{QStringLiteral("widgets"), array}}}};
    if (!ProjectLimits::validateProject(document)) return false;
    QList<WidgetData> widgets;
    QList<QPair<qsizetype, QJsonObject>> unknown;
    QSet<QString> ids;
    int retired = 0;
    for (const QJsonValue &entry : array) {
        const QJsonObject object = entry.toObject();
        const QString type = object.value("type").toString();
        if (retiredWidgetTypes.contains(type)) {
            ++retired;
            continue;
        }
        const QString id = object.value("id").toString();
        if (!validPersistedWidgetId(id) || ids.contains(id)) return false;
        ids.insert(id);
        if (!validType(type)) {
            // A newer version's widget: kept as written (ProjectLimits has
            // bounded it), at its place among the widgets written back.
            unknown.append({widgets.size() + unknown.size(), object});
            continue;
        }
        WidgetData widget = createWidget(type, widgets.size());
        widget.id = id;
        normalizeWidgetGeometry(&widget, object.toVariantMap());
        widget.visible = object.value("visible").toBool(true);
        for (const QJsonValue &cue : object.value("cues").toArray()) {
            if (!cue.isObject()) return false;
            widget.cues.append(normalizeCue(cue.toObject().toVariantMap()));
        }
        widget.groupId = object.value("groupId").toString();
        mergeNormalizedSettings(&widget.settings,
                                object.value("settings").toObject().toVariantMap(),
                                defaultSettings(type));
        widgets.append(std::move(widget));
    }
    beginResetModel();
    m_widgets = std::move(widgets);
    m_unknownWidgets = std::move(unknown);
    endResetModel();
    m_retiredWidgetsDropped = retired;
    ++m_revision;
    emit countChanged();
    emit revisionChanged();
    return true;
}

WidgetData WidgetModel::createWidget(const QString &type, const int index)
{
    const auto [width, height] = defaultSize(type);
    WidgetData widget;
    widget.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    widget.type = type;
    widget.x = 0.04 + (index % 3) * 0.20;
    widget.y = 0.05 + (index / 3) * 0.22;
    widget.width = width;
    widget.height = height;
    widget.settings = defaultSettings(type);
    constrainWidgetToCanvas(&widget);
    return widget;
}

bool WidgetModel::validType(const QString &type) { return widgetTypes.contains(type); }

void WidgetModel::update(const int index)
{
    ++m_revision;
    emit dataChanged(this->index(index), this->index(index));
    emit revisionChanged();
}

QVariantList WidgetModel::libraryWidgets() const
{
    QVariantList result;
    for (const QJsonValue &value : m_library) {
        const QJsonObject entry = value.toObject();
        const QJsonObject settings = entry.value(QStringLiteral("settings")).toObject();
        result.append(QVariantMap{
            {QStringLiteral("id"), entry.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"), entry.value(QStringLiteral("name")).toString()},
            {QStringLiteral("type"), entry.value(QStringLiteral("type")).toString()},
            {QStringLiteral("elementCount"), settings.value(QStringLiteral("elements")).toArray().size()},
        });
    }
    return result;
}

qsizetype WidgetModel::libraryIndex(const QString &libraryId) const
{
    for (qsizetype index = 0; index < m_library.size(); ++index) {
        if (m_library[index].toObject().value(QStringLiteral("id")).toString() == libraryId) return index;
    }
    return -1;
}

QJsonObject WidgetModel::libraryEntry(const QString &id, const QString &name, const WidgetData &widget) const
{
    QVariantMap settings = widget.settings;
    settings.insert(QStringLiteral("libraryId"), id);
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name.trimmed().left(80)},
            {QStringLiteral("type"), widget.type},
            {QStringLiteral("width"), widget.width},
            {QStringLiteral("height"), widget.height},
            {QStringLiteral("settings"), QJsonObject::fromVariantMap(settings)}};
}

QString WidgetModel::saveWidgetToLibrary(const int index, const QString &name)
{
    const WidgetData *widget = widgetAt(index);
    if (!widget || name.trimmed().isEmpty()) return {};
    if (!m_libraryWritable) {
        if (m_libraryError.isEmpty()) setLibraryError(tr("The widget library cannot be written."));
        return {};
    }
    if (m_library.size() >= ProjectLimits::maximumLibraryWidgets) {
        setLibraryError(tr("My widgets holds at most %1 widgets. Delete one before saving another.")
                            .arg(ProjectLimits::maximumLibraryWidgets));
        return {};
    }
    const QString id = QStringLiteral("widget-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_library.append(libraryEntry(id, name, *widget));
    if (!saveLibrary()) {
        m_library.removeLast();
        return {};
    }
    m_widgets[index].settings.insert(QStringLiteral("libraryId"), id);
    update(index);
    emit libraryWidgetsChanged();
    return id;
}

bool WidgetModel::updateLibraryWidget(const QString &libraryId, const int index)
{
    const WidgetData *widget = widgetAt(index);
    const qsizetype position = libraryIndex(libraryId);
    if (!widget || position < 0 || !m_libraryWritable) return false;
    const QJsonValue previous = m_library[position];
    m_library[position] = libraryEntry(
        libraryId, previous.toObject().value(QStringLiteral("name")).toString(), *widget);
    if (!saveLibrary()) {
        m_library[position] = previous;
        return false;
    }
    emit libraryWidgetsChanged();
    return true;
}

int WidgetModel::addLibraryWidget(const QString &libraryId)
{
    const qsizetype position = libraryIndex(libraryId);
    if (position < 0) return -1;
    const QJsonObject entry = m_library[position].toObject();
    const int index = addWidget(entry.value(QStringLiteral("type")).toString());
    if (index < 0) return -1;
    WidgetData &widget = m_widgets[index];
    widget.width = entry.value(QStringLiteral("width")).toDouble(widget.width);
    widget.height = entry.value(QStringLiteral("height")).toDouble(widget.height);
    constrainWidgetToCanvas(&widget);
    mergeNormalizedSettings(&widget.settings,
                            entry.value(QStringLiteral("settings")).toObject().toVariantMap(),
                            defaultSettings(widget.type));
    widget.settings.insert(QStringLiteral("name"), entry.value(QStringLiteral("name")).toString());
    update(index);
    return index;
}

bool WidgetModel::deleteLibraryWidget(const QString &libraryId)
{
    const qsizetype position = libraryIndex(libraryId);
    if (position < 0 || !m_libraryWritable) return false;
    const QJsonValue removed = m_library.takeAt(position);
    if (!saveLibrary()) {
        m_library.insert(position, removed);
        return false;
    }
    emit libraryWidgetsChanged();
    return true;
}

bool WidgetModel::renameLibraryWidget(const QString &libraryId, const QString &name)
{
    const qsizetype position = libraryIndex(libraryId);
    if (position < 0 || name.trimmed().isEmpty() || !m_libraryWritable) return false;
    const QJsonValue previous = m_library[position];
    QJsonObject entry = previous.toObject();
    entry.insert(QStringLiteral("name"), name.trimmed().left(80));
    m_library[position] = entry;
    if (!saveLibrary()) {
        m_library[position] = previous;
        return false;
    }
    emit libraryWidgetsChanged();
    return true;
}

bool WidgetModel::exportLibraryWidget(const QString &libraryId, const QUrl &url)
{
    const qsizetype position = libraryIndex(libraryId);
    if (position < 0 || !url.isLocalFile()) return false;
    QString path = url.toLocalFile();
    if (!path.endsWith(QStringLiteral(".fetwidget"), Qt::CaseInsensitive)) {
        path.append(QStringLiteral(".fetwidget"));
    }
    const QJsonObject package{{QStringLiteral("flappedEarWidgetVersion"), 1},
                              {QStringLiteral("widget"), m_library[position]}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(package).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        setLibraryError(tr("Could not export the widget: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

QString WidgetModel::importLibraryWidget(const QUrl &url)
{
    if (!url.isLocalFile()) return {};
    const auto loaded = BoundedJsonLoader::loadFile(
        url.toLocalFile(), ProjectLimits::libraryWidgetBytes, QStringLiteral("Widget"));
    if (!loaded.success() || !loaded.document.isObject()) {
        setLibraryError(loaded.error.isEmpty() ? tr("The file is not a FlappedEar widget.") : loaded.error);
        return {};
    }
    const QJsonObject root = loaded.document.object();
    QJsonObject entry = root.value(QStringLiteral("widget")).toObject();
    entry.insert(QStringLiteral("id"),
                 QStringLiteral("widget-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (root.value(QStringLiteral("flappedEarWidgetVersion")).toInt() != 1 || !normalizeLibraryEntry(&entry)) {
        setLibraryError(tr("The file is not a supported FlappedEar widget."));
        return {};
    }
    if (!m_libraryWritable) return {};
    if (m_library.size() >= ProjectLimits::maximumLibraryWidgets) {
        setLibraryError(tr("My widgets holds at most %1 widgets. Delete one before importing another.")
                            .arg(ProjectLimits::maximumLibraryWidgets));
        return {};
    }
    QJsonObject settings = entry.value(QStringLiteral("settings")).toObject();
    settings.insert(QStringLiteral("libraryId"), entry.value(QStringLiteral("id")));
    entry.insert(QStringLiteral("settings"), settings);
    m_library.append(entry);
    if (!saveLibrary()) {
        m_library.removeLast();
        return {};
    }
    emit libraryWidgetsChanged();
    return entry.value(QStringLiteral("id")).toString();
}

void WidgetModel::reloadLibrary()
{
    loadLibrary();
    emit libraryWidgetsChanged();
}

void WidgetModel::setElements(const int index, const QVariant &elements)
{
    if (index < 0 || index >= m_widgets.size()) return;
    m_widgets[index].settings.insert(QStringLiteral("elements"), normalizeDesignElements(elements));
    update(index);
}

void WidgetModel::setLibraryError(const QString &error)
{
    if (m_libraryError == error) return;
    m_libraryError = error;
    emit libraryErrorChanged();
}

void WidgetModel::loadLibrary()
{
    m_libraryWritable = false;
    const QString path = widgetLibraryPath();
    if (!QFileInfo::exists(path)) {
        m_library = {};
        m_libraryWritable = true;
        setLibraryError({});
        return;
    }
    const auto loaded = BoundedJsonLoader::loadFile(
        path, ProjectLimits::widgetLibraryBytes, QStringLiteral("Widget library"));
    QString error = loaded.error;
    const QJsonObject root = loaded.document.object();
    if (!loaded.success() || !loaded.document.isObject()
        || !ProjectLimits::validateWidgetLibrary(root, &error)) {
        m_library = {};
        setLibraryError(tr("My widgets could not be loaded from %1: %2. "
                           "The file is preserved; restore it and reload before saving.")
                            .arg(QDir::toNativeSeparators(path), error));
        return;
    }
    QJsonArray library;
    for (const QJsonValue &value : root.value(QStringLiteral("widgets")).toArray()) {
        QJsonObject entry = value.toObject();
        // A retired type cannot be placed any more; it leaves the library.
        if (retiredWidgetTypes.contains(entry.value(QStringLiteral("type")).toString())) continue;
        if (!normalizeLibraryEntry(&entry)) {
            m_library = {};
            setLibraryError(tr("My widgets contains an unsupported widget. "
                               "The file is preserved; restore it and reload before saving."));
            return;
        }
        library.append(entry);
    }
    m_library = library;
    m_libraryWritable = true;
    setLibraryError({});
}

bool WidgetModel::saveLibrary()
{
    if (!m_libraryWritable) return false;
    const QJsonObject root{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("widgets"), m_library}};
    QString error;
    if (!ProjectLimits::validateWidgetLibrary(root, &error)) {
        setLibraryError(error);
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (payload.size() > ProjectLimits::widgetLibraryBytes) {
        setLibraryError(tr("My widgets exceeds the %1 MiB storage limit. Delete an unused widget first.")
                            .arg(ProjectLimits::widgetLibraryBytes / (1024 * 1024)));
        return false;
    }
    const QString path = widgetLibraryPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        setLibraryError(tr("Could not create the widget library directory."));
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) {
        setLibraryError(tr("Could not save My widgets: %1").arg(file.errorString()));
        return false;
    }
    setLibraryError({});
    return true;
}

} // namespace FlappedEar
