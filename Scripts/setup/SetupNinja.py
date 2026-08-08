"""
Scripts/setup/SetupNinja.py

Ninja 탐색 → Tools/Ninja 로컬 → search_paths.json URL 다운로드.
"""

from __future__ import annotations

import os
import platform
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parents[1]
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from ConfigHelper import EnsureScriptsOnPath, GetProjectRoot, LoadSearchPaths, UpdateEngineConfig

EnsureScriptsOnPath()


def SetupNinja() -> str:
    """Ninja 빌드 도구를 탐색하고, 없으면 다운로드합니다."""
    print("[SetupNinja] Checking Ninja build tool existence...")

    system_ninja = shutil.which("ninja")
    if system_ninja:
        print(f"[SetupNinja] Found Ninja in system PATH: {system_ninja}")
        UpdateEngineConfig("ninja_path", system_ninja)
        return system_ninja

    root_dir = GetProjectRoot()
    tools_ninja_dir = root_dir / "Tools" / "Ninja"
    tools_ninja_dir.mkdir(parents=True, exist_ok=True)

    exe_name = "ninja.exe" if platform.system() == "Windows" else "ninja"
    local_ninja_path = tools_ninja_dir / exe_name

    if local_ninja_path.exists():
        print(f"[SetupNinja] Found local Ninja tool at: {local_ninja_path}")
        UpdateEngineConfig("ninja_path", str(local_ninja_path))
        return str(local_ninja_path)

    print("[SetupNinja] Ninja not found. Downloading Ninja binary...")
    download_urls = LoadSearchPaths().get("ninja_download_urls", {})
    sys_name = platform.system().lower()
    download_url = download_urls.get(sys_name)
    if not download_url:
        print(f"[SetupNinja Error] Unsupported or unconfigured platform: {sys_name}")
        sys.exit(1)

    zip_path = tools_ninja_dir / "ninja.zip"
    try:
        print(f"[SetupNinja] Downloading from {download_url}...")
        urllib.request.urlretrieve(download_url, zip_path)
        print("[SetupNinja] Extracting archive...")
        with zipfile.ZipFile(zip_path, "r") as zip_ref:
            zip_ref.extractall(tools_ninja_dir)
        if zip_path.exists():
            zip_path.unlink()

        if local_ninja_path.exists():
            if platform.system() != "Windows":
                os.chmod(local_ninja_path, 0o755)
            print(f"[SetupNinja] Successfully installed Ninja at: {local_ninja_path}")
            UpdateEngineConfig("ninja_path", str(local_ninja_path))
            return str(local_ninja_path)
    except Exception as e:
        print(f"[SetupNinja Error] Failed to download/extract Ninja: {e}")

    sys.exit(1)


if __name__ == "__main__":
    SetupNinja()
