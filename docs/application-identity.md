# Application identity and upgrades

The desktop app is **FlappedEar Overlays** (KAN-125, owner decision of 2 October 2026).
Until then it was called **FlappedEar Telemetry**. That name, and the bundle identifier
`com.flappedear.telemetry`, now belong to the separate analysis app, a new Flutter app
in its own repository (KAN-165, KAN-167). The brand is written **FlappedEar**, without
a space (owner direction, KAN-171); candidate builds of 2 October 2026 were still named
`Flapped Ear Overlays.app`.

| Purpose | Value |
| --- | --- |
| User-facing name and executable | `FlappedEar Overlays` |
| macOS bundle | `FlappedEar Overlays.app` |
| macOS bundle identifier | `com.flappedear.overlays` |
| Qt storage application name | `FlappedEar Overlays` (previously `FlappedEar Telemetry`) |
| Qt organization / domain | `FlappedEar` / `flappedear.com` (unchanged) |
| Project / template extensions | `.fetproject` / `.fettemplate` (unchanged) |
| Candidate archive prefix | `FlappedEar-Overlays-` |
| Windows installer | Not renamed yet; see [Windows status](#project-opening-and-windows-status) |

`ApplicationIdentity::initialize()` sets the visible display name and the storage name
used by QSettings and QStandardPaths. The internal CMake target, C++ namespace, QML URI
and icon resource are unchanged.

## Moving existing data

Because the storage name changed, the first start of FlappedEar Overlays runs
`LegacyStorageMigration` once. It runs after the app holds its own session lock and
before the log, the recovery store, the templates or the preferences are used.

- **Preferences** are copied from the `FlappedEar Telemetry` store into the new store,
  and only if the new store is still empty. The old preferences are not removed.
- **Files and folders** in the old application-data and configuration directories are
  moved into the new ones: `project-recovery.json` and its discard tombstone,
  `flappedear.log`, `flappedear.previous.log`, `exports/` and `layout-templates.json`.
  Each item is renamed whole. An item whose name already exists in the new directory is
  never overwritten; it stays in the old directory.
- **The previous app must not be running.** The migration holds the old app's session
  lock while it works. If the old app still runs, nothing is moved, the new app says so,
  and the migration is tried again at the next start.
- **Anything left behind is reported** once, in a startup dialog and in the application
  log, with its path. Nothing is deleted.
- The migration records `storage/legacyMigration` in the new preferences and does not
  run again.

On macOS the data directories move as follows:

| Item | Before | After |
| --- | --- | --- |
| Logs, export logs, recovery snapshot | `~/Library/Application Support/FlappedEar/FlappedEar Telemetry/` | `~/Library/Application Support/FlappedEar/FlappedEar Overlays/` |
| Custom templates | `~/Library/Preferences/FlappedEar/FlappedEar Telemetry/` | `~/Library/Preferences/FlappedEar/FlappedEar Overlays/` |
| Preferences | The app's macOS preferences under `FlappedEar Telemetry` | The same under `FlappedEar Overlays` |

`flappedear_storage_migration_tests` covers: a complete one-time migration, never
overwriting, keeping preferences that already exist, waiting while the previous version
runs, missing legacy storage, and the platform's real settings and QStandardPaths
locations.

The separate FlappedEar Telemetry app must use its own storage locations. It must not
read, write or delete the `FlappedEar Telemetry` locations above, which remain only as
the read-only source of this migration
([architect handover](telemetry-handover.md#identity-and-storage)).

## Replacing a macOS candidate

1. Quit the running app, whether it is FlappedEar Telemetry or FlappedEar Overlays.
   Keep your projects and source media in their existing locations.
2. Extract the new candidate and place `FlappedEar Overlays.app` in the directory where
   you keep the application. Remove the old `Flapped Ear Overlays.app`,
   `Flapped Ear Telemetry.app` or `FlappedEar Telemetry.app` bundle after replacing it;
   remove only the application bundle, not application data. The storage name did not
   change with the 2 October spelling change, so nothing moves again.
3. Launch the new bundle. Preferences, templates, the last project and recovery data come
   across on the first start; unsaved recovery still requires the same explicit Recover
   or Discard choice.
4. Update any Dock item or script that names the old bundle or executable path.

The application version remains 0.2.0. These are internal candidates; signing,
notarization and clean-machine acceptance remain separate gates. There is no
automatic updater. See [candidate acceptance](beta-acceptance.md).

## Project opening and Windows status

The baseline registers no Finder document association and has no Finder file-open
handler. The Windows installer also deliberately registers no file association.
The rename preserves that behavior: use **File > Open Project** for `.fetproject`
files. It does not introduce a double-click association or change project schemas.

The Windows installer script (`packaging/windows/installer.nsi`) and its build and test
scripts still use the previous product name, install directory and uninstall key. They
are renamed together with an upgrade path when Windows work resumes (KAN-162). Windows
builds, tests, packaging and installer execution are paused by owner direction since
13 September 2026; see [the retained installer procedure](windows-installer.md).
