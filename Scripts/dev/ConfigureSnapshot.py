#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/ConfigureSnapshot.py

CMake 구성 결과를 비교할 수 있는 모양으로 떠서(스냅숏) 두 개를 견줍니다 — **CMake 를 고친 뒤 "동작이 같은가"** 를 보는 도구.

CMake 리팩터는 컴파일러가 잡아 주지 않는다. 같은 일을 하는 함수로 바꿨는데 정의 하나 · 링크 순서 하나 · 출력 폴더 하나가
달라져도 configure 는 조용히 통과한다. 그래서 고치기 전과 뒤의 구성 결과를 기계로 견준다:

- **타깃**: 종류 · 디스크 이름 · 산출물 경로 · IDE 폴더 · 소스 목록 · 컴파일 정의 · include(시스템 여부) · 컴파일 조각 · PCH ·
  링크 조각(순서 그대로) · 의존 타깃 — CMake File API(codemodel-v2)의 답에서 읽는다.
- **캐시**: INTERNAL · STATIC 이 아닌 항목 전부(`SW_*` 옵션 · 도구 경로).
- **생성 파일**: `generated/sw/config/*` · `Bin/Modules/*`(매니페스트 · 적재 순서) · 리플렉션 입력 목록의 내용 해시.
- **CTest**: `ctest --show-only=json-v1` 의 시험 이름 · 명령 · 속성(라벨 · 제한 시간 · 작업 폴더 · 환경).

정의 · include 는 집합으로(순서가 의미 없다), 링크 조각 · 명령은 목록으로(순서가 의미 있다) 견준다.

  py -3 Scripts/dev/ConfigureSnapshot.py prepare --preset Ninja-Debug           # File API 질의를 둔다(configure 전에 한 번)
  cmake --preset Ninja-Debug
  py -3 Scripts/dev/ConfigureSnapshot.py take --preset Ninja-Debug --out before.json
  ... CMake 를 고친다 ...
  cmake --preset Ninja-Debug
  py -3 Scripts/dev/ConfigureSnapshot.py take --preset Ninja-Debug --out after.json
  py -3 Scripts/dev/ConfigureSnapshot.py diff before.json after.json             # 다르면 1

  cmake --preset Ninja-Debug --profiling-format=google-trace --profiling-output=trace.json
  py -3 Scripts/dev/ConfigureSnapshot.py profile trace.json --top 25             # 구성 시간을 어디서 쓰나
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

from common import getProjectRoot  # noqa: E402

_kTag = "ConfigureSnapshot"
#: 이 도구가 File API 에 남기는 질의 — 클라이언트 이름을 따로 둬 VS Code 등 다른 클라이언트의 질의와 섞이지 않게 한다.
_kQueryRelPath = Path(".cmake/api/v1/query/client-sw-snapshot/query.json")
_kQueryContent = {"requests": [{"kind": "codemodel", "version": 2}, {"kind": "cache", "version": 2}]}
#: 내용을 견줄 생성 파일(빌드 폴더 기준 glob).
_kGeneratedGlob = (
    "generated/sw/config/*",
    "generated/*/ReflectionInputs.list",
    "generated/moduleidentity/*",
    "Bin/Modules/*",
)
#: 캐시에서 견주지 않는 종류 — CMake 가 스스로 쓰는 값이라 리팩터와 무관하게 바뀐다.
_kIgnoredCacheType = {"INTERNAL", "STATIC"}


def resolveBuildDirInternal(repositoryRoot: Path, preset: str, buildDir: str) -> Path:
    if buildDir:
        return Path(buildDir).resolve()
    return (repositoryRoot / "build" / preset).resolve()


def prepareQuery(buildDir: Path) -> int:
    """다음 configure 가 이 도구용 File API 답을 쓰도록 질의 파일을 둡니다."""
    queryPath = buildDir / _kQueryRelPath
    queryPath.parent.mkdir(parents=True, exist_ok=True)
    queryPath.write_text(json.dumps(_kQueryContent, indent=2) + "\n", encoding="utf-8")
    print(f"[{_kTag}] 질의를 뒀습니다: {queryPath.as_posix()} — 이제 cmake --preset 을 돌리십시오")
    return 0


def readReplyIndexInternal(buildDir: Path) -> dict[str, Any]:
    replyDir = buildDir / ".cmake/api/v1/reply"
    listIndex = sorted(replyDir.glob("index-*.json"))
    if not listIndex:
        raise FileNotFoundError(f"{replyDir.as_posix()} 에 File API 답이 없습니다 — `prepare` 뒤 configure 하십시오")
    return json.loads(listIndex[-1].read_text(encoding="utf-8"))


def findReplyFileInternal(buildDir: Path, index: dict[str, Any], kind: str) -> Path:
    for item in index.get("objects", []):
        if item.get("kind") == kind:
            return buildDir / ".cmake/api/v1/reply" / item["jsonFile"]
    # 다른 클라이언트의 질의로 생긴 답만 있을 때(질의를 두지 않은 빌드 폴더) — 같은 종류의 최신 파일을 쓴다.
    listCandidate = sorted((buildDir / ".cmake/api/v1/reply").glob(f"{kind}-v*-*.json"), key=lambda path: path.stat().st_mtime)
    if not listCandidate:
        raise FileNotFoundError(f"File API 답에 {kind} 가 없습니다")
    return listCandidate[-1]


