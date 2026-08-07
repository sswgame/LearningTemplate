"""
Scripts/FindVcpkg.py

[역할 및 목적]
vcpkg C++ 패키지 관리자의 설치 경로를 프로젝트 탐색 지점에 따라 자동으로 검색하고,
만약 시스템 어디에도 설치되어 있지 않다면 `Tools/vcpkg` 디렉터리로 git clone 및 부트스트랩(bootstrap)을 실행하는 자동화 스크립트입니다.

[탐색 5단계 알고리즘]
1. `Config/engine_config.json` 내 사전 설정 경로 확인
2. 프로젝트 하위 `Tools/vcpkg` 존재 여부 확인
3. 시스템 환경 변수 (`VCPKG_ROOT`, `VCPKG_INSTALLATION_ROOT`) 검색
4. OS `PATH` 시스템 경로 내 vcpkg 바이너리 검색
5. 감지 실패 시 Microsoft 원격 vcpkg 저장소에서 `Tools/vcpkg`로 자동 클론 및 설치
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

from ConfigHelper import GetProjectRoot, LoadEngineConfig, UpdateEngineConfig

def FindOrInstallVcpkg() -> Optional[Path]:
    """
    vcpkg 설치 경로를 동적으로 쿼리하고, 없으면 Tools/vcpkg 하위에 자동 설치합니다.

    Returns:
        Optional[Path]: 검출되거나 새로 설치된 vcpkg 루트 절대 경로 객체 (실패 시 None)
    """
    project_root = GetProjectRoot()
    cfg = LoadEngineConfig()

    vcpkg_cfg = cfg.get("vcpkg_root")
    if vcpkg_cfg and (Path(vcpkg_cfg) / "scripts" / "buildsystems" / "vcpkg.cmake").exists():
        return Path(vcpkg_cfg).resolve()

    tools_vcpkg = project_root / "Tools" / "vcpkg"
    if (tools_vcpkg / "scripts" / "buildsystems" / "vcpkg.cmake").exists():
        UpdateEngineConfig("vcpkg_root", str(tools_vcpkg))
        return tools_vcpkg.resolve()

    for env_var in ["VCPKG_ROOT", "VCPKG_INSTALLATION_ROOT"]:
        val = os.environ.get(env_var)
        if val and (Path(val) / "scripts" / "buildsystems" / "vcpkg.cmake").exists():
            resolved = Path(val).resolve()
            UpdateEngineConfig("vcpkg_root", str(resolved))
            return resolved

    vcpkg_bin = shutil.which("vcpkg")
    if vcpkg_bin:
        bin_path = Path(vcpkg_bin).resolve()
        parent = bin_path.parent
        if (parent / "scripts" / "buildsystems" / "vcpkg.cmake").exists():
            UpdateEngineConfig("vcpkg_root", str(parent))
            return parent
        if (parent.parent / "scripts" / "buildsystems" / "vcpkg.cmake").exists():
            UpdateEngineConfig("vcpkg_root", str(parent.parent))
            return parent.parent

    tools_dir = project_root / "Tools"
    tools_dir.mkdir(parents=True, exist_ok=True)

    sys.stderr.write("[FindVcpkg] vcpkg not found. Automatically cloning and bootstrapping under Tools/vcpkg...\n")
    try:
        subprocess.run(["git", "clone", "https://github.com/microsoft/vcpkg.git", str(tools_vcpkg)], check=True)
        bootstrap_script = tools_vcpkg / ("bootstrap-vcpkg.bat" if sys.platform == "win32" else "bootstrap-vcpkg.sh")
        subprocess.run([str(bootstrap_script)], cwd=str(tools_vcpkg), check=True)
        if (tools_vcpkg / "scripts" / "buildsystems" / "vcpkg.cmake").exists():
            resolved = tools_vcpkg.resolve()
            UpdateEngineConfig("vcpkg_root", str(resolved))
            return resolved
    except Exception as e:
        sys.stderr.write(f"[FindVcpkg Error] Failed to auto-install vcpkg under Tools/: {e}\n")

    return None

if __name__ == "__main__":
    vcpkg_path = FindOrInstallVcpkg()
    if vcpkg_path:
        print(vcpkg_path.as_posix())
        sys.exit(0)
    else:
        sys.stderr.write("[FindVcpkg Error] vcpkg not found\n")
        sys.exit(1)
