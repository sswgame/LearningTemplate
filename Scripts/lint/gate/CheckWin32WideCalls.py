#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Win32 의 문자 집합 일반 이름(`DefWindowProc` · `LoadCursor` · `CreateFile` …)과 `A` 판을 부르는 곳을 잡는다 — `W` 판을 이름으로 부른다.

Win32 는 문자열을 받는 함수마다 `xxxA`(ANSI 코드 페이지) · `xxxW`(UTF-16) 두 벌을 두고, 일반 이름 `xxx` 는 `UNICODE` 정의에
따라 둘 중 하나로 바뀌는 **매크로**다. 이 저장소는 `UNICODE` 를 정의하지 않으므로 일반 이름은 늘 `A` 다. 창 클래스 ·
창은 `RegisterClassExW` · `CreateWindowExW` 로 만들었는데 프로시저 끝이 `DefWindowProc`(= `DefWindowProcA`)이면,
`WM_NCCREATE` · `WM_SETTEXT` 의 UTF-16 제목을 ANSI 로 읽어 첫 글자에서 끊는다(창 제목이 "S" 한 글자).

문자열은 `utf8` 로 들고 경계에서 UTF-16 으로 바꾸므로 TCHAR 전환으로 얻는 것이 없다 — 일반 이름은 부르지 않고 `W` 를 쓴다.
`A` 판을 이름으로 부르는 것도 막는다: `A` 판은 문자열을 ANSI 코드 페이지로 읽어, UTF-8 경로(한글 사용자 폴더 · 설치 경로)를 깨뜨린다.
예외는 `Scripts/lint/rules/CheckWin32WideCalls.toml` 의 `[exemption]`(디버거 출력처럼 깨져도 동작이 바뀌지 않는 `A` 판 이름)뿐이다.
`->GetMessage(` · `.GetMessage(` 처럼 멤버로 부르는 같은 이름(COM 인터페이스 메서드)은 보지 않는다. 주석 · 문자열 안의 언급도 보지 않는다.
일반 이름 표는 닫혀 있지 않다 — 저장소가 부르는 `XxxW(` 의 `Xxx` 가 표에 없으면 그것도 위반이다(새 API 가 들어올 때 표가 같이 자란다).

  python Scripts/lint/gate/CheckWin32WideCalls.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankCommentsAndLiterals, kLintTargetRelDirs, normalizePath  # noqa: E402
from common.RuleData import kKindTextList  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kListScanRoot = kLintTargetRelDirs
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")

#: `UNICODE` 에 따라 `A` · `W` 로 바뀌는 Win32 일반 이름(`rules/CheckWin32WideCalls.toml` — 새로 쓰는 API 가 없으면 거기 더한다).
_kRuleSchema = {"generic_name": kKindTextList}
_kListGenericName: tuple[str, ...] = LintGate.readRules("CheckWin32WideCalls", _kRuleSchema, requiredKeys=("generic_name",))["generic_name"]

_kSetGenericName = frozenset(_kListGenericName)

#: 저장소가 부르는 `W` 판 — 일반 이름이 위 표에 없으면 위반이다(표가 닫혀 있으면 새 API 의 일반 이름 · A 판 호출이 빠진다).
_kWideCallRe = re.compile(r"(?<![\w$>.:])([A-Z][A-Za-z0-9]+)W\s*\(")

#: 일반 이름 또는 그 `A` 판을 함수처럼 부르는 자리입니다. 더 긴 이름의 일부(`DefWindowProcW` · `myGetMessage`)는 빠집니다.
_kGenericCallRe = re.compile(r"(?<![\w$])(" + "|".join(re.escape(name) for name in _kListGenericName) + r")(A?)\s*\(")


