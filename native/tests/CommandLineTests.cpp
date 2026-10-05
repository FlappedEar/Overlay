#include "app/CommandLine.h"

#include <QtTest>

using namespace FlappedEar;
using CommandLine::Mode;

// KAN-178: the application must recognise each mode exactly, and report any
// other "--" option instead of silently opening the editor.
class CommandLineTests : public QObject {
    Q_OBJECT

private slots:
    void opensTheEditorWithoutOptions()
    {
        QCOMPARE(CommandLine::parse({}).mode, Mode::Editor);
        // Qt's own options use a single dash and stay with Qt.
        QCOMPARE(CommandLine::parse({"-platform", "offscreen"}).mode, Mode::Editor);
    }

    void recognisesEveryModeWithItsArguments_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<Mode>("mode");
        QTest::addColumn<QStringList>("parameters");
        QTest::newRow("render-still") << QStringList{"--render-still", "a.png"} << Mode::RenderStill
                                      << QStringList{"a.png"};
        QTest::newRow("visual-smoke") << QStringList{"--render-visual-smoke", "out"}
                                      << Mode::RenderVisualSmoke << QStringList{"out"};
        QTest::newRow("visual-smoke-dark") << QStringList{"--render-visual-smoke-dark", "out"}
                                           << Mode::RenderVisualSmokeDark << QStringList{"out"};
        QTest::newRow("export-worker") << QStringList{"--export-worker", "c.json"} << Mode::ExportWorker
                                       << QStringList{"c.json"};
        QTest::newRow("benchmark") << QStringList{"--benchmark-render", "1920", "1080", "300"}
                                   << Mode::BenchmarkRender << QStringList{"1920", "1080", "300"};
        QTest::newRow("startup-smoke") << QStringList{"--startup-smoke"} << Mode::StartupSmoke
                                       << QStringList{};
    }

    void recognisesEveryModeWithItsArguments()
    {
        QFETCH(QStringList, arguments);
        QFETCH(Mode, mode);
        QFETCH(QStringList, parameters);
        const CommandLine::Parsed parsed = CommandLine::parse(arguments);
        QCOMPARE(parsed.mode, mode);
        QCOMPARE(parsed.arguments, parameters);
        QVERIFY(parsed.error.isEmpty());
    }

    void reportsUnknownOptionsAndWrongArgumentCounts_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<QString>("error");
        QTest::newRow("removed flag") << QStringList{"--export-test", "x", "y"}
                                      << QStringLiteral("Unknown option --export-test.");
        QTest::newRow("extra argument") << QStringList{"--startup-smoke", "extra"}
                                        << QStringLiteral("--startup-smoke takes 0 argument(s), 1 given.");
        QTest::newRow("missing argument") << QStringList{"--export-worker"}
                                          << QStringLiteral("--export-worker takes 1 argument(s), 0 given.");
        QTest::newRow("benchmark short") << QStringList{"--benchmark-render", "1920"}
                                         << QStringLiteral("--benchmark-render takes 3 argument(s), 1 given.");
        QTest::newRow("after a plain argument") << QStringList{"file", "--help"}
                                                << QStringLiteral("Unknown option --help.");
        QTest::newRow("bare double dash") << QStringList{"--"} << QStringLiteral("Unknown option --.");
    }

    void reportsUnknownOptionsAndWrongArgumentCounts()
    {
        QFETCH(QStringList, arguments);
        QFETCH(QString, error);
        const CommandLine::Parsed parsed = CommandLine::parse(arguments);
        QCOMPARE(parsed.mode, Mode::UsageError);
        QCOMPARE(parsed.error, error);
        const QString usage = CommandLine::usage(parsed.error);
        QVERIFY(usage.startsWith(error + QLatin1Char('\n')));
        for (const char *flag : {"--render-still", "--render-visual-smoke", "--render-visual-smoke-dark",
                                 "--export-worker", "--benchmark-render", "--startup-smoke"})
            QVERIFY2(usage.contains(QLatin1String(flag)), flag);
    }
};

QTEST_GUILESS_MAIN(CommandLineTests)
#include "CommandLineTests.moc"
