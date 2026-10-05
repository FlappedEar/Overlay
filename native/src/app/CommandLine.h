#pragma once

#include <QString>
#include <QStringList>

namespace FlappedEar::CommandLine {

enum class Mode {
    Editor,
    RenderStill,
    RenderVisualSmoke,
    RenderVisualSmokeDark,
    ExportWorker,
    BenchmarkRender,
    StartupSmoke,
    UsageError,
};

struct Parsed {
    Mode mode = Mode::Editor;
    // The mode's own arguments, without the program name or the mode flag.
    QStringList arguments;
    QString error;
};

// KAN-178: `arguments` excludes the program name. A known mode needs its exact
// argument count. Any other argument that starts with "--" is a usage error, so
// a mistyped or removed flag never opens the editor or takes the session lock.
// Arguments without "--" (Qt's own single-dash options) still open the editor.
[[nodiscard]] Parsed parse(const QStringList &arguments);
[[nodiscard]] QString usage(const QString &error);

} // namespace FlappedEar::CommandLine
