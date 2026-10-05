#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace FlappedEar::ProjectLimits {

// These are deliberately sized for complex broadcast layouts while preventing an
// imported document from turning into an unbounded QML/model allocation.
inline constexpr qint64 projectBytes = 4 * 1024 * 1024;
inline constexpr qint64 templateBytes = 2 * 1024 * 1024;
inline constexpr qint64 templateStoreBytes = 8 * 1024 * 1024;
inline constexpr qint64 recoveryBytes = 4 * 1024 * 1024;
inline constexpr qint64 manifestBytes = 64 * 1024;
inline constexpr qsizetype maximumWidgets = 256;
inline constexpr qsizetype maximumCuesPerWidget = 256;
inline constexpr qsizetype maximumTotalCues = 4096;
inline constexpr qsizetype maximumSettingsEntries = 128;
inline constexpr qsizetype maximumTemplateCount = 128;
inline constexpr qsizetype maximumJsonDepth = 32;
inline constexpr qsizetype maximumStringCharacters = 4096;
inline constexpr qsizetype maximumIdCharacters = 128;
// Comparison-view persisted range/channel selection (KAN-41): a shared-progress
// window in meters and up to 4 channel names. The distance bound is far beyond
// any real track length -- it exists only to keep an imported/edited document
// from carrying an unbounded or non-finite value, not to constrain real tracks.
inline constexpr double maximumComparisonRangeMeters = 1'000'000.0;
inline constexpr qsizetype maximumComparisonChannels = 4;
// KAN-103: per-channel rules of one run's approved source fusion.
inline constexpr qsizetype maximumFusionRules = 64;
// An approved fusion clock: offsets up to a day, drift up to a logger's plausible 1000 ppm.
inline constexpr double maximumFusionOffsetSeconds = 86400.0;
inline constexpr double maximumFusionDriftPpm = 1000.0;
inline constexpr qsizetype maximumTemplateNameCharacters = 160;
inline constexpr qsizetype maximumTemplateDescriptionCharacters = 2048;
// KAN-191: a designed widget's elements, and the "My widgets" library.
inline constexpr qsizetype maximumDesignElements = 64;
inline constexpr qsizetype maximumLibraryWidgets = 128;
inline constexpr qint64 libraryWidgetBytes = 1024 * 1024;
inline constexpr qint64 widgetLibraryBytes = 8 * 1024 * 1024;

bool validateProject(const QJsonObject &project, QString *error = nullptr);
bool validateTemplate(const QJsonObject &templateObject, QString *error = nullptr);
bool validateTemplateStore(const QJsonObject &store, QString *error = nullptr);
bool validateLibraryWidget(const QJsonObject &entry, QString *error = nullptr);
bool validateWidgetLibrary(const QJsonObject &library, QString *error = nullptr);

} // namespace FlappedEar::ProjectLimits
