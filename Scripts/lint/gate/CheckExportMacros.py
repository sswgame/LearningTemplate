#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""GameFramework 클래스의 내보내기 매크로를 알맞은 자리에 쓰는지 잡는다 — DLL 경계가 둘로 갈리거나 링크가 깨지는 것을 막는다.

내보내기 매크로는 헤더가 속한 DLL 마다 하나다(`RuntimeAPI/Export/GameFrameworkAPI.h`):

    Base (GameFramework.dll)          SW_GF_API
    키트 (GF_<X>.dll)                  SW_KIT_API
    서버 키트 (GF_Server_<X>.dll)       SW_KIT_SERVER_API   — 폴더 이름이 `Server`

키트 TU 는 `SW_GF_EXPORTS` 를 정의하지 않으므로 키트 헤더에 `SW_GF_API` 를 쓰면 Base 클래스가 아니라 키트 클래스가 import 로 풀려 링크가 깨진다.
서버 키트 TU 는 `SW_KIT_EXPORTS` 를 정의하지 않으므로 서버 헤더에 `SW_KIT_API` 를 쓰면 클래스가 import 로 풀리고, 반대로 서버가 include 한 공유 키트
헤더의 `SW_KIT_API` 는 import 로 남아 공유 키트 심볼을 서버 DLL 이 다시 내보내지 않는다(백로그 C-3).

순수 인터페이스(순수 가상 · `= default` · `= delete` · `using` 만 든 클래스)는 내보낼 멤버가 없다. 인라인 `= default` 생성자 · 소멸자는 그 헤더를 include 하는
`.cpp` 가 DLL 에 있을 때만 내보내져서, 키트가 파생할 때 import 로 풀려 `??0` · `??1` 링크 오류가 난다 — 매크로를 달지 않는다.

주석 · 문자열 안의 언급은 보지 않는다.

  python Scripts/lint/gate/CheckExportMacros.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate · gate

from common import findFast  # noqa: E402
from LintGate import LintGate  # noqa: E402
from SourceScan import FileFindings, SourceFile  # noqa: E402

_kRoot = "Source/GameFramework/"
_kKitRoot = _kRoot + "Kits/"
_kListScanRoot = ("Source/GameFramework",)
_kSuffixes = (".h", ".hpp", ".inl")

_kMacroRe = re.compile(r"(?<![\w$])(SW_GF_API|SW_KIT_API|SW_KIT_SERVER_API)(?![\w$])")
_kExportedClassRe = re.compile(r"\b(?:class|struct)\s+(SW_\w+_API)\s+(\w+)\s*(?:final\s*)?(?::[^;{]*)?\{")


def _expectedMacro(relative: str) -> str:
    """헤더 경로가 속한 DLL 의 내보내기 매크로 이름."""
    if relative.startswith(_kKitRoot) == False:
        return "SW_GF_API"
    return "SW_KIT_SERVER_API" if "/Server/" in relative[len(_kKitRoot):] else "SW_KIT_API"


def _isPureInterfaceBody(body: str) -> bool:
    """본문이 순수 가상 하나 이상과 `= default` · `= delete` · `using` · `typedef` 뿐이면 참(멤버 정의 `{` 가 없어야 한다)."""
    if "{" in body:
        return False
    listStatement = [re.sub(r"\[\[[^\]]*\]\]", "", statement).strip()
                     for statement in re.sub(r"\b(?:public|private|protected)\s*:", ";", body).split(";")]
    listStatement = [statement for statement in listStatement if statement]
    countPure = 0
    for statement in listStatement:
        if re.search(r"=\s*0$", statement):
            countPure += 1
        elif re.search(r"=\s*(?:default|delete)$", statement) or statement.startswith(("using ", "typedef ")):
            continue
        else:
            return False
    return countPure > 0


def _findClassBody(text: str, openIndex: int) -> str:
    depth = 1
    index = openIndex
    while index < len(text) and depth:
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
        index += 1
    return text[openIndex:index - 1]


def findExportMacroMisuse(source: SourceFile) -> list[str]:
    """DLL 에 맞지 않는 내보내기 매크로와, 매크로를 단 순수 인터페이스를 위반 문자열로 돌려줍니다."""
    relative = source.relative
    listViolation: list[str] = []
    if relative.startswith(_kRoot) == False or findFast(_kMacroRe, source.text) is None:
        return listViolation
    expected = _expectedMacro(relative)
    listOriginalLine = source.listLine
    blanked = source.masked
    for lineIndex, line in enumerate(source.listMaskedLine, start=1):
        for match in _kMacroRe.finditer(line):
            if match.group(1) != expected:
                listViolation.append(f"{relative}:{lineIndex}: {match.group(1)} -> {expected}  | {listOriginalLine[lineIndex - 1].strip()}")
    for match in _kExportedClassRe.finditer(blanked):
        if _isPureInterfaceBody(_findClassBody(blanked, match.end())):
            lineIndex = blanked.count("\n", 0, match.start()) + 1
            listViolation.append(f"{relative}:{lineIndex}: {match.group(2)} 는 순수 인터페이스 — {match.group(1)} 를 뺀다  | {listOriginalLine[lineIndex - 1].strip()}")
    return listViolation


class CheckExportMacrosGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "GameFramework 헤더가 자기 DLL 의 내보내기 매크로를 쓰는지, 순수 인터페이스에 매크로를 달지 않았는지 검사"
    buildComment = "Checking GameFramework export macros (SW_GF_API / SW_KIT_API / SW_KIT_SERVER_API)..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 내보내기 매크로 오용"
    violationHeader = "내보내기 매크로 오용"
    hint = (
        "  헤더가 속한 DLL 의 매크로를 씁니다(RuntimeAPI/Export/GameFrameworkAPI.h):\n"
        "      Base = SW_GF_API · 키트 = SW_KIT_API · 서버 키트(폴더 Server/) = SW_KIT_SERVER_API\n"
        "  순수 인터페이스(순수 가상 · = default 뿐)는 매크로를 달지 않습니다 — 인라인 생성자 · 소멸자를 DLL 이 내보내지 않으면 파생하는 쪽 링크가 깨집니다."
    )
    selfTestCases = [
        {
            "name": "키트 헤더가 SW_GF_API 를 쓴다",
            "files": {
                "Source/GameFramework/Kits/Genre/Probe/ProbeKit/ProbeKit.h": "#pragma once\nclass SW_GF_API ProbeKit\n{\npublic:\n    void run();\n};\n",
            },
        },
        {
            "name": "서버 키트 헤더가 SW_KIT_API 를 쓴다",
            "files": {
                "Source/GameFramework/Kits/Feature/Online/Probe/Server/ProbeServer.h": "#pragma once\nclass SW_KIT_API ProbeServer\n{\npublic:\n    void run();\n};\n",
            },
        },
        {
            "name": "순수 인터페이스에 SW_GF_API 를 단다",
            "files": {
                "Source/GameFramework/Base/Probe/IProbe.h": (
                    "#pragma once\n"
                    "class SW_GF_API IProbe\n"
                    "{\n"
                    "public:\n"
                    "    IProbe()          = default;\n"
                    "    virtual ~IProbe() = default;\n"
                    "    virtual void run() = 0;\n"
                    "};\n"
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source/GameFramework 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findExportMacroMisuse(source))


main = CheckExportMacrosGate.run


if __name__ == "__main__":
    sys.exit(main())
