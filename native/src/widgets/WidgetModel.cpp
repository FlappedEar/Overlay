#include "widgets/WidgetModel.h"
#include "project/BoundedJsonLoader.h"
#include "project/ProjectLimits.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
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

QVariantMap defaultSettings(const QString &type)
{
    const QJsonObject defaults = templateCatalog().value("widgetDefaults").toObject();
    QVariantMap result = defaults.value("common").toObject().toVariantMap();
    const QVariantMap typeDefaults = defaults.value(type).toObject().toVariantMap();
    for (auto iterator = typeDefaults.cbegin(); iterator != typeDefaults.cend(); ++iterator) {
        result.insert(iterator.key(), iterator.value());
    }
    result.insert("name", type);
    return result;
}

const QStringList widgetTypes = {
    "speed",          "rpm",       "heartRate",       "pedals", "gForce",
    "f1GForceRadar",  "gForceMagnitudeBar", "track", "customValue", "retroCustomValue", "arcGauge", "dialGauge",
    "telemetryOverlay", "lapBest", "lapCurrent", "lapDelta", "speedBest", "speedCurrent",
    "speedDelta", "retroGrandPrix", "retroTachometer", "retroGear",
    "retroPedal", "retroSpeedArc", "retroNameplate", "brandLogo", "tyres"};

QPair<double, double> defaultSize(const QString &type)
{
    if (type == "rpm") {
        return {0.20, 0.09};
    }
    if (type == "heartRate") {
        return {0.12, 0.13};
    }
    if (type == "pedals") {
        return {0.25, 0.13};
    }
    if (type == "gForce") {
        return {0.14, 0.19};
    }
    if (type == "f1GForceRadar") {
        return {0.15, 0.20};
    }
    if (type == "gForceMagnitudeBar") {
        return {0.24, 0.10};
    }
    if (type == "track") {
        return {0.20, 0.28};
    }
    if (type == "customValue" || type == "retroCustomValue") {
        return {0.20, 0.13};
    }
    if (type == "arcGauge" || type == "dialGauge") {
        return {0.20, 0.24};
    }
    if (type == "telemetryOverlay") {
        return {0.42, 0.12};
    }
    if (type == "lapBest" || type == "lapCurrent" || type == "lapDelta"
        || type == "speedBest" || type == "speedCurrent" || type == "speedDelta") {
        return {0.17, 0.14};
    }
    if (type == "retroGrandPrix") {
        return {0.42, 0.61};
    }
    if (type == "retroTachometer") {
        return {0.25, 0.36};
    }
    if (type == "retroGear" || type == "retroPedal") {
        return {0.14, 0.055};
    }
    if (type == "retroSpeedArc") {
        return {0.28, 0.27};
    }
    if (type == "retroNameplate") {
        return {0.24, 0.10};
    }
    if (type == "brandLogo") {
        return {0.12, 0.16};
    }
    if (type == "tyres") {
        return {0.17, 0.20};
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

bool validTemplateObject(const QJsonObject &item)
{
    if (!ProjectLimits::validateTemplate(item)) {
        return false;
    }
    for (const QJsonValue &value : item.value("widgets").toArray()) {
        if (!value.isObject() || !widgetTypes.contains(value.toObject().value("type").toString())) {
            return false;
        }
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

QVariant normalizeSettingValue(
    const QVariantMap &defaults, const QString &name, const QVariant &value, bool *accepted)
{
    if (accepted) *accepted = true;
    const auto fallback = [&defaults, &name] { return defaults.value(name); };
    double number = 0.0;
    if (name == QStringLiteral("fontSize")) {
        return finiteNumber(value, &number) ? bounded(number, 0.0, 200.0) : fallback();
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
    QJsonArray widgets = templateObject->value(QStringLiteral("widgets")).toArray();
    for (QJsonValueRef value : widgets) {
        QJsonObject widgetObject = value.toObject();
        const QString type = widgetObject.value(QStringLiteral("type")).toString();
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
        value = widgetObject;
    }
    templateObject->insert(QStringLiteral("widgets"), widgets);
    return true;
}

} // namespace

WidgetModel::WidgetModel(QObject *parent)
    : QAbstractListModel(parent)
{
    loadUserTemplates();
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
    if (m_widgets.size() >= ProjectLimits::maximumWidgets) {
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
    if (m_widgets.size() >= ProjectLimits::maximumWidgets
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
    if (applyTemplate(QStringLiteral("track-day"))) {
        return;
    }
    beginResetModel();
    m_widgets = {createWidget("speed", 0), createWidget("rpm", 1),
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

bool WidgetModel::exportTemplate(const QString &templateId, const QUrl &url) const
{
    QJsonObject selected;
    for (const QJsonValue &value : allTemplates()) {
        if (value.toObject().value("id").toString() == templateId) {
            selected = value.toObject();
            break;
        }
    }
    if (selected.isEmpty() || !url.isLocalFile()) {
        return false;
    }
    QString path = url.toLocalFile();
    if (!path.endsWith(".fettemplate", Qt::CaseInsensitive)) {
        path.append(".fettemplate");
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QJsonObject package{{"flappedEarTemplateVersion", 1}, {"template", selected}};
    return file.write(QJsonDocument(package).toJson(QJsonDocument::Indented)) >= 0
        && file.commit();
}

QString WidgetModel::importTemplate(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return {};
    }
    const auto loaded = BoundedJsonLoader::loadFile(
        url.toLocalFile(), ProjectLimits::templateBytes, QStringLiteral("Template"));
    if (!loaded.success() || !loaded.document.isObject()) {
        return {};
    }
    const QJsonObject root = loaded.document.object();
    QJsonObject item = root.value("template").toObject();
    if (item.isEmpty()) {
        item = root;
    }
    item.insert("id", QStringLiteral("user-%1").arg(
                          QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!validTemplateObject(item)) {
        return {};
    }
    if (!normalizeTemplateObject(&item)) return {};
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
    qsizetype count = 0;
    for (const WidgetData &widget : m_widgets) count += widget.cues.size();
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
    return array;
}

bool WidgetModel::fromJson(const QJsonArray &array)
{
    const QJsonObject document{{QStringLiteral("version"), 2},
                               {QStringLiteral("scene"), QJsonObject{{QStringLiteral("widgets"), array}}}};
    if (!ProjectLimits::validateProject(document)) return false;
    QList<WidgetData> widgets;
    QSet<QString> ids;
    for (const QJsonValue &entry : array) {
        const QJsonObject object = entry.toObject();
        const QString type = object.value("type").toString();
        if (!validType(type)) {
            return false;
        }
        const QString id = object.value("id").toString();
        if (!validPersistedWidgetId(id) || ids.contains(id)) return false;
        ids.insert(id);
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
    endResetModel();
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

} // namespace FlappedEar
