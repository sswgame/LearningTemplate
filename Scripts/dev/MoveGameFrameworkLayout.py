#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file MoveGameFrameworkLayout.py
@brief GameFramework 폴더 재배치(docs/plans/GameFrameworkLayout.md) 의 이동 표와 경로 치환 — 단계마다 다시 돌릴 수 있습니다.

    py -3 Scripts/dev/MoveGameFrameworkLayout.py --step 1            # 1 단계 이동 + 경로 치환
    py -3 Scripts/dev/MoveGameFrameworkLayout.py --step 1 --dry-run  # 옮길 파일 수와 고칠 파일만 보인다

하는 일은 둘입니다.
  1) 이동 — 표의 한 줄은 폴더 하나(`Dir`) 또는 한 폴더 안의 파일 묶음(`Files`, 확장자를 뺀 이름)입니다. `git mv` 로 옮깁니다.
  2) 치환 — git 이 추적하는 글 파일에서 `GameFramework/Base/…` · `GameFramework/Kits/…` 꼴의 경로를 새 자리로 바꿉니다
     (include · 문서 링크 · 게이트 · CMake 의 경로 글자). 파일 경로는 정확히 같을 때만, 폴더 경로는 앞부분이 같을 때 바꿉니다.

다시 돌려도 됩니다. 이미 옮긴 줄은 새 자리의 파일에서 옛 경로를 거꾸로 구해 치환만 다시 합니다 — 다른 작업이 옛 경로로 include 를 더한 뒤
최신 main 위에서 이 스크립트를 다시 돌리는 경우입니다. 표의 경로는 `Source/GameFramework/` 기준입니다.
"""

from __future__ import annotations

import argparse
import posixpath
import re
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot, runGit, runProcess  # noqa: E402

_kGameFrameworkRoot = "Source/GameFramework"
_kTextSuffixes = {".h", ".hpp", ".inl", ".c", ".cpp", ".py", ".cmake", ".txt", ".md", ".json", ".xml", ".xxx", ".yml", ".yaml", ".ps1",
                  ".hlsl", ".hlsli", ".ini", ".toml"}
_kTokenRe = re.compile(r"GameFramework/(?:Base|Kits)/[A-Za-z0-9_./-]*")
_kSourceSuffixes = (".h", ".hpp", ".inl", ".cpp", ".c")


@dataclass(frozen=True)
class Dir:
    """폴더 하나를 통째로 옮깁니다(하위 폴더 포함)."""

    old: str
    new: str


@dataclass(frozen=True)
class Files:
    """`folder` 안의 파일 가운데 이름(확장자 뺀 것)이 `listStem` 에 있는 것을 `newFolder` 로 옮깁니다."""

    folder: str
    newFolder: str
    listStem: tuple[str, ...]


def filesInternal(folder: str, newFolder: str, stems: str) -> Files:
    return Files(folder, newFolder, tuple(stems.split()))


# ------------------------------------------------------------------------------
# 1 단계 — Base 를 층으로 묶는다(Base/<층>/<폴더>/). 층 순서와 근거는 Scripts/lint/gate/CheckGameFrameworkLayers.py 의 _kBaseLayer.
# ------------------------------------------------------------------------------
_kListLayer: dict[str, tuple[str, ...]] = {
    "Foundation": ("Utility", "Data", "Framework"),
    "World": ("World", "Spline"),
    "Actor": ("Input", "Movement", "Navigation", "Combat", "Camera", "AI", "Control"),
    "UI": ("UI",),
    "Gameplay": ("Inventory", "Progression", "Match", "Ability", "Interaction", "Quest", "Appearance", "Gimmick", "GameState", "Vehicle"),
}
_kStep1: list[Dir | Files] = [Dir(f"Base/{folder}", f"Base/{layer}/{folder}") for layer, listFolder in _kListLayer.items() for folder in listFolder]

_kSteps: dict[int, list[Dir | Files]] = {
    1: _kStep1,
}


def listTrackedInternal(root: Path) -> list[str]:
    result = runGit(["ls-files", "-z"], cwd=root)
    if result.returnCode != 0:
        raise SystemExit(f"git ls-files 실패: {result.stderr}")
    return [path for path in result.stdout.split("\0") if path]


def buildMapInternal(listEntry: list[Dir | Files], setTracked: set[str]) -> tuple[dict[str, str], dict[str, str], list[tuple[str, str]]]:
    """(파일 지도 옛 → 새, 폴더 지도 옛 → 새, 아직 옮기지 않은 (옛, 새) 쌍). 경로는 `Source/GameFramework/` 기준입니다."""
    prefix = _kGameFrameworkRoot + "/"
    listGameFramework = sorted(path[len(prefix):] for path in setTracked if path.startswith(prefix))
    listNewDir = [entry.new for entry in listEntry if isinstance(entry, Dir)]
    mapFile: dict[str, str] = {}
    mapDir: dict[str, str] = {}
    listPending: list[tuple[str, str]] = []
    for entry in listEntry:
        if isinstance(entry, Dir):
            mapDir[entry.old] = entry.new
            for path in listGameFramework:
                if path.startswith(entry.old + "/") is False:
                    continue
                # 이 단계의 다른(또는 같은) 줄이 옮겨 둔 새 자리 안이면 옛 파일이 아니다(World → World/World 처럼 새 자리가 옛 자리 안).
                if any(path.startswith(newDir + "/") for newDir in listNewDir):
                    continue
                newPath = entry.new + path[len(entry.old):]
                mapFile[path] = newPath
                listPending.append((path, newPath))
            for path in listGameFramework:
                if path.startswith(entry.new + "/"):
                    mapFile.setdefault(entry.old + path[len(entry.new):], path)
            continue
        setStem = set(entry.listStem)
        for folder, isNew in ((entry.folder, False), (entry.newFolder, True)):
            for path in listGameFramework:
                parent, _, name = path.rpartition("/")
                if parent != folder or name.split(".")[0] not in setStem:
                    continue
                oldPath = f"{entry.folder}/{name}"
                newPath = f"{entry.newFolder}/{name}"
                mapFile[oldPath] = newPath
                if isNew is False:
                    listPending.append((oldPath, newPath))
    return mapFile, mapDir, listPending


def addStemMapInternal(mapFile: dict[str, str]) -> dict[str, str]:
    """확장자 없이 적은 경로(`…/Foo` — 문서의 `.h`/`.cpp` 짝)도 바꿀 수 있게 이름 지도를 더합니다. 짝의 새 자리가 다르면 넣지 않습니다."""
    mapStem: dict[str, str] = {}
    setConflict: set[str] = set()
    for oldPath, newPath in mapFile.items():
        if oldPath.endswith(_kSourceSuffixes) is False:
            continue
        oldStem = oldPath.rsplit(".", 1)[0]
        newStem = newPath.rsplit(".", 1)[0]
        if mapStem.get(oldStem, newStem) != newStem:
            setConflict.add(oldStem)
        mapStem[oldStem] = newStem
    for stem in setConflict:
        mapStem.pop(stem, None)
    return mapStem


def mapTokenInternal(relative: str, mapFile: dict[str, str], mapStem: dict[str, str], mapDir: dict[str, str]) -> str:
    """`GameFramework/` 뒤 경로 하나를 새 자리로. 끝의 `.` · `/` 는 떼고 보고 다시 붙입니다."""
    core = relative.rstrip("./")
    tail = relative[len(core):]
    if core in mapFile:
        return mapFile[core] + tail
    if core in mapStem:
        return mapStem[core] + tail
    oldBest = ""
    for oldDir in mapDir:
        if (core == oldDir or core.startswith(oldDir + "/")) and len(oldDir) > len(oldBest):
            oldBest = oldDir
    if oldBest == "":
        return relative
    # 이미 새 자리면 그대로 둔다(World → World/World: `World/World/x` 를 또 바꾸지 않는다).
    for newDir in mapDir.values():
        if len(newDir) > len(oldBest) and (core == newDir or core.startswith(newDir + "/")):
            return relative
    return mapDir[oldBest] + core[len(oldBest):] + tail


def moveFilesInternal(root: Path, listPending: list[tuple[str, str]], bDryRun: bool) -> None:
    mapByTarget: dict[str, list[str]] = {}
    for oldPath, newPath in listPending:
        mapByTarget.setdefault(newPath.rpartition("/")[0], []).append(f"{_kGameFrameworkRoot}/{oldPath}")
    for targetFolder, listSource in sorted(mapByTarget.items()):
        if bDryRun:
            continue
        (root / _kGameFrameworkRoot / targetFolder).mkdir(parents=True, exist_ok=True)
        for index in range(0, len(listSource), 100):
            result = runGit(["mv", *listSource[index:index + 100], f"{_kGameFrameworkRoot}/{targetFolder}/"], cwd=root)
            if result.returnCode != 0:
                raise SystemExit(f"git mv 실패({targetFolder}): {result.stderr}")
    if bDryRun:
        return
    # 비운 옛 폴더를 지운다(깊은 쪽부터) — 빈 폴더가 남으면 게이트가 모르는 기반 폴더로 본다.
    for folder in sorted((root / _kGameFrameworkRoot).rglob("*"), key=lambda path: len(path.parts), reverse=True):
        if folder.is_dir() and any(folder.iterdir()) is False:
            folder.rmdir()


def rewriteTextInternal(root: Path, listTracked: list[str], mapFile: dict[str, str], mapDir: dict[str, str], bDryRun: bool) -> list[str]:
    mapStem = addStemMapInternal(mapFile)
    selfPath = Path(__file__).resolve()
    listChanged: list[str] = []
    for relative in listTracked:
        path = root / relative
        if path.suffix.lower() not in _kTextSuffixes or path.resolve() == selfPath or path.is_file() is False:
            continue
        raw = path.read_bytes()
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            continue
        if "GameFramework/" not in text:
            continue

        def replaceInternal(match: re.Match[str]) -> str:
            token = match.group(0)
            return "GameFramework/" + mapTokenInternal(token[len("GameFramework/"):], mapFile, mapStem, mapDir)

        newText = _kTokenRe.sub(replaceInternal, text)
        if newText == text:
            continue
        listChanged.append(relative)
        if bDryRun is False:
            path.write_bytes(newText.encode("utf-8"))
    return listChanged


_kMarkdownLinkRe = re.compile(r"\]\(([^)\s#]+)((?:#[^)\s]*)?)\)")


def rewriteMarkdownLinksInternal(root: Path, listTracked: list[str], mapFile: dict[str, str], mapDir: dict[str, str], bDryRun: bool) -> list[str]:
    """
    문서의 상대 링크(`](../x/README.md)`) 가운데 지금 깨진 것을 고칩니다. 링크를 옛 자리(문서가 옮겨졌으면 문서의 옛 자리)에서 풀어
    옮긴 표로 새 자리를 구하고, 그 자리가 있으면 문서의 지금 자리에서 다시 상대 경로로 적습니다. 살아 있는 링크는 건드리지 않습니다.
    """
    prefix = _kGameFrameworkRoot + "/"
    mapReverse = {prefix + newPath: prefix + oldPath for oldPath, newPath in mapFile.items()}

    def mapRepoInternal(repoPath: str) -> str:
        if repoPath.startswith(prefix) is False:
            return repoPath
        relative = repoPath[len(prefix):]
        return prefix + mapTokenInternal(relative, mapFile, {}, mapDir)

    listChanged: list[str] = []
    for relative in listTracked:
        if relative.endswith(".md") is False or (root / relative).is_file() is False:
            continue
        text = (root / relative).read_bytes().decode("utf-8")
        folder = PurePosixPath(relative).parent
        oldFolder = PurePosixPath(mapReverse.get(relative, relative)).parent

        def replaceInternal(match: re.Match[str]) -> str:
            target = match.group(1)
            if "://" in target or target.startswith(("/", "mailto:")):
                return match.group(0)
            if (root / folder / target).exists():
                return match.group(0)
            oldTarget = posixpath.normpath(str(oldFolder / target))
            newTarget = mapRepoInternal(oldTarget)
            if (root / newTarget).exists() is False:
                return match.group(0)
            newLink = posixpath.relpath(newTarget, str(folder))
            if target.endswith("/") and newLink.endswith("/") is False:
                newLink += "/"
            return f"]({newLink}{match.group(2)})"

        newText = _kMarkdownLinkRe.sub(replaceInternal, text)
        if newText == text:
            continue
        listChanged.append(relative)
        if bDryRun is False:
            (root / relative).write_bytes(newText.encode("utf-8"))
    return listChanged


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="GameFramework 폴더 재배치 — 이동 표와 경로 치환")
    parser.add_argument("--step", type=int, required=True, choices=sorted(_kSteps))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--root", type=Path, default=None)
    args = parser.parse_args(argv)
    root = (args.root or getProjectRoot()).resolve()

    listEntry = _kSteps[args.step]
    listTracked = listTrackedInternal(root)
    mapFile, mapDir, listPending = buildMapInternal(listEntry, set(listTracked))
    moveFilesInternal(root, listPending, args.dry_run)
    listTracked = listTrackedInternal(root) if args.dry_run is False else listTracked
    listChanged = rewriteTextInternal(root, listTracked, mapFile, mapDir, args.dry_run)
    listChanged += [path for path in rewriteMarkdownLinksInternal(root, listTracked, mapFile, mapDir, args.dry_run) if path not in listChanged]
    # 경로가 바뀌면 include 정렬 순서도 바뀐다 — 고친 C++ 파일을 정렬 규칙대로 다시 쓴다(게이트와 같은 규칙).
    listChangedSource = [path for path in listChanged if path.endswith(_kSourceSuffixes)]
    if args.dry_run is False and listChangedSource:
        fixer = root / "Scripts/lint/fixer/FormatIncludeOrder.py"
        for index in range(0, len(listChangedSource), 200):
            runProcess([sys.executable, fixer, "--files", *listChangedSource[index:index + 200]], cwd=root)
    print(f"[MoveGameFrameworkLayout] {args.step} 단계: 옮긴 파일 {len(listPending)} · 경로를 고친 파일 {len(listChanged)}")
    for relative in listChanged:
        print(f"  {relative}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
