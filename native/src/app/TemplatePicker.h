#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

class SourceTests;

namespace FlappedEar {

class WidgetModel;

// KAN-215: the editor's template picker. It remembers the selected template
// across launches and tracks which template the current scene came from, so
// "Update template" writes back only to a user template the scene was
// applied from. A project stores a scene, not where it came from: opening one
// clears the active template but keeps the selection. QML reaches it as
// appController.templatePicker.
class TemplatePicker final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    Q_PROPERTY(QString activeId READ activeId NOTIFY changed)

public:
    TemplatePicker(WidgetModel &widgets, QSettings &settings, QObject *parent = nullptr);

    [[nodiscard]] QString selectedId() const { return m_selectedId; }
    [[nodiscard]] QString activeId() const { return m_activeId; }

    Q_INVOKABLE int indexForId(const QString &templateId) const;
    Q_INVOKABLE void select(const QString &templateId);
    Q_INVOKABLE void reconcileSelection();
    // Replaces the scene with the template, then selects it and marks it active.
    Q_INVOKABLE bool apply(const QString &templateId);
    Q_INVOKABLE void markActive(const QString &templateId);
    // Writes the scene back to the active template; false for a built-in one.
    Q_INVOKABLE bool saveActive();
    void clearActive() { markActive({}); }

signals:
    void changed();

private:
    friend class ::SourceTests;
    void storeSelection();

    WidgetModel &m_widgets;
    QSettings &m_settings;
    QString m_selectedId;
    QString m_activeId;
};

} // namespace FlappedEar
