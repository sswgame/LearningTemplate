#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckTextFilesAreText.py

소스·문서·스크립트 안에 **널 바이트(0x00)가 박히는 것**을 막습니다.

[왜 필요한가 — 한 바이트가 파일 하나를 리뷰 불가로 만든다]
2026-09-19 에 `Source/Core/String/StringBuilder.h` 의 주석 안에서 진짜 널 바이트 하나가 나왔다.
쓰려던 것은 이것이었다:

    `[0] = '\\0'` 로 널 종단을 세우고 …

두 글자 이스케이프여야 할 `\\0` 이 어느 세션의 heredoc 을 지나며 **0x00 한 바이트로** 바뀌어
그대로 커밋됐다. 컴파일은 통과한다(주석이니까). 그런데 git 은 blob 앞부분에 널이 있으면 그 파일을
**바이너리로 판정**하므로, 그 순간부터

  - `git diff` · `git log -p` 가 `Bin 12872 -> 14252 bytes` 만 보여 준다 — 리뷰에서 무엇이
    바뀌었는지 **볼 수 없다.**
  - `grep` 이 `Binary file ... matches` 만 찍고 줄을 안 보여 준다.
  - 줄 끝 정규화(`core.autocrlf`)가 그 파일을 건너뛴다.

즉 "틀린 코드" 가 아니라 **"코드를 볼 수 없게 만드는 것"** 이고, 그래서 아무도 눈치채지 못한 채
오래 남아 있었다. 이 저장소는 heredoc 이 이스케이프를 먹는 함정에 이미 여러 번 걸렸으므로
(AGENTS 의 같은 경고 참조) 사람의 주의가 아니라 게이트가 맡는다.

[무엇을 잡는가]
검사 대상 확장자의 파일에 0x00 이 **한 바이트라도** 있으면 위반이다. 진짜 바이너리(이미지·폰트·
바이너리 에셋)는 확장자로 걸러 내므로 걸리지 않는다.

[고치는 법]
그 자리는 거의 언제나 **이스케이프가 먹힌 자리**다. `'\\0'` · `"\\0"` 처럼 원래 쓰려던 두 글자로
되돌린다. 파일을 쓸 때 셸 heredoc 대신 편집 도구를 쓰면 애초에 생기지 않는다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # Scripts/lint — LintGate

from LintGate import GateResult, LintGate  # noqa: E402

#: 텍스트로 다뤄야 하는 확장자. 여기 없는 것(이미지·폰트·바이너리 에셋)은 검사하지 않는다.
kTextSuffix = (
    ".h",
    ".hpp",
    ".c",
    ".cpp",
    ".cc",
    ".inl",
    ".xxx",
    ".py",
    ".md",
    ".txt",
    ".cmake",
    ".json",
    ".xml",
    ".yml",
    ".yaml",
    ".hlsl",
    ".hlsli",
    ".glsl",
    ".sh",
    ".bat",
    ".ps1",
)

#: 검사에서 빼는 경로 조각 — 남의 코드이거나 생성물이다.
kExcludedPart = ("build", "generated", ".git", "__pycache__", ".venv", "ThirdParty", "Tools", "vcpkg")


def describeNulLocationInternal(data: bytes, index: int) -> tuple[int, str]:
    """@brief 널 바이트가 몇 번째 줄인지와 그 줄의 앞부분을 사람이 읽을 수 있게 만듭니다."""
    lineNumber = data.count(b"\n", 0, index) + 1
    lineStart = data.rfind(b"\n", 0, index) + 1
    lineEnd = data.find(b"\n", index)
    if lineEnd < 0:
        lineEnd = len(data)

    rawLine = data[lineStart:lineEnd].replace(b"\x00", b"<NUL>")
    return lineNumber, rawLine.decode("utf-8", errors="replace").strip()[:80]


def findNulBytesInternal(path: Path, repositoryRoot: Path) -> list[str]:
    """@brief 파일 하나에서 널 바이트를 찾습니다."""
    try:
        data = path.read_bytes()
    except OSError:
        return []

    if b"\x00" not in data:
        return []

    # 식을 먼저 변수로 뽑는다 — f-string **식** 안의 백슬래시는 CI 러너의 Python 3.10 에서
    # SyntaxError 다(CheckPythonMinimumVersion 이 그것을 잡는다. 실제로 이 줄에서 잡혔다).
    nulCount = data.count(b"\x00")
    relativePath = path.relative_to(repositoryRoot).as_posix()
    lineNumber, lineText = describeNulLocationInternal(data, data.find(b"\x00"))
    return [
        f"{relativePath}:{lineNumber} -> 널 바이트(0x00) {nulCount}개 — "
        f"git 이 이 파일을 바이너리로 보아 diff 를 볼 수 없습니다: {lineText}"
    ]


class CheckTextFilesAreTextGate(LintGate):
    """소스·문서 안의 널 바이트 — 한 바이트가 파일 하나를 리뷰 불가로 만든다."""

    description = "텍스트 파일에 널 바이트(0x00)가 박혀 있지 않은지 검사"
    buildComment = "Checking text files contain no NUL bytes..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"*{suffix}" for suffix in kTextSuffix)
    preCommitFileArgument = "--files"
    violationHeader = "텍스트 파일 안의 널 바이트"
    hint = (
        "  그 자리는 거의 언제나 이스케이프가 먹힌 자리입니다 — `'\\0'` 처럼 원래 쓰려던 두 글자로\n"
        "  되돌리세요. 파일을 쓸 때 셸 heredoc 대신 편집 도구를 쓰면 애초에 생기지 않습니다."
    )
    selfTestCases = [
        {
            "name": "주석 안에 박힌 널 바이트",
            "files": {
                "Source/Probe/NulInComment.h": "#pragma once\n// 널 종단은 '\x00' 입니다\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="검사할 파일 (생략 시 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        if args.files:
            listPath = [Path(item).resolve() for item in args.files]
            listPath = [path for path in listPath if path.suffix in kTextSuffix and path.is_file()]
        else:
            listPath = sorted(
                path
                for path in repositoryRoot.rglob("*")
                if path.suffix in kTextSuffix
                and path.is_file()
                and not any(part in kExcludedPart for part in path.relative_to(repositoryRoot).parts)
            )

        listViolation: list[str] = []
        for path in listPath:
            listViolation.extend(findNulBytesInternal(path, repositoryRoot))

        return GateResult(
            listViolation=listViolation,
            summary=f"{len(listPath)} text files scanned for NUL bytes",
        )


main = CheckTextFilesAreTextGate.run


if __name__ == "__main__":
    sys.exit(main())