def findGenericWin32Calls(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """Win32 일반 이름 · `A` 판을 부르는 줄을 위반 문자열로 돌려줍니다."""
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=_kSuffixes)
    listViolation: list[str] = []
    gate = CheckWin32WideCallsGate
    for path, text in LintGate.readFiles(listPath):
        if _kGenericCallRe.search(text) is None and _kWideCallRe.search(text) is None:
            continue
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        listOriginalLine = text.splitlines()
        for lineIndex, line in enumerate(blankCommentsAndLiterals(text).splitlines(), start=1):
            for match in _kWideCallRe.finditer(line):
                baseName = match.group(1)
                if baseName in _kSetGenericName:
                    continue
                if baseName + "W" in gate.mapExemption:
                    gate.useExemption(baseName + "W")
                    continue
                listViolation.append(f"{relative}:{lineIndex}: {baseName}W 의 일반 이름 '{baseName}' 이 rules/CheckWin32WideCalls.toml 의 generic_name 에 없습니다 — 거기 더합니다"
                                     f"  | {listOriginalLine[lineIndex - 1].strip()}")
            for match in _kGenericCallRe.finditer(line):
                # 멤버 호출(`queue->GetMessage(` · `x.GetObject(`)은 COM · 클래스 메서드다 — 매크로가 바꿔도 선언과 함께 바뀐다.
                prefix = line[:match.start()].rstrip()
                if prefix.endswith("->") or prefix.endswith("."):
                    continue
                name = match.group(1)
                bAnsi = match.group(2) == "A"
                if bAnsi and name + "A" in CheckWin32WideCallsGate.mapExemption:
                    CheckWin32WideCallsGate.useExemption(name + "A")
                    continue
                originalLine = listOriginalLine[lineIndex - 1].strip()
                called = name + match.group(2)
                listViolation.append(f"{relative}:{lineIndex}: {called} -> {name}W  | {originalLine}")
    return listViolation


class CheckWin32WideCallsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    ruleSchema = _kRuleSchema

    description = "Win32 API 를 문자 집합 일반 이름(A/W 매크로) · A 판이 아니라 W 판 이름으로 부르는지 검사"
    buildComment = "Checking that Win32 calls name the W variant..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    violationHeader = "Win32 문자 집합 일반 이름 · A 판 호출"
    hint = (
        "  이 저장소는 UNICODE 를 정의하지 않습니다 — 일반 이름(DefWindowProc · LoadCursor · CreateFile …)은 ANSI(A) 판입니다.\n"
        "  W 판을 이름으로 부르십시오(DefWindowProcW · LoadCursorW · CreateFileW). 문자열은 StringUtil::utf8ToUtf16 으로 넘깁니다.\n"
        "  TCHAR 자원 매크로(IDC_ARROW · IDI_APPLICATION)는 A 판 포인터 모양의 정수 id 라 W 판에는 reinterpret_cast<LPCWSTR>( IDC_ARROW ) 로 넘깁니다.\n"
        "  A 판(LoadLibraryExA · CreateFileA …)은 UTF-8 경로를 ANSI 로 읽어 한글 경로에서 실패합니다 — W 판 + StringUtil::utf8ToUtf16 으로 넘깁니다."
    )
    selfTestCases = [
        {
            "name": "일반 이름 표에 없는 W 판을 부른다(표가 닫혀 있으면 그 A 판 · 일반 이름을 못 잡는다)",
            "files": {"Source/Probe/ProbeWide.cpp": "void probe()\n{\n    ProbeThingW( L\"x\" );\n}\n"},
        },
        {
            "name": "창 프로시저가 DefWindowProc 로 끝난다",
            "files": {
                "Source/Probe/ProbeWindow.cpp": (
                    "LRESULT CALLBACK probeProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )\n"
                    "{\n"
                    "    return DefWindowProc( hWnd, msg, wParam, lParam );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "전역 한정자를 붙인 ::LoadCursor(Test 폴더)",
            "files": {"Test/Probe/ProbeCursor.cpp": "void probe() { HCURSOR hCursor = ::LoadCursor( nullptr, IDC_ARROW ); }\n"},
        },
        {
            "name": "UTF-8 경로를 A 판에 넘긴다(LoadLibraryExA)",
            "files": {
                "Source/Probe/ProbeLoad.cpp": (
                    "void* probe( const sw::string& path ) { return LoadLibraryExA( path.c_str(), nullptr, 0 ); }\n"
                ),
            },
        },
        {
            "name": "비교 연산자 뒤의 GetModuleHandle(ReflectionParser)",
            "files": {"Tools/ReflectionParser/ProbeModule.cpp": "bool probe( void* p ) { return p > GetModuleHandle( nullptr ); }\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 린트 대상 뿌리 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = findGenericWin32Calls(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary=f"{' · '.join(_kListScanRoot)} 의 Win32 일반 이름(A/W 매크로) · A 판 호출")


main = CheckWin32WideCallsGate.run


if __name__ == "__main__":
    sys.exit(main())
