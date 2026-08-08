"""플랫폼 시스템 헤더 include 경로 수집."""

from __future__ import annotations

import os
import platform
import subprocess
from typing import List

from ConfigHelper import NormalizePath


def FindSystemIncludeDirs() -> List[str]:
    """플랫폼별 헤더 인클루드 경로를 수집합니다."""
    include_dirs: List[str] = []
    if platform.system() != "Darwin":
        return include_dirs

    try:
        sdk_path = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
        if sdk_path and os.path.exists(sdk_path):
            usr_inc = os.path.join(sdk_path, "usr", "include")
            if os.path.exists(usr_inc):
                include_dirs.append(NormalizePath(usr_inc))
    except Exception:
        pass

    return include_dirs
