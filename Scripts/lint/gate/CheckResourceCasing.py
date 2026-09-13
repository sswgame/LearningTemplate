#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckResourceCasing.py

Resource/ 디렉터리 하위의 모든 파일 및 폴더명이 완전한 소문자(Lowercase)인지 검사합니다.
Linux ext4 등 대소문자 구분 파일시스템 호환성 및 엔진 에셋 명명 표준을 강제합니다.

검사 규칙:
- Resource/ 하위의 모든 디렉터리 이름은 소문자여야 합니다 (대문자 금지).
- Resource/ 하위의 모든 파일 이름은 소문자여야 합니다 (대문자 금지, README.md 제외).
- 위반 사항 발견 시 0이 아닌 종료 코드를 반환하여 Git 커밋 및 CI를 중단시킵니다.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Sequence

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from common import getProjectRoot, useUtf8Stdout

_kAllowedUppercaseBasenames = {"README.md"}


def checkResourceCasing(projectRoot: Path, targetFiles: Sequence[str] | None = None) -> list[str]:
    """
    Resource/ 하위의 파일 및 디렉터리명에 대문자가 포함되어 있는지 검사합니다.
    """
    resourceRoot = (projectRoot / "Resource").resolve()
    if not resourceRoot.is_dir():
        return []

    violations: list[str] = []

    if targetFiles is not None:
        # Staged 파일 목록 검사
        for filePathStr in targetFiles:
            p = Path(filePathStr).resolve()
            try:
                rel = p.relative_to(resourceRoot)
            except ValueError:
                continue

            for part in rel.parts:
                if part in _kAllowedUppercaseBasenames:
                    continue
                if any(ch.isupper() for ch in part):
                    violations.append(f"[Resource Casing] 대문자가 포함된 리소스 경로: Resource/{rel.as_posix()}")
                    break
        return violations

    # 전체 Resource/ 트리 검사
    for root, dirs, files in os.walk(resourceRoot):
        rel_root = Path(root).relative_to(projectRoot).as_posix()
        for d in dirs:
            if any(ch.isupper() for ch in d):
                violations.append(f"[Resource Casing] 대문자가 포함된 디렉터리: {rel_root}/{d}")
        for f in files:
            if f in _kAllowedUppercaseBasenames:
                continue
            if any(ch.isupper() for ch in f):
                violations.append(f"[Resource Casing] 대문자가 포함된 파일: {rel_root}/{f}")

    return violations



# 이 린트가 **반드시 잡아야 하는** 조각. `CheckLintsAreAlive.py` 가 임시 트리에 써서 돌려 보고,
# 통과해 버리면 검사가 죽은 것으로 본다. 조각을 여기 두는 이유는 하나다 — 표를 따로 만들면 어긋난다.
kSelfTestCases = [
    {
        "name": "대문자 리소스 경로",
        "files": {
            "Resource/engine/Textures/Splash.png": "probe",
        },
    },
]

def main() -> int:
    useUtf8Stdout()

    parser = argparse.ArgumentParser(description="Resource 하위 소문자 명명 규칙 검사")
    parser.add_argument("--root", type=Path, default=None, help="프로젝트 루트 디렉터리")
    parser.add_argument("files", nargs="*", help="검사할 특정 파일 경로 목록 (생략 시 전체 Resource/ 검사)")
    args = parser.parse_args()

    projectRoot = (args.root or getProjectRoot()).resolve()
    targetFiles = args.files if args.files else None

    violations = checkResourceCasing(projectRoot, targetFiles)
    if violations:
        print("=" * 60)
        print("  [CheckResourceCasing] Resource 소문자 규칙 위반 발견!")
        print("  Resource/ 하위의 모든 파일/폴더는 반드시 소문자여야 합니다.")
        print("=" * 60)
        for v in violations:
            print(f"  - {v}")
        print(f"\n총 {len(violations)}건의 위반 사항이 발견되어 중단합니다.")
        return 1

    print("[CheckResourceCasing] OK (Resource 하위 모든 파일/폴더 소문자 검증 완료)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
