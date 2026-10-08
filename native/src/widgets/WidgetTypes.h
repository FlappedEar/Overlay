#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace FlappedEar {

// KAN-217: one descriptor per widget type. The editor's Add widget list, the
// type check on load, the default box, the inspector's hidden controls and the
// renderer the scene loads all read this table, so a type is added or changed
// in one place. Default settings
// stay in widget-templates.json ("widgetDefaults"); Telemetry no longer shares it
// (the apps are independent since 8 October 2026).
struct WidgetTypeDescriptor {
    QString type;
    QString label; // empty: not offered in the Add widget list
    QString icon;
    double width = 0.15;
    double height = 0.16;
    // KAN-139: shared controls the renderer of each style does not read, so the
    // inspector never offers a control that changes nothing. Keep in step with
    // the renderers in qml/widgets/.
    QStringList classicUnusedControls;
    QStringList techUnusedControls;
    // QML renderer paths relative to TelemetryScene.qml. A type without a Tech
    // renderer draws its Classic one in the Tech style.
    QString classicRenderer;
    QString techRenderer;
};

const QList<WidgetTypeDescriptor> &widgetTypeDescriptors();
const WidgetTypeDescriptor *widgetTypeDescriptor(const QString &type);
// The renderer for a type in a style ("tech" or anything else for Classic);
// empty for an unknown type.
QString widgetRendererSource(const QString &type, const QString &style);
// Shared controls no Tech renderer reads.
const QStringList &techSharedUnusedControls();

} // namespace FlappedEar
