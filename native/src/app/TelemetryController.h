#pragma once

#include "app/AnalysisController.h"
#include "app/DocumentController.h"
#include "app/DocumentHost.h"

#include <QObject>
#include <QString>

namespace FlappedEar {

// KAN-124: Flapped Ear Telemetry's controller -- a project document and its
// day analysis with no overlay editor, no video and no export. It hosts the
// document with nothing of its own stored in the project, so a project
// saved here keeps the editor state another app wrote, untouched. Built in
// flappedear_telemetry_app with Qt Core only (no Gui), for macOS, iOS and
// Android.
class TelemetryController final : public QObject, private DocumentHost {
    Q_OBJECT
    Q_PROPERTY(DocumentController *document READ document CONSTANT)
    Q_PROPERTY(AnalysisController *analysis READ analysis CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit TelemetryController(QString recoveryPath = {},
        ProjectRecoveryStore::Operations recoveryOperations = {}, QObject *parent = nullptr);
    ~TelemetryController() override;

    [[nodiscard]] DocumentController *document() { return &m_document; }
    [[nodiscard]] AnalysisController *analysis() { return &m_analysis; }
    [[nodiscard]] QString statusText() const { return m_statusText; }

signals:
    void statusTextChanged();

private:
    // DocumentHost: no editor state; the stored project passes through.
    quint64 beginSourceGeneration(bool preserveAnalysis) override;
    [[nodiscard]] bool acceptsEditorProject(const QJsonObject &, const QString &) override { return true; }
    [[nodiscard]] bool applyEditorScene(const ProjectLoadResult &, QString *) override { return true; }
    void applyEditorProject(const ProjectLoadResult &) override {}
    void announceEditorProject() override {}
    void startEditorSources(const ProjectLoadResult &) override {}
    void clearEditor() override {}
    // The active run's source references, rebased for where the project is
    // saved (as the editor does); everything else passes through.
    [[nodiscard]] QJsonObject withEditorState(QJsonObject projection, const QString &documentPath,
        const QString &targetPath) const override;
    [[nodiscard]] QByteArray loadedTelemetryRevision() const override { return {}; }
    [[nodiscard]] QJsonObject withVerifiedAnalysis(const QJsonObject &project) const override;
    void editorProjectSaved(const QJsonObject &) override {}
    [[nodiscard]] bool documentBusy() const override { return false; }
    void showStatus(const QString &status) override;
    void revealAnalysis() override {}

    QString m_statusText;
    // Analysis after the document it reads: destroyed first.
    DocumentController m_document;
    AnalysisController m_analysis{m_document};
};

} // namespace FlappedEar
