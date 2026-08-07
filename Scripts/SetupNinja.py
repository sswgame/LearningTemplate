"""
Scripts/SetupNinja.py

[초심자를 위한 역할 및 기능 설명]
Ninja는 CMake 빌드 속도를 기존 Visual Studio MSBuild 대비 5~10배 빠르게 만들어주는 초고속 빌드 툴입니다.
이 스크립트는 개발자의 컴퓨터 시스템 PATH나 `Tools/Ninja` 폴더에 Ninja가 없으면,
`Config/search_paths.json`의 원격 URL에서 Ninja 실행 파일을 자동으로 다운로드하고 압축을 풀어 `Tools/Ninja`에 배치해줍니다.

[동작 3단계]
1. SYSTEM PATH 내 `ninja` 또는 `ninja.exe` 감지
2. 프로젝트 하위 `Tools/Ninja` 내 실행 파일 검출
3. 없으면 원격 zip 다운로드 -> 압축 해제 -> 실행 권한 부여(Linux/macOS) 후 `Config/engine_config.json` 저장
"""

import json
import os
import platform
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path
from typing import Any, Dict

from ConfigHelper import GetProjectRoot, UpdateEngineConfig

def LoadSearchPaths() -> Dict[str, Any]:
    """
    Config/search_paths.json 설정을 안전하게 로드합니다.
    (원격 Ninja 바이너리 다운로드 URL 목록 등이 저장되어 있습니다)
    """
    root_dir = GetProjectRoot()
    search_file = root_dir / "Config" / "search_paths.json"
    if search_file.exists():
        try:
            with open(search_file, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {}

def SetupNinja() -> str:
    """Ninja 빌드 도구를 탐색하고, 없으면 다운로드 및 설치합니다."""
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
    search_config = LoadSearchPaths()
    download_urls = search_config.get("ninja_download_urls", {})

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
