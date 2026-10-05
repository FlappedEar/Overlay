#include "app/CommandLine.h"

#include <array>

namespace FlappedEar::CommandLine {

namespace {

struct ModeSpec {
    const char *flag;
    Mode mode;
    const char *parameters;
    qsizetype parameterCount;
};

constexpr std::array modes{
    ModeSpec{"--render-still", Mode::RenderStill, "<output.png>", 1},
    ModeSpec{"--render-visual-smoke", Mode::RenderVisualSmoke, "<output-directory>", 1},
    ModeSpec{"--render-visual-smoke-dark", Mode::RenderVisualSmokeDark, "<output-directory>", 1},
    ModeSpec{"--export-worker", Mode::ExportWorker, "<config.json>", 1},
    ModeSpec{"--benchmark-render", Mode::BenchmarkRender, "<width> <height> <frames>", 3},
    ModeSpec{"--startup-smoke", Mode::StartupSmoke, "", 0},
};

Parsed usageError(QString error)
{
    return {Mode::UsageError, {}, std::move(error)};
}

} // namespace

Parsed parse(const QStringList &arguments)
{
    if (!arguments.isEmpty()) {
        const QString &command = arguments.first();
        for (const ModeSpec &spec : modes) {
            if (command != QLatin1String(spec.flag)) continue;
            const QStringList parameters = arguments.mid(1);
            if (parameters.size() != spec.parameterCount) {
                return usageError(QStringLiteral("%1 takes %2 argument(s), %3 given.")
                                      .arg(command)
                                      .arg(spec.parameterCount)
                                      .arg(parameters.size()));
            }
            return {spec.mode, parameters, {}};
        }
    }
    for (const QString &argument : arguments) {
        if (argument.startsWith(QStringLiteral("--")))
            return usageError(QStringLiteral("Unknown option %1.").arg(argument));
    }
    return {};
}

QString usage(const QString &error)
{
    QString text;
    if (!error.isEmpty()) text += error + QLatin1Char('\n');
    text += QStringLiteral("Usage: run without options to open the editor, or use one of:\n");
    for (const ModeSpec &spec : modes) {
        text += QStringLiteral("  %1").arg(QLatin1String(spec.flag));
        if (spec.parameterCount > 0) text += QLatin1Char(' ') + QLatin1String(spec.parameters);
        text += QLatin1Char('\n');
    }
    return text;
}

} // namespace FlappedEar::CommandLine
