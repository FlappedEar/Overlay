#pragma once

#include <functional>

#include <QString>

namespace FlappedEar {

class ExportProcessSupervisor;

struct ExportCancellationResult final {
    bool markerCreated = false;
    bool workerStopped = false;
    QString error;
};

// The marker is the cooperative path. If it cannot be made durable, the
// supervised worker is synchronously stopped instead; callers may set a
// terminal export state only after workerStopped is true.
class ExportCancellation final {
public:
    using MarkerWriter = std::function<bool(const QString &path, QString *error)>;

    [[nodiscard]] static ExportCancellationResult request(
        const QString &markerPath,
        ExportProcessSupervisor *supervisor,
        MarkerWriter markerWriter = {});
};

// KAN-156: an export worker stops when the application that started it has
// gone (a crash), instead of encoding on into a hidden staging file. An
// orphaned process is reparented, so its parent process ID changes.
class ParentProcessWatch final {
public:
    ParentProcessWatch();
    [[nodiscard]] bool parentExited() const;

private:
    qint64 m_parent = 0;
};

} // namespace FlappedEar
