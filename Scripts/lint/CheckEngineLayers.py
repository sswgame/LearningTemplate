#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engine 레이어 금지 include 검사.

강제 규칙:
  1) Source/Engine/** 에서 Editor / GameFramework / Games 경로 include 금지.
  2) Source/Games/**, Source/GameFramework/** 에서 GameObjectManagerInternal.h 금지.
  3) Source/Games/**, Source/GameFramework/** 에서 Engine/Common/EngineServices.h 금지
     (게임 쪽은 RuntimeAPI/GameService.h 의 game:: 만 사용).
  4) 의도 레이어(문서):
       Common/Utility → Reflection/Serialization → Object/Scene → Graphics/Input/Audio/Window
       Physics / Animation 은 experimental stub.

  python Scripts/lint/CheckEngineLayers.py [--root <repo>] [--strict]
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ConfigHelper import getProjectRoot
import ConfigHelper

_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

def includeHitsBanInternal(inc: str, ban: str) -> bool:
    normParts = inc.replace("\\", "/").split("/")
    banParts = [p for p in ban.replace("\\", "/").split("/") if p]
    if not banParts:
        return False
    for i in range(len(normParts) - len(banParts) + 1):
        if normParts[i : i + len(banParts)] == banParts:
            return True
    return False

_kDirSourceEngine = ConfigHelper.kDirSourceEngine
_kDirSourceGames = ConfigHelper.kDirSourceGames
_kDirSourceGameFramework = ConfigHelper.kDirSourceGameFramework
_kFileGameObjectManagerInternal = ConfigHelper.kFileGameObjectManagerInternal
_kFileEngineServices = ConfigHelper.kFileEngineServices

_kForbiddenRules: list[tuple[str, tuple[str, ...]]] = [
    (
        f"{_kDirSourceEngine}/",
        (
            "Editor/",
            "Source/Editor/",
            "GameFramework/",
            "Games/",
            f"{_kDirSourceGames}/",
        ),
    ),
    (
        f"{_kDirSourceGames}/",
        (_kFileGameObjectManagerInternal, _kFileEngineServices, "EngineServices.h"),
    ),
    (
        f"{_kDirSourceGameFramework}/",
        (_kFileGameObjectManagerInternal, _kFileEngineServices, "EngineServices.h"),
    ),
]

# Strict mode: reverse edges that fight the intended layer order.
_kStrictReverse: list[tuple[str, str]] = [
    ("Utility", "Graphics"),
    ("Reflection", "Object"),
    ("Physics", "Graphics"),
    ("Animation", "Graphics"),
]

def iterSourceFiles(engineRoot: Path) -> list[Path]:
    exts = {".h", ".hpp", ".inl", ".c", ".cpp", ".cc", ".cxx"}
    return [p for p in engineRoot.rglob("*") if p.is_file() and p.suffix.lower() in exts]

def processFile(path: Path, repo: Path, strict: bool) -> tuple[list[str], list[str], Optional[str]]:
    rel = path.relative_to(repo).as_posix()
    try:
        text = path.read_text(encoding="utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        return [], [], f"[CheckEngineLayers] UTF-8 인코딩 오류: {rel}: {exc}"
    except OSError as exc:
        return [], [], f"[CheckEngineLayers] 읽기 실패: {rel}: {exc}"

    fileViolations: list[str] = []
    fileStrictWarns: list[str] = []
    prefix = f"{_kDirSourceEngine}/"

    for rulePrefix, banned in _kForbiddenRules:
        if not rel.startswith(rulePrefix):
            continue
        for inc in _kIncludeRe.findall(text):
            norm = inc.replace("\\", "/")
            for ban in banned:
                if includeHitsBanInternal(norm, ban):
                    fileViolations.append(f'{rel}: #include "{inc}"  (금지: {ban})')

    srcLayer = ""
    if rel.startswith(prefix) and "/" in rel[len(prefix) :]:
        srcLayer = rel[len(prefix) :].split("/", 1)[0]

    # ReflectGenerated.h는 .gen.cpp 리플렉션 생성 코드 전용 preamble이며, ResourceManager.cpp는 파사드 구현체입니다.
    if rel.endswith("ReflectGenerated.h") or rel.endswith("ResourceManager.cpp"):
        return fileViolations, [], None

    for inc in _kIncludeRe.findall(text):
        norm = inc.replace("\\", "/")
        if "Engine/" not in norm:
            continue
        dstParts = norm.split("Engine/", 1)[-1].split("/")
        if not dstParts:
            continue
        dstLayer = dstParts[0]
        for srcBan, dstBan in _kStrictReverse:
            if srcLayer == srcBan and dstLayer == dstBan:
                msg = f'{rel}: #include "{inc}"  (레이어 {srcBan}->{dstBan})'
                if strict:
                    fileViolations.append(msg)
                else:
                    fileStrictWarns.append(msg)

    return fileViolations, fileStrictWarns, None

def main() -> int:
    parser = argparse.ArgumentParser(description="Engine 레이어 금지 include 검사")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
    parser.add_argument("--strict", action="store_true", help="내부 reverse-edge도 실패로 처리")
    args = parser.parse_args()
    repo = (args.root or getProjectRoot()).resolve()
    engineDir = repo / _kDirSourceEngine
    if not engineDir.is_dir():
        print(f"[CheckEngineLayers] Engine 경로 없음: {engineDir}", file=sys.stderr)
        return 2

    scanRoots = [engineDir, repo / _kDirSourceGames, repo / _kDirSourceGameFramework]

    allFiles: list[Path] = []
    for scanRoot in scanRoots:
        if scanRoot.is_dir():
            allFiles.extend(iterSourceFiles(scanRoot))

    violations: list[str] = []
    strictWarns: list[str] = []

    maxWorkers = min(32, (os.cpu_count() or 4) * 2)
    with concurrent.futures.ThreadPoolExecutor(max_workers=maxWorkers) as executor:
        futures = [executor.submit(processFile, p, repo, args.strict) for p in allFiles]
        for future in concurrent.futures.as_completed(futures):
            v, sw, err = future.result()
            if err:
                print(err, file=sys.stderr)
                return 2
            violations.extend(v)
            strictWarns.extend(sw)

    if strictWarns and not args.strict:
        print(f"[CheckEngineLayers] 내부 레이어 경고 {len(strictWarns)}건 (--strict 시 실패):")
        for line in strictWarns[:20]:
            print(f"  - {line}")
        if len(strictWarns) > 20:
            print(f"  ... +{len(strictWarns) - 20} more")

    if violations:
        print("[CheckEngineLayers] 레이어 위반:")
        for line in violations:
            print(f"  - {line}")
        return 1

    print(f"[CheckEngineLayers] OK ({len(allFiles)} files scanned in parallel)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
