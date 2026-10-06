#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckExecutableBits.py

`#!` 로 시작하는 파일(셸 · 파이썬 스크립트, vcpkg 오버레이 포트의 `configure`)은 git 모드가 **100755** 여야 합니다.

[왜 필요한가]
Windows 에서 파일을 만들거나 통째로 다시 쓰면 git 은 실행 비트 없이(100644) 기록한다(`core.fileMode=false`).
Windows 빌드는 아무 일 없이 지나가고, 리눅스 체크아웃만 그 파일을 실행하지 못한다 — 오버레이 포트의
`openssl/unix/configure` 가 그래서 리눅스 vcpkg 설치를 `Permission denied` 로 세웠다(리눅스 CI 잡 전부가 Configure 에서 짐).

[무엇을 보는가]
git 저장소면 인덱스의 모드(`git ls-files -s` — 커밋 훅에서는 staged 모드), 저장소가 아니면(자체 시험 조각) 파일 시스템의 실행 비트.
심볼릭 링크 · 서브모듈은 보지 않는다.

[고치는 법]
`git update-index --chmod=+x <경로>` 후 다시 스테이지한다.
"""

from __future__ import annotations

import argparse
import os
import stat
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # Scripts/lint — LintGate

from common import kNotOurDirNames, runProcess  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

kExecutableMode = "100755"
kRegularModes = ("100644", "100755")


def readGitModesInternal(repositoryRoot: Path) -> dict[str, str] | None:
    """저장소 루트가 git 작업 트리의 루트면 {상대 경로: 인덱스 모드}, 아니면 None(파일 시스템 비트로 본다)."""
    topLevel = runProcess(["git", "rev-parse", "--show-toplevel"], cwd=repositoryRoot)
    if not topLevel.bSucceeded or Path(topLevel.stdout.strip()).resolve() != repositoryRoot.resolve():
        return None
    listed = runProcess(["git", "ls-files", "-s", "-z"], cwd=repositoryRoot)
    if not listed.bSucceeded:
        return None

    mapMode: dict[str, str] = {}
    for entry in listed.stdout.split("\0"):
        if not entry:
            continue
        meta, relativePath = entry.split("\t", 1)
        mapMode[relativePath] = meta.split()[0]
    return mapMode


def readFileModeInternal(path: Path) -> str:
    """git 밖(자체 시험 조각)의 모드 — 실행 비트가 하나라도 있으면 100755."""
    return kExecutableMode if os.stat(path).st_mode & (stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH) else "100644"


def isScriptInternal(path: Path) -> bool:
    try:
        with path.open("rb") as file:
            return file.read(2) == b"#!"
    except OSError:
        return False


def isOurPathInternal(relativePath: str) -> bool:
    return not any(part in kNotOurDirNames for part in relativePath.split("/")[:-1])


class CheckExecutableBitsGate(LintGate):
    """`#!` 스크립트는 실행 비트를 들고 커밋된다 — Windows 에서 만든 파일은 리눅스에서 실행이 안 된다."""

    description = "#! 로 시작하는 파일의 git 모드가 100755 인지 검사"
    buildComment = "Checking that #! scripts are committed with the executable bit..."
    timeoutSeconds = 60
    preCommitPattern = ()
    preCommitFileArgument = "--files"
    violationHeader = "실행 비트 없이 커밋된 스크립트"
    hint = "  `git update-index --chmod=+x <경로>` 로 실행 비트를 켜세요(Windows 에서 만든 파일은 100644 로 들어간다)."
    selfTestCases = [
        {
            "name": "실행 비트 없는 오버레이 포트 configure",
            "files": {
                "ThirdParty/openssl/vcpkg-port/openssl/unix/configure": "#!/usr/bin/env bash\nset -e\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        mapMode = readGitModesInternal(repositoryRoot)
        if mapMode is None or args.files:
            listPath = self.selectTargetFiles(repositoryRoot, args.files, bAnySuffix=True)
            listRelative = [path.resolve().relative_to(repositoryRoot.resolve()).as_posix() for path in listPath]
        else:
            listRelative = [relativePath for relativePath in mapMode if isOurPathInternal(relativePath)]

        listViolation: list[str] = []
        scriptCount = 0
        for relativePath in sorted(listRelative):
            path = repositoryRoot / relativePath
            if mapMode is not None:
                mode = mapMode.get(relativePath)
                if mode not in kRegularModes:
                    continue  # 추적 안 됨 · 심볼릭 링크 · 서브모듈
            elif path.is_symlink() or not path.is_file():
                continue
            else:
                mode = readFileModeInternal(path)
            if not isScriptInternal(path):
                continue
            scriptCount += 1
            if mode != kExecutableMode:
                listViolation.append(f"{relativePath} -> 모드 {mode}, #! 스크립트는 {kExecutableMode}")

        return GateResult(listViolation=listViolation, summary=f"{scriptCount} #! scripts checked for the executable bit")


main = CheckExecutableBitsGate.run


if __name__ == "__main__":
    sys.exit(main())
