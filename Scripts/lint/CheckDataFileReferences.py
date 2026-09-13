#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
X-macro 목록 파일(`*.xxx`)이 실제로 참조되는지 검사한다.

왜 필요한가:
  이 저장소는 열거형·인자·서비스 목록을 `.xxx` 파일 하나에 모아 두고 여러 번 include 해서
  전개한다. 그 파일이 **복사되어 두 곳에 있으면**, 고쳐도 아무 일이 일어나지 않는 덫이 된다.
  실제로 그런 사본이 여섯 개 있었다. 가장 나쁜 사례:

    Source/Core/CommandLine/ArgumentList.xxx   (죽은 사본 — 폴더 이름상 먼저 찾게 되는 자리)
    Source/Core/Predefined/ArgumentList.xxx    (진짜 — 코드가 include 하는 것)

  죽은 쪽은 LANGUAGE·BAKE_SHADERS 가 빠진 낡은 상태였고, 거기서 창 크기 기본값을 고쳐도
  빌드 결과는 바뀌지 않았다.

규칙:
  `Source/**` 와 `Tools/**` 의 모든 `*.xxx` 는 다음 중 하나로 참조되어야 한다.
    1) `#include "<Source 기준 경로>"`            예: "Core/Predefined/ArgumentList.xxx"
    2) 같은 디렉터리 파일의 `#include "<파일명>"`  예: Tools/ReflectionParser 안에서 "X.xxx"
    3) cmake/Scripts/Config 의 텍스트에 저장소 기준 경로가 그대로 등장
  어느 것도 없으면 죽은 파일이다.

  python Scripts/lint/CheckDataFileReferences.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot, mapConcurrent, useUtf8Stdout

# 검사 대상 확장자 — X-macro 목록 파일.
_kDataSuffix = ".xxx"

# 대상 파일을 찾는 루트 (저장소 기준).
_kScanRoots = ("Source", "Tools")

# 참조를 찾을 때 읽는 루트와 확장자.
_kReferenceRoots = ("Source", "Tools", "cmake", "Scripts", "Config")
_kReferenceSuffix = (".cpp", ".h", ".hpp", ".c", ".cmake", ".py", ".txt", ".in", ".json", ".xxx")

_kSkipDirName = frozenset({"vcpkg", "build", "__pycache__", ".git", "ThirdParty"})

# include 이 `Source/` 를 루트로 쓰는 타깃과, 자기 디렉터리를 쓰는 타깃이 둘 다 있다.
_kIncludeRootDir = "Source"

_kIncludeRe = re.compile(r'#\s*include\s*"([^"]+)"')


def collectFilesInternal(repositoryRoot: Path, roots: tuple[str, ...], suffixes: tuple[str, ...]) -> list[Path]:
    """지정한 루트 아래에서 해당 확장자 파일을 모읍니다."""
    collected: list[Path] = []
    for rootName in roots:
        rootPath = repositoryRoot / rootName
        if rootPath.is_dir() is False:
            continue
        for dirPath, dirNames, fileNames in os.walk(rootPath):
            dirNames[:] = [name for name in dirNames if name not in _kSkipDirName]
            for fileName in fileNames:
                if fileName.endswith(suffixes):
                    collected.append(Path(dirPath) / fileName)
    return collected


def main() -> int:
    useUtf8Stdout()

    parser = argparse.ArgumentParser(description="X-macro 목록 파일 참조 검사")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
    args = parser.parse_args()
    repositoryRoot = (args.root or getProjectRoot()).resolve()

    dataFiles = collectFilesInternal(repositoryRoot, _kScanRoots, (_kDataSuffix,))
    if not dataFiles:
        print("[CheckDataFileReferences] 검사할 .xxx 파일이 없습니다.")
        return 0

    # 대상 파일의 `resolve()` 와 저장소 기준 경로는 **미리 한 번만** 구한다.
    # 예전에는 참조 파일마다 안쪽 루프에서 다시 구했다 — 참조 파일 × 대상 파일만큼의 파일시스템
    # 질의가 되어, 파일이 늘수록 제곱으로 느려지는 자리였다.
    dataEntries = [(dataFile.resolve(), dataFile.relative_to(repositoryRoot).as_posix()) for dataFile in dataFiles]

    # 이 스크립트 자신은 참조로 세지 않는다. 아래 독스트링이 죽은 사본의 경로를 **예시로** 적고
    # 있어서, 그것을 참조로 인정하면 그 파일이 되살아나도 검사가 통과한다(실제로 한 번 그랬다).
    selfPath = Path(__file__).resolve()

    def scanReferenceFileInternal(referenceFile: Path) -> list[Path] | None:
        """
        참조 파일 하나가 가리키는 대상 파일들을 돌려줍니다. 읽지 못하면 None (호출부가 실패로 본다).

        @note 어느 참조 파일이 가리켰는지는 돌려주지 않는다 — 쓰이는 것은 "참조되었는가" 뿐이고,
              동시에 훑으므로 "누가 먼저 가리켰나" 는 실행마다 달라진다. 값을 남기면 그 비결정성이
              메시지로 새어 나간다.
        """
        if referenceFile.resolve() == selfPath:
            return []
        try:
            text = referenceFile.read_text(encoding="utf-8", errors="replace")
        except OSError as exception:
            print(f"[CheckDataFileReferences] 읽기 실패: {referenceFile}: {exception}", file=sys.stderr)
            return None

        referenceRelative = referenceFile.relative_to(repositoryRoot).as_posix()
        found: list[Path] = []

        # 1) / 2) include 해석
        for includePath in _kIncludeRe.findall(text):
            if includePath.endswith(_kDataSuffix) is False:
                continue
            normalizedInclude = includePath.replace("\\", "/")

            # Source/ 를 루트로 본 경로
            candidate = repositoryRoot / _kIncludeRootDir / normalizedInclude
            if candidate.is_file():
                found.append(candidate.resolve())

            # 포함하는 파일과 같은 디렉터리 기준 경로
            sibling = (referenceFile.parent / normalizedInclude).resolve()
            if sibling.is_file():
                found.append(sibling)

        # 3) 저장소 기준 경로가 텍스트에 그대로 있는 경우 (cmake/py/json 이 경로로 읽는다)
        for resolved, dataRelative in dataEntries:
            if dataRelative != referenceRelative and dataRelative in text:
                found.append(resolved)
        return found

    referenceFiles = collectFilesInternal(repositoryRoot, _kReferenceRoots, _kReferenceSuffix)
    referenced: set[Path] = set()
    for found in mapConcurrent(scanReferenceFileInternal, referenceFiles):
        if found is None:
            return 2
        referenced.update(found)

    violations = sorted(
        dataRelative for resolved, dataRelative in dataEntries if resolved not in referenced
    )

    if violations:
        print("[CheckDataFileReferences] 아무도 include 하지 않는 목록 파일:")
        for line in violations:
            print(f"  - {line}")
        print("  고쳐도 빌드 결과가 바뀌지 않는 파일입니다. 사본이면 지우고, 쓰려던 것이면 include 하세요.")
        return 1

    print(f"[CheckDataFileReferences] OK ({len(dataFiles)} data files, all referenced)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
