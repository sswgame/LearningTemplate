#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Win32 의 문자 집합 일반 이름(`DefWindowProc` · `LoadCursor` · `CreateFile` …)과 `A` 판을 부르는 곳을 잡는다 — `W` 판을 이름으로 부른다.

Win32 는 문자열을 받는 함수마다 `xxxA`(ANSI 코드 페이지) · `xxxW`(UTF-16) 두 벌을 두고, 일반 이름 `xxx` 는 `UNICODE` 정의에
따라 둘 중 하나로 바뀌는 **매크로**다. 이 저장소는 `UNICODE` 를 정의하지 않으므로 일반 이름은 늘 `A` 다. 창 클래스 ·
창은 `RegisterClassExW` · `CreateWindowExW` 로 만들었는데 프로시저 끝이 `DefWindowProc`(= `DefWindowProcA`)이면,
`WM_NCCREATE` · `WM_SETTEXT` 의 UTF-16 제목을 ANSI 로 읽어 첫 글자에서 끊는다(창 제목이 "S" 한 글자).

문자열은 `utf8` 로 들고 경계에서 UTF-16 으로 바꾸므로 TCHAR 전환으로 얻는 것이 없다 — 일반 이름은 부르지 않고 `W` 를 쓴다.
`A` 판을 이름으로 부르는 것도 막는다: `A` 판은 문자열을 ANSI 코드 페이지로 읽어, UTF-8 경로(한글 사용자 폴더 · 설치 경로)를 깨뜨린다.
예외는 `_kSetAnsiAllowed`(디버거 출력처럼 깨져도 동작이 바뀌지 않는 자리)뿐이다.
`->GetMessage(` · `.GetMessage(` 처럼 멤버로 부르는 같은 이름(COM 인터페이스 메서드)은 보지 않는다. 주석 · 문자열 안의 언급도 보지 않는다.

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
from LintGate import GateResult, LintGate  # noqa: E402

_kListScanRoot = kLintTargetRelDirs
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")

#: `UNICODE` 에 따라 `A` · `W` 로 바뀌는 Win32 일반 이름(함수 매크로)입니다. 새로 쓰는 API 가 여기 없으면 더합니다.
_kListGenericName = (
    # 창 · 메시지
    "DefWindowProc", "CallWindowProc", "RegisterClass", "RegisterClassEx", "UnregisterClass", "GetClassInfo", "GetClassInfoEx",
    "CreateWindow", "CreateWindowEx", "FindWindow", "FindWindowEx", "SetWindowText", "GetWindowText", "GetWindowTextLength",
    "GetClassName", "GetWindowLong", "SetWindowLong", "GetWindowLongPtr", "SetWindowLongPtr", "GetClassLong", "SetClassLong",
    "GetClassLongPtr", "SetClassLongPtr", "PeekMessage", "GetMessage", "DispatchMessage", "PostMessage", "SendMessage",
    "SendMessageTimeout", "SendNotifyMessage", "PostThreadMessage", "RegisterWindowMessage", "IsDialogMessage",
    "TranslateAccelerator", "SetWindowsHookEx", "SystemParametersInfo", "GetMonitorInfo", "EnumDisplaySettings",
    "EnumDisplaySettingsEx", "EnumDisplayDevices", "ChangeDisplaySettings", "ChangeDisplaySettingsEx",
    # 자원 · 대화 상자 · 글꼴
    "LoadCursor", "LoadCursorFromFile", "LoadIcon", "LoadImage", "LoadBitmap", "LoadString", "LoadMenu", "LoadAccelerators",
    "MessageBox", "MessageBoxEx", "DialogBoxParam", "CreateDialogParam", "DrawText", "DrawTextEx", "TextOut", "CreateFont",
    "CreateFontIndirect", "GetObject", "GetTextExtentPoint32", "GetTextMetrics", "CreateDC", "AddFontResource",
    "AddFontResourceEx", "RegisterClipboardFormat", "GetOpenFileName", "GetSaveFileName", "SHBrowseForFolder",
    "SHGetPathFromIDList", "SHGetFolderPath", "SHFileOperation", "ShellExecute", "ShellExecuteEx",
    # 키보드
    "MapVirtualKey", "MapVirtualKeyEx", "GetKeyNameText", "VkKeyScan", "VkKeyScanEx", "CharUpper", "CharLower",
    # 모듈 · 파일 · 프로세스
    "GetModuleHandle", "GetModuleHandleEx", "GetModuleFileName", "LoadLibrary", "LoadLibraryEx", "SetDllDirectory",
    "GetDllDirectory", "CreateFile", "CreateFileMapping", "OpenFileMapping", "DeleteFile", "CopyFile", "CopyFileEx", "MoveFile",
    "MoveFileEx", "ReplaceFile", "CreateDirectory", "RemoveDirectory", "GetFileAttributes", "GetFileAttributesEx",
    "SetFileAttributes", "FindFirstFile", "FindFirstFileEx", "FindNextFile", "GetFullPathName", "GetCurrentDirectory",
    "SetCurrentDirectory", "GetTempPath", "GetTempFileName", "GetLongPathName", "GetShortPathName", "GetFinalPathNameByHandle",
    "CreateHardLink", "CreateSymbolicLink", "GetDriveType", "GetVolumeInformation", "GetDiskFreeSpace", "GetDiskFreeSpaceEx",
    "FindFirstChangeNotification", "SearchPath", "GetSystemDirectory", "GetWindowsDirectory", "GetEnvironmentVariable",
    "SetEnvironmentVariable", "ExpandEnvironmentStrings", "GetEnvironmentStrings", "FreeEnvironmentStrings",
    "CreateProcess", "GetCommandLine", "GetStartupInfo", "CreateNamedPipe", "WaitNamedPipe", "CallNamedPipe",
    "Process32First", "Process32Next", "Module32First", "Module32Next",
    # 동기화 객체 · 진단 · 시스템
    "CreateEvent", "CreateEventEx", "OpenEvent", "CreateMutex", "CreateMutexEx", "OpenMutex", "CreateSemaphore",
    "CreateSemaphoreEx", "OpenSemaphore", "CreateWaitableTimer", "CreateWaitableTimerEx", "OutputDebugString", "FormatMessage",
    "GetComputerName", "GetUserName", "GetVersionEx", "RegOpenKeyEx", "RegCreateKeyEx", "RegQueryValueEx", "RegSetValueEx",
    "RegDeleteKey", "RegDeleteValue", "RegEnumKeyEx", "RegEnumValue", "RegGetValue",
    # 콘솔
    "SetConsoleTitle", "GetConsoleTitle", "WriteConsole", "ReadConsole", "ReadConsoleInput", "PeekConsoleInput",
    "FillConsoleOutputCharacter", "WriteConsoleOutput", "WriteConsoleOutputCharacter",
)

