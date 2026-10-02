"""Package a tested Release install for internal acceptance, without publishing a release."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import tempfile


def smoke_installed(executable, sdk, log_path):
    system = platform.system()
    env = os.environ.copy()
    for key in ("QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "DYLD_LIBRARY_PATH",
                "DYLD_FRAMEWORK_PATH", "QT_ROOT_DIR"):
        env.pop(key, None)
    env["PATH"] = os.pathsep.join(p for p in env.get("PATH", "").split(os.pathsep)
                                  if not Path(p).resolve().is_relative_to(sdk))
    env.update(QT_QPA_PLATFORM="cocoa" if system == "Darwin" else "windows",
               QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1")
    hidden_sdk = sdk.with_name(sdk.name + "-candidate-smoke-hidden")
    if hidden_sdk.exists():
        raise RuntimeError("SDK isolation destination already exists")
    sdk.rename(hidden_sdk)
    try:
        with tempfile.TemporaryDirectory() as cwd:
            result = subprocess.run([str(executable), "--startup-smoke"], cwd=cwd, env=env,
                                    capture_output=True, text=True, errors="replace", timeout=60)
    finally:
        hidden_sdk.rename(sdk)
    log = result.stdout + result.stderr
    log_path.write_text(log, encoding="utf-8")
    if result.returncode or "Startup smoke passed" not in log or any(
            marker in log for marker in ("ReferenceError", "TypeError", "Binding loop")):
        raise RuntimeError("Installed candidate failed startup smoke:\n" + log[-8000:])



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--qt-root", type=Path, required=True)
    args = parser.parse_args()
    stage, sdk = args.stage.resolve(), args.qt_root.resolve()
    repo = Path(__file__).resolve().parents[1]
    system = platform.system()
    if system not in ("Darwin", "Windows"):
        raise RuntimeError("Candidate packaging supports macOS and Windows only")
    executable = stage / ("FlappedEar Overlays.app/Contents/MacOS/FlappedEar Overlays"
                          if system == "Darwin" else "bin/FlappedEar Overlays.exe")
    if not executable.is_file():
        raise RuntimeError(f"Missing installed application: {executable}")
    for path in stage.rglob("*"):
        if path.is_symlink() and (not path.exists() or not path.resolve().is_relative_to(stage)):
            raise RuntimeError(f"Installed symlink escapes package or is broken: {path}")
    if system == "Darwin":
        bundle = stage / "FlappedEar Overlays.app"
        with (bundle / "Contents/Info.plist").open("rb") as handle:
            info = plistlib.load(handle)
        expected = {"CFBundleName": "FlappedEar Overlays",
                    "CFBundleExecutable": "FlappedEar Overlays",
                    "CFBundleIdentifier": "com.flappedear.overlays"}
        if any(info.get(key) != value for key, value in expected.items()):
            raise RuntimeError("Candidate bundle name or compatibility identity changed")
        # The baseline has no Finder document association or file-open handler.
        # Do not advertise double-click support as part of a display rename.
        if info.get("CFBundleDocumentTypes") or info.get("CFBundleURLTypes"):
            raise RuntimeError("Unexpected document/URL association in candidate bundle")
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(bundle)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)

    smoke_installed(executable, sdk, repo / "ci-logs/candidate-smoke.txt")

    for name in ("README.md", "THIRD_PARTY_NOTICES.md"):
        shutil.copyfile(repo / name, stage / name)
    shutil.copytree(repo / "docs", stage / "docs", dirs_exist_ok=True)
    shutil.copytree(repo / ".github/workflows", stage / ".github/workflows", dirs_exist_ok=True)
    sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    files = []
    for path in sorted(stage.rglob("*")):
        relative = path.relative_to(stage).as_posix()
        if path.is_symlink():
            files.append({"path": relative, "symlink": os.readlink(path)})
        elif path.is_file():
            with path.open("rb") as handle:
                digest = hashlib.file_digest(handle, "sha256").hexdigest()
            files.append({"path": relative, "bytes": path.stat().st_size, "sha256": digest})
    manifest = {"productName": "FlappedEar Overlays", "commit": sha, "platform": system, "architecture": platform.machine(),
                "buildType": "Release", "qt": "6.8.3", "betaApproved": False,
                "startupWithoutBuildSdk": "passed", "files": files}
    (stage / "candidate-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    artifacts = repo / "native-dist/artifacts"
    artifacts.mkdir(parents=True, exist_ok=True)
    name = f"FlappedEar-Overlays-{system}-{platform.machine()}-{sha[:12]}"
    archive = Path(shutil.make_archive(str(artifacts / name), "gztar" if system == "Darwin" else "zip",
                                       root_dir=stage.parent, base_dir=stage.name))
    with archive.open("rb") as handle:
        digest = hashlib.file_digest(handle, "sha256").hexdigest()
    (artifacts / (archive.name + ".sha256")).write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    shutil.copyfile(stage / "candidate-manifest.json", artifacts / "candidate-manifest.json")
    print(f"Internal candidate created: {archive.name}; SDK-isolated startup passed")


if __name__ == "__main__":
    main()
