#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
폴더마다 코드 파일 수를 세서 **너무 큰 평면 폴더**와 **파일 하나짜리 폴더**를 보고한다.

[왜 필요한가]
폴더는 한 번 커지면 아무도 나누지 않는다 — 파일이 하나씩 늘 때마다 "하나쯤" 이라 60 개짜리 평면 폴더가 생기고,
반대로 파일 하나를 위해 만든 폴더는 그 이름이 다른 폴더의 같은 이름과 겹쳐 헷갈리게 한다. 이 보고서는 둘을 숫자로 보여 준다.

[게이트가 아니다]
`Run*` 은 보고하고 `Check*` 이 막는다. 폴더를 나눌지는 내용을 보고 정한다 — 종료 코드는 늘 0 이다.
엔진 루트에 둘 수 있는 파일은 게이트(`CheckEngineRootFiles.py`)가 막는다.

세는 것: 바로 아래의 C++ 파일(`kCppAllExtensions`)과 X-macro 표(`.xxx`). 하위 폴더는 세지 않는다.
파일 하나짜리 폴더는 `Source/` 만 본다 — 시험 폴더는 소스 폴더를 따르므로 하나짜리여도 자리가 맞다.

사용법:
  py -3 Scripts/lint/report/RunFolderFileCount.py              # 상한 40
  py -3 Scripts/lint/report/RunFolderFileCount.py --max 30
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from common import collectSourceFiles, getLintSearchDirs, getProjectRoot, kCppAllExtensions, kNotOurDirNames, useUtf8Stdout  # noqa: E402

kDefaultMaxFileCount = 40

# 파일 하나짜리여도 되는 폴더 — 그 하나가 폴더의 뜻인 자리다.
#   - RHI 백엔드 모듈 진입점: 백엔드마다 `ModuleEntry.cpp` 하나가 MODULE 타깃 하나다(`sw_addRhiBackendModule`).
_kSingleFileFolderPrefix: tuple[str, ...] = (
    "Source/Engine/Graphics/RHI/Modules/",
)


def countFilesPerFolder(repositoryRoot: Path) -> Counter[str]:
    """저장소 상대 폴더 경로 → 바로 아래 코드 파일 수입니다."""
    extensions = set(kCppAllExtensions) | {".xxx"}
    listPath = collectSourceFiles(getLintSearchDirs(repositoryRoot), extensions, excludeSubdirs=kNotOurDirNames)
    return Counter(filePath.parent.relative_to(repositoryRoot).as_posix() for filePath in listPath)


def isSingleFileFolderAllowedInternal(folder: str) -> bool:
    return any(f"{folder}/".startswith(prefix) for prefix in _kSingleFileFolderPrefix)


def folderAncestorsInternal(folder: str) -> list[str]:
    """`a/b/c` → [`a`, `a/b`]. 자기 자신은 넣지 않습니다."""
    listPart = folder.split("/")
    return ["/".join(listPart[:index]) for index in range(1, len(listPart))]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="폴더당 코드 파일 수 보고")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
    parser.add_argument("--max", type=int, default=kDefaultMaxFileCount, help=f"이보다 많으면 보고합니다(기본 {kDefaultMaxFileCount})")
    args = parser.parse_args(argv)
    useUtf8Stdout()

    repositoryRoot = (args.root or getProjectRoot()).resolve()
    mapFileCount = countFilesPerFolder(repositoryRoot)

    # 하위 폴더에 코드가 있는 폴더는 묶음이다(`Kits/Network` 의 공용 헤더 하나) — 파일 하나짜리 폴더로 세지 않는다.
    setParentFolder = {parent for folder in mapFileCount for parent in folderAncestorsInternal(folder)}
    listLargeFolder = sorted(((count, folder) for folder, count in mapFileCount.items() if count > args.max), reverse=True)
    listSingleFileFolder = sorted(folder for folder, count in mapFileCount.items()
                                  if count == 1 and folder.startswith("Source/") and folder not in setParentFolder
                                  and not isSingleFileFolderAllowedInternal(folder))

    print(f"[RunFolderFileCount] 코드 파일이 {args.max} 개를 넘는 폴더 {len(listLargeFolder)}개")
    for count, folder in listLargeFolder:
        print(f"  {count:4d}  {folder}")
    print(f"[RunFolderFileCount] 코드 파일이 하나뿐인 Source 폴더 {len(listSingleFileFolder)}개 — 위 폴더로 합칠지 본다")
    for folder in listSingleFileFolder:
        print(f"        {folder}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
