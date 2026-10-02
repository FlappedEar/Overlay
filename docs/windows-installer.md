# Windows candidate installer

Windows builds and installer validation are paused by owner direction on
13 September 2026. The procedure below is retained for later resumption; do not
run it until the owner explicitly requests Windows work again.

Windows Release CI compiles `packaging/windows/installer.nsi` using
[NSIS 3.12](https://nsis.sourceforge.io/Download). This is an unsigned internal
candidate, not beta distribution approval. The existing portable ZIP remains available.

KAN-125 renamed the desktop app to **FlappedEar Overlays** (spelled without a space
since KAN-171), but this installer, its
build and test scripts still use the previous name; rename them, with an upgrade path
from the previous install directory, when Windows work resumes (KAN-162). KAN-18 set the
display name, executable, shortcuts and candidate filenames to
**Flapped Ear Telemetry**. The legacy install directory and uninstall registry key
remain unchanged so existing installations are still detected. These are static
source changes; Windows build and installer execution remain unverified while
the owner pause is active. See [identity and upgrades](application-identity.md).

## Installation and removal

- Verify the `*-setup.exe.sha256` sidecar, then run `*-setup.exe` as your ordinary user.
- Installation is fixed at `%LOCALAPPDATA%\Programs\FlappedEar Telemetry` and needs no
  administrator rights. `/D` is intentionally ignored. The payload targets Windows x64;
  supported OS/hardware acceptance remains recorded in [beta acceptance](beta-acceptance.md).
- Start-menu application and uninstall shortcuts and an HKCU Windows uninstall entry
  are created. No desktop shortcut, file association, startup task or PATH change is made.
- External FFmpeg/ffprobe must be installed separately; see the beta prerequisites.
- Close the app before uninstalling from Windows Settings. Removal deletes only exact
  packaged file paths, shortcuts and the installer registration. It leaves settings,
  recovery data, projects, media and extra files alone. Do not store your own files at
  package-owned paths: those are application files and will be removed.
- For updates, uninstall the old candidate, then install the new one. An existing
  installed candidate is rejected with exit code 2, including silent mode, before file
  copying. There is no in-place updater in this first installer.
- `/S` supports silent installation/removal. An uninstall failure leaves registration
  and the uninstaller available for retry. Close the app and retry if files are locked.
  A moved old uninstaller is rejected if its build identity differs from registration.
- Record SmartScreen/security prompts; publisher signing remains pending. Do not disable
  Windows protections to meet acceptance.

## Build and validation

First build, deploy and validate the Release staging tree as described in beta acceptance,
including `scripts/package_candidate.py`. Then, in PowerShell with NSIS 3.12 installed:

```powershell
python scripts/build_windows_installer.py --stage native-dist/stage --makensis "${env:ProgramFiles(x86)}/NSIS/makensis.exe"
```

The builder verifies payload hashes against the candidate manifest and generates exact
NSIS install/remove includes under `native-dist/nsis`. The application version comes from
CMake; the filename and Windows registration identify the manifest commit. The installer
and its SHA-256 sidecar are attached to the same CI artifact as the ZIP. Generated includes
and binary artifacts are not committed. NSIS uses its zlib compressor; dependency provenance
is listed in [third-party notices](../THIRD_PARTY_NOTICES.md).

On an isolated Windows runner only:

```powershell
python scripts/test_windows_installer.py --stage native-dist/stage --qt-root "$env:QT_ROOT_DIR"
```

This refuses an existing installation, verifies all installed payload hashes, registration
and shortcut presence, hides the Qt SDK during installed startup, checks rejection of an
in-place reinstall, then removes the app and verifies a user-added file survives. It repeats
the complete cycle to exercise uninstall/reinstall with that retained file. Logs are in
`ci-logs/installer-*.txt`. This hosted CI check does not establish interactive installer UX,
clean-machine playback/export, hardware encoder support or publisher signing acceptance.
