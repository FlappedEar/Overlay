#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace FlappedEar {

// KAN-217: one descriptor per widget type. The editor's Add widget list, the
// type check on load, the default box and the inspector's hidden controls all
// read this table, so a type is added or changed in one place. Default settings
// stay in widget-templates.json ("widgetDefaults"), which Telemetry shares.
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
};

const QList<WidgetTypeDescriptor> &widgetTypeDescriptors();
const WidgetTypeDescriptor *widgetTypeDescriptor(const QString &type);
// Shared controls no Tech renderer reads.
const QStringList &techSharedUnusedControls();

} // namespace FlappedEar
