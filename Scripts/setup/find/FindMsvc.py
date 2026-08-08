"""MSVC 도구 경로 탐색."""

from __future__ import annotations

import os
import platform
import subprocess
from functools import lru_cache

from ConfigHelper import NormalizePath


@lru_cache(maxsize=1)
def FindMsvcPath() -> str:
    """vswhere / 환경 변수로 MSVC 도구 디렉터리를 찾습니다."""
    if platform.system() != "Windows":
        return ""

    env_vc = os.environ.get("VCToolsInstallDir")
    if env_vc and os.path.exists(env_vc):
        return NormalizePath(env_vc)

    pf = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    if not pf:
        return ""

    vswhere = os.path.join(pf, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        return ""

    try:
        cmd = [
            vswhere,
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools",
            "-property",
            "installationPath",
        ]
        vs_path = subprocess.check_output(cmd, text=True, encoding="utf-8", errors="replace").strip()
        if not vs_path:
            cmd_fallback = [vswhere, "-latest", "-products", "*", "-property", "installationPath"]
            vs_path = subprocess.check_output(
                cmd_fallback, text=True, encoding="utf-8", errors="replace"
            ).strip()

        if vs_path:
            msvc_base = os.path.join(vs_path, "VC", "Tools", "MSVC")
            if os.path.exists(msvc_base):
                vers = [
                    d
                    for d in os.listdir(msvc_base)
                    if os.path.isdir(os.path.join(msvc_base, d))
                ]
                if vers:
                    vers.sort(reverse=True)
                    return NormalizePath(os.path.join(msvc_base, vers[0]))
    except Exception:
        pass

    return ""