def relativeInternal(path: str, repositoryRoot: Path, buildDir: Path) -> str:
    """경로를 `<src>/…` · `<build>/…` 로 — 다른 폴더에서 뜬 스냅숏도 견줄 수 있게."""
    normalized = Path(path)
    if not normalized.is_absolute():
        return normalized.as_posix()
    for prefix, root in (("<build>", buildDir), ("<src>", repositoryRoot)):
        try:
            return f"{prefix}/{normalized.resolve().relative_to(root).as_posix()}"
        except ValueError:
            continue
    return normalized.as_posix()


def snapshotTargetInternal(replyDir: Path, jsonFile: str, repositoryRoot: Path, buildDir: Path,
                           mapIdToName: dict[str, str]) -> dict[str, Any]:
    data = json.loads((replyDir / jsonFile).read_text(encoding="utf-8"))

    def rel(path: str) -> str:
        return relativeInternal(path, repositoryRoot, buildDir)

    # 컴파일 묶음은 "언어:첫 소스" 를 키로 둔다 — 목록으로 두면 한 정의가 달라도 묶음 전체가 한 줄로 찍혀 무엇이 다른지 안 보인다.
    mapCompileGroup: dict[str, Any] = {}
    for group in data.get("compileGroups", []):
        listSource = sorted(rel(data["sources"][index]["path"]) for index in group.get("sourceIndexes", []))
        mapCompileGroup[f"{group.get('language', '')}:{listSource[0] if listSource else ''}"] = {
            "standard": group.get("languageStandard", {}).get("standard", ""),
            "defines": sorted(item["define"] for item in group.get("defines", [])),
            "includes": sorted(f"{'system:' if item.get('isSystem') else ''}{rel(item['path'])}" for item in group.get("includes", [])),
            "fragments": [item["fragment"] for item in group.get("compileCommandFragments", [])],
            "pch": sorted(rel(item["header"]) for item in group.get("precompileHeaders", [])),
            "sources": listSource,
        }

    link = data.get("link") or {}
    return {
        "type": data.get("type", ""),
        "nameOnDisk": data.get("nameOnDisk", ""),
        "folder": (data.get("folder") or {}).get("name", ""),
        "artifacts": sorted(rel(item["path"]) for item in data.get("artifacts", [])),
        "sources": sorted(rel(item["path"]) for item in data.get("sources", [])),
        "compileGroups": mapCompileGroup,
        "link": [rel(item["fragment"]) if "/" in item["fragment"] or "\\" in item["fragment"] else item["fragment"]
                 for item in link.get("commandFragments", [])],
        "dependencies": sorted(mapIdToName.get(item["id"], item["id"]) for item in data.get("dependencies", [])),
    }


def snapshotCtestInternal(buildDir: Path, repositoryRoot: Path) -> dict[str, Any]:
    completed = subprocess.run(["ctest", "--show-only=json-v1"], cwd=str(buildDir), capture_output=True,
                               encoding="utf-8", errors="replace", check=False)
    if completed.returncode != 0:
        return {"error": completed.stderr.strip()}
    data = json.loads(completed.stdout)
    mapTest: dict[str, Any] = {}
    for test in data.get("tests", []):
        mapProperty = {item["name"]: item["value"] for item in test.get("properties", [])}
        mapTest[test["name"]] = {
            "command": [relativeInternal(part, repositoryRoot, buildDir) for part in test.get("command", [])],
            "properties": {name: (sorted(value) if isinstance(value, list) else value) for name, value in sorted(mapProperty.items())},
        }
    return mapTest


def takeSnapshot(buildDir: Path, repositoryRoot: Path, bIncludeCtest: bool) -> dict[str, Any]:
    """빌드 폴더 하나의 구성 결과를 스냅숏으로 만듭니다."""
    index = readReplyIndexInternal(buildDir)
    replyDir = buildDir / ".cmake/api/v1/reply"

    codemodel = json.loads(findReplyFileInternal(buildDir, index, "codemodel").read_text(encoding="utf-8"))
    configuration = codemodel["configurations"][0]
    mapIdToName = {target["id"]: target["name"] for target in configuration["targets"]}
    mapTarget = {target["name"]: snapshotTargetInternal(replyDir, target["jsonFile"], repositoryRoot, buildDir, mapIdToName)
                 for target in configuration["targets"]}

    cache = json.loads(findReplyFileInternal(buildDir, index, "cache").read_text(encoding="utf-8"))
    mapCache = {entry["name"]: entry["value"] for entry in cache["entries"] if entry.get("type") not in _kIgnoredCacheType}

    mapGenerated: dict[str, str] = {}
    for pattern in _kGeneratedGlob:
        for path in sorted(buildDir.glob(pattern)):
            if path.is_file():
                mapGenerated[path.relative_to(buildDir).as_posix()] = hashlib.sha1(path.read_bytes()).hexdigest()

    snapshot: dict[str, Any] = {"configuration": configuration["name"], "targets": mapTarget, "cache": mapCache,
                                "generated": mapGenerated}
    if bIncludeCtest:
        snapshot["ctest"] = snapshotCtestInternal(buildDir, repositoryRoot)
    return snapshot


