#include "app/TemplatePicker.h"

#include "widgets/WidgetModel.h"

namespace FlappedEar {

TemplatePicker::TemplatePicker(WidgetModel &widgets, QSettings &settings, QObject *parent)
    : QObject(parent)
    , m_widgets(widgets)
    , m_settings(settings)
    , m_selectedId(settings.value(QStringLiteral("ui/selectedTemplateId")).toString())
{
    connect(&m_widgets, &WidgetModel::templatesChanged, this, [this] {
        reconcileSelection();
        if (indexForId(m_activeId) < 0) {
            clearActive();
        }
    });
    reconcileSelection();
}

int TemplatePicker::indexForId(const QString &templateId) const
{
    const QVariantList templates = m_widgets.templates();
    for (qsizetype index = 0; index < templates.size(); ++index) {
        if (templates[index].toMap().value(QStringLiteral("id")).toString() == templateId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void TemplatePicker::select(const QString &templateId)
{
    if (indexForId(templateId) < 0 || m_selectedId == templateId) {
        return;
    }
    m_selectedId = templateId;
    storeSelection();
    emit changed();
}

void TemplatePicker::reconcileSelection()
{
    if (indexForId(m_selectedId) >= 0) {
        return;
    }
    const QVariantList templates = m_widgets.templates();
    const QString fallback = templates.isEmpty()
        ? QString() : templates.constFirst().toMap().value(QStringLiteral("id")).toString();
    if (m_selectedId == fallback) {
        return;
    }
    m_selectedId = fallback;
    storeSelection();
    emit changed();
}

bool TemplatePicker::apply(const QString &templateId)
{
    if (indexForId(templateId) < 0 || !m_widgets.applyTemplate(templateId)) {
        return false;
    }
    select(templateId);
    markActive(templateId);
    return true;
}

void TemplatePicker::markActive(const QString &templateId)
{
    const QString activeId = indexForId(templateId) >= 0 ? templateId : QString();
    if (m_activeId == activeId) {
        return;
    }
    m_activeId = activeId;
    emit changed();
}

bool TemplatePicker::saveActive()
{
    const int index = indexForId(m_activeId);
    if (index < 0 || m_widgets.templates()[index].toMap().value(QStringLiteral("builtIn")).toBool()) {
        return false;
    }
    return m_widgets.updateTemplate(m_activeId);
}

void TemplatePicker::storeSelection()
{
    m_settings.setValue(QStringLiteral("ui/selectedTemplateId"), m_selectedId);
    m_settings.sync();
}

} // namespace FlappedEar
