"""Windows SDK 경로 탐색."""

from __future__ import annotations

import os
from functools import lru_cache
from typing import Tuple

import platform

from ConfigHelper import NormalizePath


@lru_cache(maxsize=1)
def FindWindowsSdkPath() -> Tuple[str, str]:
    """레지스트리 / 환경 변수로 Windows SDK 경로와 버전을 찾습니다."""
    if platform.system() != "Windows":
        return "", ""

    env_sdk = os.environ.get("WindowsSdkDir")
    if env_sdk and os.path.exists(env_sdk):
        version = os.environ.get("WindowsSDKVersion", "").strip("\\")
        return NormalizePath(env_sdk), version

    try:
        import winreg

        key = winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots"
        )
        kits_root, _ = winreg.QueryValueEx(key, "KitsRoot10")
        winreg.CloseKey(key)

        if kits_root and os.path.exists(kits_root):
            inc_dir = os.path.join(kits_root, "Include")
            if os.path.exists(inc_dir):
                vers = [d for d in os.listdir(inc_dir) if d.startswith("10.")]
                if vers:
                    vers.sort(reverse=True)
                    return NormalizePath(kits_root), vers[0]
    except Exception:
        pass

    return "", ""