#: `A` 판을 이름으로 불러도 되는 일반 이름입니다. 글이 깨져도 동작이 바뀌지 않는 자리만 둡니다.
_kSetAnsiAllowed = frozenset({
    "OutputDebugString",  # 디버거 출력 창에 보내는 로그 한 줄 — 로그 문자열은 영어이고, 깨져도 아무것도 실패하지 않는다
})

#: 일반 이름 또는 그 `A` 판을 함수처럼 부르는 자리입니다. 더 긴 이름의 일부(`DefWindowProcW` · `myGetMessage`)는 빠집니다.
_kGenericCallRe = re.compile(r"(?<![\w$])(" + "|".join(re.escape(name) for name in _kListGenericName) + r")(A?)\s*\(")


def findGenericWin32Calls(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """Win32 일반 이름 · `A` 판을 부르는 줄을 위반 문자열로 돌려줍니다."""
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=_kSuffixes)
    listViolation: list[str] = []
    for path, text in LintGate.readFiles(listPath):
        if _kGenericCallRe.search(text) is None:
            continue
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        listOriginalLine = text.splitlines()
        for lineIndex, line in enumerate(blankCommentsAndLiterals(text).splitlines(), start=1):
            for match in _kGenericCallRe.finditer(line):
                # 멤버 호출(`queue->GetMessage(` · `x.GetObject(`)은 COM · 클래스 메서드다 — 매크로가 바꿔도 선언과 함께 바뀐다.
                prefix = line[:match.start()].rstrip()
                if prefix.endswith("->") or prefix.endswith("."):
                    continue
                name = match.group(1)
                bAnsi = match.group(2) == "A"
                if bAnsi and name in _kSetAnsiAllowed:
                    continue
                originalLine = listOriginalLine[lineIndex - 1].strip()
                called = name + match.group(2)
                listViolation.append(f"{relative}:{lineIndex}: {called} -> {name}W  | {originalLine}")
    return listViolation


class CheckWin32WideCallsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

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
