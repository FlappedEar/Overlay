#pragma once

#include <QGuiApplication>

namespace FlappedEar::ApplicationIdentity {

inline constexpr auto displayName = "FlappedEar Overlays";
inline constexpr auto storageName = "FlappedEar Overlays";
inline constexpr auto organization = "FlappedEar";
inline constexpr auto domain = "flappedear.com";
// KAN-125: the storage identity before the rename to FlappedEar Overlays.
// LegacyStorageMigration brings its preferences and data across once at startup;
// nothing else may read or write it.
inline constexpr auto legacyStorageName = "FlappedEar Telemetry";

inline void initialize()
{
    // These identifiers own the QSettings and QStandardPaths data. A display or
    // bundle rename alone must never create a second preferences/recovery tree;
    // a storage rename needs a migration from the previous name (KAN-125).
    QCoreApplication::setOrganizationName(organization);
    QCoreApplication::setOrganizationDomain(domain);
    QCoreApplication::setApplicationName(storageName);
    QGuiApplication::setApplicationDisplayName(displayName);
}

} // namespace FlappedEar::ApplicationIdentity