def diffValueInternal(path: str, before: Any, after: Any, listLine: list[str]) -> None:
    if isinstance(before, dict) and isinstance(after, dict):
        for key in sorted(set(before) | set(after)):
            if key not in after:
                listLine.append(f"- {path}/{key}")
            elif key not in before:
                listLine.append(f"+ {path}/{key}")
            else:
                diffValueInternal(f"{path}/{key}", before[key], after[key], listLine)
        return
    if before == after:
        return
    if isinstance(before, list) and isinstance(after, list) and all(isinstance(item, str) for item in before + after):
        setBefore, setAfter = set(before), set(after)
        if setBefore == setAfter:
            listLine.append(f"~ {path}: 같은 원소, 순서가 다르다")
            return
        for item in sorted(setBefore - setAfter):
            listLine.append(f"- {path}: {item}")
        for item in sorted(setAfter - setBefore):
            listLine.append(f"+ {path}: {item}")
        return
    listLine.append(f"~ {path}: {before!r} -> {after!r}")


def diffSnapshots(beforePath: Path, afterPath: Path) -> list[str]:
    """두 스냅숏의 차이를 줄 목록으로. 같으면 빈 목록."""
    before = json.loads(beforePath.read_text(encoding="utf-8"))
    after = json.loads(afterPath.read_text(encoding="utf-8"))
    listLine: list[str] = []
    diffValueInternal("", before, after, listLine)
    return listLine


def summarizeProfile(tracePath: Path, top: int) -> list[str]:
    """`--profiling-format=google-trace` 출력에서 위치(파일:줄)별 포함 시간을 큰 순서로."""
    listEvent = json.loads(tracePath.read_text(encoding="utf-8"))
    if isinstance(listEvent, dict):
        listEvent = listEvent.get("traceEvents", [])
    mapDuration: dict[str, float] = defaultdict(float)
    mapCount: dict[str, int] = defaultdict(int)
    totalUs = 0.0
    for event in listEvent:
        if event.get("ph") != "X":
            continue
        location = (event.get("args") or {}).get("location", "")
        key = f"{event.get('name', '')} @ {location}"
        mapDuration[key] += float(event.get("dur", 0.0))
        mapCount[key] += 1
        if event.get("name") == "project" or not location:
            totalUs = max(totalUs, float(event.get("dur", 0.0)))
    listRow = sorted(mapDuration.items(), key=lambda item: item[1], reverse=True)[:top]
    return [f"{duration / 1000.0:9.1f} ms  x{mapCount[key]:<5d} {key}" for key, duration in listRow]


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="CMake 구성 결과 스냅숏 · 비교 · 구성 시간 요약")
    subparsers = parser.add_subparsers(dest="command", required=True)

    for name in ("prepare", "take"):
        sub = subparsers.add_parser(name)
        sub.add_argument("--preset", default="Ninja-Debug", help="빌드 폴더 build/<프리셋> (기본 Ninja-Debug)")
        sub.add_argument("--build-dir", default="", help="빌드 폴더를 직접 줄 때")
        if name == "take":
            sub.add_argument("--out", type=Path, required=True, help="스냅숏을 쓸 JSON")
            sub.add_argument("--no-ctest", action="store_true", help="CTest 목록을 뜨지 않는다")

    diffParser = subparsers.add_parser("diff")
    diffParser.add_argument("before", type=Path)
    diffParser.add_argument("after", type=Path)

    profileParser = subparsers.add_parser("profile")
    profileParser.add_argument("trace", type=Path)
    profileParser.add_argument("--top", type=int, default=25)

    args = parser.parse_args(argv)
    repositoryRoot = getProjectRoot().resolve()

    if args.command == "prepare":
        return prepareQuery(resolveBuildDirInternal(repositoryRoot, args.preset, args.build_dir))

    if args.command == "take":
        buildDir = resolveBuildDirInternal(repositoryRoot, args.preset, args.build_dir)
        try:
            snapshot = takeSnapshot(buildDir, repositoryRoot, not args.no_ctest)
        except FileNotFoundError as error:
            print(f"[{_kTag}] {error}", file=sys.stderr)
            return 2
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(snapshot, indent=1, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"[{_kTag}] 타깃 {len(snapshot['targets'])} · 캐시 {len(snapshot['cache'])} · 생성 파일 {len(snapshot['generated'])}"
              f"{' · 시험 ' + str(len(snapshot['ctest'])) if 'ctest' in snapshot else ''} → {args.out.as_posix()}")
        return 0

    if args.command == "diff":
        listLine = diffSnapshots(args.before, args.after)
        for line in listLine:
            print(line)
        print(f"[{_kTag}] {'같다' if not listLine else f'다른 곳 {len(listLine)}'}")
        return 1 if listLine else 0

    for line in summarizeProfile(args.trace, args.top):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
