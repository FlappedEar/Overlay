#include "widgets/WidgetTypes.h"

namespace FlappedEar {

const QList<WidgetTypeDescriptor> &widgetTypeDescriptors()
{
    static const QList<WidgetTypeDescriptor> descriptors = {
        {"speed", "Speed", "KM", 0.15, 0.16, {"accentColor"}, {},
         "widgets/SpeedWidget.qml", "widgets/tech/TechSpeed.qml"},
        {"heartRate", "Heart rate", "♥", 0.12, 0.13, {}, {"label", "showUnit"},
         "widgets/HeartRateWidget.qml", "widgets/tech/TechHeartRate.qml"},
        {"pedals", "Pedals", "▥", 0.25, 0.13, {"accentColor", "fontWeight", "valueFontScale"}, {"barRadius"},
         "widgets/PedalsWidget.qml", "widgets/tech/TechPedals.qml"},
        {"f1GForceRadar", "F1 G-Force Radar", "G+", 0.15, 0.20,
         {"accentColor", "showBackground", "backgroundColor", "showBorder", "borderColor", "borderWidth",
          "borderOpacity", "cornerRadius", "padding", "fontFamily", "fontWeight", "valueFontScale",
          "labelFontScale", "textColor", "secondaryTextColor"},
         {"showBackground", "gridColor", "showCenterBox"},
         "widgets/F1GForceRadarWidget.qml", "widgets/tech/TechRadar.qml"},
        {"gForceMagnitudeBar", "G-Force Bar", "G=", 0.24, 0.10,
         {"accentColor", "fontWeight", "secondaryTextColor", "invertLateral", "invertLongitudinal"},
         {"fontSize", "maxG", "barRadius", "showLabel", "showValue", "barColor", "barBackgroundColor",
          "invertLateral", "invertLongitudinal"},
         "widgets/GForceMagnitudeBarWidget.qml", "widgets/tech/TechGForce.qml"},
        {"tyres", "Tyres", "◫", 0.17, 0.20, {}, {"fontSize"},
         "widgets/TyresWidget.qml", "widgets/tech/TechTyres.qml"},
        {"retroCustomValue", "Retro Custom", "R+", 0.20, 0.13,
         {"accentColor", "backgroundColor", "fontWeight", "textColor", "secondaryTextColor"},
         {"panelColor", "valueColor", "labelColor"},
         "widgets/RetroCustomValueWidget.qml", "widgets/tech/TechCustomValue.qml"},
        {"lapCurrent", "Current lap time", "NOW", 0.17, 0.14, {"fontWeight", "padding", "secondaryTextColor"}, {},
         "widgets/LapTimeWidget.qml", "widgets/tech/TechLapTime.qml"},
        {"retroTachometer", "Retro RPM", "R", 0.25, 0.36,
         {"accentColor", "showBackground", "backgroundColor", "backgroundOpacity", "showBorder", "borderColor",
          "borderWidth", "borderOpacity", "cornerRadius", "padding", "fontWeight", "valueFontScale",
          "labelFontScale", "textColor", "unit", "decimals", "prefix", "suffix", "showUnit"},
         {"fontSize", "showBackground", "backgroundOpacity", "unit", "decimals", "prefix", "suffix", "showUnit",
          "dialColor", "needleColor", "panelColor", "panelOpacity"},
         "widgets/RetroTachometerWidget.qml", "widgets/tech/TechTachometer.qml"},
        // KAN-191: made in the widget editor, not from the Add widget list.
        {"designed", {}, {}, 0.20, 0.13, {}, {},
         "widgets/DesignedWidget.qml", {}},
    };
    return descriptors;
}

const WidgetTypeDescriptor *widgetTypeDescriptor(const QString &type)
{
    for (const WidgetTypeDescriptor &descriptor : widgetTypeDescriptors()) {
        if (descriptor.type == type) return &descriptor;
    }
    return nullptr;
}

QString widgetRendererSource(const QString &type, const QString &style)
{
    const WidgetTypeDescriptor *descriptor = widgetTypeDescriptor(type);
    if (!descriptor) return {};
    if (style == QStringLiteral("tech") && !descriptor->techRenderer.isEmpty()) return descriptor->techRenderer;
    return descriptor->classicRenderer;
}

const QStringList &techSharedUnusedControls()
{
    static const QStringList controls = {
        "accentColor", "backgroundColor", "showBorder", "borderColor", "borderWidth", "borderOpacity",
        "cornerRadius", "padding", "fontFamily", "fontWeight", "valueFontScale", "labelFontScale",
        "textColor", "secondaryTextColor"};
    return controls;
}

} // namespace FlappedEar
