#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Core · Engine 에 이미 있는 기능을 두고 C 런타임 · 표준 라이브러리를 직접 부르는 곳을 센다 — 새로 늘지 않게 막는다.

무엇을 대신 쓰는지는 `docs/agents/ExistingFacilities.md` 의 표가 정본이다. 규칙 하나 = `_kListRule` 한 줄이고, 줄마다
**왜 형식(include 경계 · `[[deprecated]]` · 래퍼 헤더)으로 막지 못하는지**를 적는다. 그 이유를 못 쓰는 규칙은 넣지 않는다.

    stdout        printf · fprintf · puts · fputs · std::cout · std::cerr   →  SW_LOG_* (로그 티어 아래는 SW_LOW_LEVEL_*)
    random        rand · srand · std::mt19937 · std::random_device           →  MathUtil::getRandom* · XorShift32 · GameRandom
    fileOpen      fopen · _wfopen · std::ifstream · std::ofstream · fstream  →  FileUtil::read*/write* · ResourceIO · AsyncFileIO
    fileMutate    std::rename · ::unlink · ::mkdir · MoveFile · DeleteFile · open(O_WRONLY…) · PlatformFileUtil::openFile/replaceFile/moveToRecycleBin
                  →  FileUtil::writeFile · appendTextFile · copyFile · removeFile · removeDirectory · moveToRecycleBin(쓰기 게이트 `FileUtil::isWriteAllowed` 를 지난다)
    libraryWrite  DirectX::SaveTo*File · sqlite3_open · ImGui::SaveIniSettingsToDisk · IniFilename =  →  쓰기 전에 `FileUtil::isWriteAllowed` 를 직접 묻는다
    processSpawn  system · popen · CreateProcess · ShellExecute · exec* · fork  →  sw::Process(execute · launch · launchDetached)
    numberParse   atoi · strtol · strtod · std::stoi · std::from_chars · sscanf …               →  StringUtil::parseInt · parseInt64 · parseFloat · tryParseBool …

이미 따로 막는 것은 여기 넣지 않는다: 시계(`CheckClockReads`) · 환경 변수(`CheckProcessEnvironment`) · 스레드(`CheckStdThread`) ·
맨 할당(`CheckRawAllocation`) · `std::filesystem`(`CheckStdFilesystemIsolation`) · 서드파티 헤더(`CheckThirdPartyIsolation`).

판정: 주석 · 문자열은 가린 글에서 본다. 멤버 호출(`.printf(` · `->system(`)과 이름의 일부(`snprintf` · `getSystem(`)는 보지 않는다.
허용 목록은 `Scripts/lint/rules/CheckFacilityBypass.toml` 의 `[exemption]` — 키는 `<규칙>:<경로 글롭>`, 그 기능을 구현하는 파일 자신만 둔다.
그 밖의 기존 위반은 `[baseline]` 의 규칙별 건수로 센다(옮긴 이력과 남긴 자리의 이유는 결정 기록 5-3). 커밋 훅은 추가한 줄만,
전체 훑기는 기준선을 넘을 때만 실패한다 — 두 모드는 기반(`LintGate.BaselineSourceGate`)이 정한다.

  python Scripts/lint/gate/CheckFacilityBypass.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--list]
"""

from __future__ import annotations

import fnmatch
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import findFast  # noqa: E402
from common.Constants import kLintTargetRelDirs  # noqa: E402
from lint.LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")
#: 자유 함수 호출의 앞 — 이름의 일부 · 멤버 호출 · `::` 로 이어지는 다른 이름공간을 뺀다(`std::` 는 패턴이 따로 받는다).
_kCallHead = r"(?<![\w.>:])(?:std::)?"


@dataclass(frozen=True)
class BypassRule:
    """직접 호출 규칙 하나."""

    key: str            #: `[baseline]` 의 키 · 예외 키의 앞부분
    pattern: re.Pattern
    instead: str        #: 위반 줄에 적을 대체 API
    whyNotType: str     #: 형식으로 막지 못하는 이유(문서 · 리뷰용 — 이유를 못 쓰면 규칙을 넣지 않는다)


_kListRule: tuple[BypassRule, ...] = (
    BypassRule("stdout",
               re.compile(_kCallHead + r"(?:printf|fprintf|vprintf|vfprintf|puts|fputs|putchar|wprintf|fwprintf)\s*\(|\bstd::(?:w?cout|w?cerr|w?clog)\b"),
               "SW_LOG_* (로그 티어 아래는 SW_LOW_LEVEL_ERROR, 명령줄 결과 글은 StandardOutput::write, 테스트 진단은 test::printTestNote)",
               "`<cstdio>` 는 pch(Core/Common/StdHeaders.h)로 모든 TU 에 보이고 snprintf 와 한 헤더라 숨길 수 없다. [[deprecated]] 재선언은 UCRT 인라인 정의와 "
               "`std::` 호출에 듣지 않는다(결정 기록 5-3). std 객체(cout) 재선언은 미정의 동작"),
    BypassRule("random",
               re.compile(_kCallHead + r"s?rand\s*\(|\bstd::(?:mt19937(?:_64)?|random_device|default_random_engine|minstd_rand0?|ranlux\w+|knuth_b)\b"),
               "MathUtil::getRandom · getRandomRange (결정적 수열은 XorShift32 · HashUtil::splitMix64Next, 게임 규칙은 GameRandom)",
               "`<random>` 은 표준 헤더라 어느 타깃이든 include 할 수 있어 레이어 경계로 못 막고, rand 는 pch 의 `<cstdlib>` 에 있다"),
    BypassRule("fileOpen",
               re.compile(_kCallHead + r"(?:fopen|fopen_s|_wfopen|_wfopen_s|freopen|_fsopen|_wfsopen)\s*\(|\bstd::(?:basic_|w)?[io]?fstream\b"),
               "FileUtil::readFile · readTextFile · writeFile · writeTextFile (크기 0 의사 파일은 readStreamTextFile, 리소스는 ResourceIO, 비동기는 AsyncFileIO)",
               "fopen 은 pch 의 `<cstdio>` 에, fstream 은 표준 헤더에 있어 숨길 수 없다(FileUtil 구현이 같은 이름을 써야 한다)"),
    BypassRule("fileMutate",
               re.compile(r"(?<![\w.>])(?:std::|::)(?:rename|unlink|rmdir|mkdir|chmod|chown|truncate|symlink)\s*\("
                          r"|(?<![\w.>])(?:std::|::)remove\s*\((?:[^,()]|\([^()]*\))*\)"   # 인자 하나짜리 파일 삭제만 — std::remove(first, last, v) 알고리즘은 뺀다
                          r"|\b(?:_wrename|_wremove|_unlink|_wunlink|_rmdir|_wrmdir|_mkdir|_wmkdir|_chmod|_wchmod|_chsize_s|mkstemp|ftruncate|futimens|utimes|renameat|openat|open64|_wopen|_sopen_s|_wsopen_s)\s*\("
                          r"|(?<![\w.>:])(?:CreateDirectory(?:Ex)?[AW]?|DeleteFile[AW]?|MoveFile(?:Ex|Transacted)?[AW]?|CopyFile(?:Ex|2)?[AW]?|ReplaceFile[AW]?|RemoveDirectory[AW]?"
                          r"|SHFileOperation[AW]?|SetFileAttributes[AW]?|SetEndOfFile|SetFileTime|CreateHardLink[AW]?|CreateSymbolicLink[AW]?|WriteFile(?:Ex)?)\s*\("
                          r"|(?<![\w.>:])CreateFile[AW]?\s*\([^;]*\b(?:GENERIC_WRITE|GENERIC_ALL|FILE_APPEND_DATA|DELETE)\b"
                          r"|(?<![\w.>])(?:::)?open\s*\([^;]*\bO_(?:WRONLY|RDWR|CREAT|TRUNC|APPEND)\b"
                          r"|\bPlatformFileUtil::(?:openFile|replaceFile|moveToRecycleBin)\b"),
               "FileUtil::writeFile · appendTextFile · copyFile · removeFile · removeDirectory · moveToRecycleBin (자동화 쓰기 게이트 FileUtil::isWriteAllowed 를 지난다)",
               "`<cstdio>` · `<unistd.h>` · `<windows.h>` 는 pch 와 플랫폼 TU 가 다른 이유로 include 해야 해서 숨길 수 없고, `PlatformFileUtil` 은 읽기 연산과 한 구조체라 쓰기만 따로 감추면 "
               "FileUtil · 로그 싱크가 friend 로 풀어야 한다. 게이트 없이 파일을 바꾸면 자동화 실행이 리소스 원본을 바꿀 수 있다"),
    BypassRule("libraryWrite",
               re.compile(r"\bDirectX::SaveTo\w+File\b|\bsqlite3_open(?:_v2|16)?\s*\(|\bImGui::(?:SaveIniSettingsToDisk|LogToFile)\b|\bstbi_write_\w+\s*\(|\bIniFilename\s*=[^=]"),
               "쓰기 전에 FileUtil::isWriteAllowed( 경로 ) 를 묻는다(서드파티가 경로를 스스로 열어 쓰므로 FileUtil 입구를 지나지 않는다)",
               "서드파티 헤더는 CheckThirdPartyIsolation 이 이미 한 폴더로 모았지만, 그 함수들은 경로 글을 받아 스스로 열어서 FileUtil 입구 형식으로 감쌀 수 없다"),
    BypassRule("processSpawn",
               re.compile(_kCallHead + r"(?:system|_wsystem|popen|_popen|_wpopen)\s*\(|(?<![\w.>:])(?:CreateProcess[AW]?|ShellExecute(?:Ex)?[AW]?|WinExec|execl|execlp|execle|execv|execvp|execve|execvpe|posix_spawnp?|fork)\s*\("),
               "sw::Process::execute · launch · launchDetached (Core/Process/Process.h)",
               "system 은 pch 의 `<cstdlib>` 에, CreateProcess · fork 는 OS 헤더에 있다 — OS 헤더는 플랫폼 TU 가 다른 이유로 include 해야 해서 숨길 수 없다"),
    BypassRule("numberParse",
               re.compile(_kCallHead + r"(?:from_chars|atoi|atol|atoll|atof|strtol|strtoul|strtoll|strtoull|strtod|strtof|strtold|sscanf|stoi|stol|stoll|stoul|stoull|stof|stod|stold)\s*\("),
               "StringUtil::parseInt · parseInt64 · parseUint64 · parseFloat · parseDouble · tryParseBool (Core/Container/StringUtil.h)",
               "C 변환 함수는 pch 의 `<cstdlib>` 에, std::stoi 는 sw::string 이 감싸는 `<string>` 에 있어 숨길 수 없다"),
)
#: 어느 규칙이든 걸릴 수 있는 파일만 줄 단위로 본다(대부분의 파일은 여기서 끝난다).
_kAnyRuleRe = re.compile("|".join(f"(?:{rule.pattern.pattern})" for rule in _kListRule))


def _findExemptionKeysInternal(rule: str, relative: str) -> list[str]:
    """`relative` 에 맞는 `rule` 의 예외 키들."""
    prefix = f"{rule}:"
    return [key for key in CheckFacilityBypassGate.mapExemption if key.startswith(prefix) and fnmatch.fnmatchcase(relative, key[len(prefix):])]


def findBypassCalls(source: SourceFile) -> list[CountedFinding]:
    """파일 하나에서 예외 밖의 직접 호출을 규칙별로 돌려줍니다. 예외 대상 파일은 보면 `see`, 걸리면 `use` 로 적습니다."""
    gate = CheckFacilityBypassGate
    relative = source.relative
    mapExemptKey: dict[str, list[str]] = {rule.key: _findExemptionKeysInternal(rule.key, relative) for rule in _kListRule}
    for listKey in mapExemptKey.values():
        for key in listKey:
            gate.seeExemption(key)
    if findFast(_kAnyRuleRe, source.masked) is None:
        return []
    listFound: list[CountedFinding] = []
    for lineIndex, line in enumerate(source.listMaskedLine, start=1):
        for rule in _kListRule:
            for match in rule.pattern.finditer(line):
                if mapExemptKey[rule.key]:
                    gate.useExemption(mapExemptKey[rule.key][0])
                    continue
                text = source.listLine[lineIndex - 1].strip()
                listFound.append(CountedFinding(rule.key, relative, lineIndex, lineIndex, f"{rule.key} `{match.group(0).rstrip('( ')}` — {rule.instead}", text))
    return listFound


class CheckFacilityBypassGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "Core · Engine 기능을 두고 printf · rand · fopen · system · atoi · 파일 바꾸기 API 를 직접 부르는 곳(훅은 추가한 줄만, 전체는 규칙별 기준선 초과만)"
    buildComment = "Checking that code uses engine facilities instead of direct C runtime calls..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in kLintTargetRelDirs)
    sourceScanRoots = kLintTargetRelDirs
    sourceSuffixes = _kSuffixes
    violationHeader = "엔진 기능을 우회한 직접 호출"
    countedNoun = "direct calls"
    baselineRules = tuple(rule.key for rule in _kListRule)
    baselineExceededHint = "새 호출은 docs/agents/ExistingFacilities.md 의 '쓸 것' 으로"
    hint = ("  대신 쓸 것은 docs/agents/ExistingFacilities.md 의 표다 — 로그 SW_LOG_*, 난수 MathUtil::getRandom*, 파일 FileUtil::read*/write*,\n"
            "  파일을 바꾸는 직접 호출(fileMutate · libraryWrite)은 FileUtil 의 쓰기 입구로 — 서드파티가 경로를 스스로 여는 곳은 쓰기 전에 FileUtil::isWriteAllowed 를 묻는다,\n"
            "  프로세스 sw::Process, 숫자 파싱 StringUtil::parse*. 그 기능을 구현하는 파일이면 Scripts/lint/rules/CheckFacilityBypass.toml 의\n"
            "  [exemption] 에 `<규칙>:<경로>` 로 이유와 함께 적는다. 전체를 보려면 --whole-files 또는 --list.")
    selfTestCases = [
        {"name": "fprintf(stderr) · std::cout · snprintf 는 통과",
         "expectedViolation": "Source/Probe/A.cpp:3: stdout `std::fprintf`",
         "unexpectedViolation": "snprintf",
         "files": {"Source/Probe/A.cpp": "void f( char* p )\n{\n    std::fprintf( stderr, \"x\" );\n    std::snprintf( p, 4, \"y\" );\n    std::cout << 1;\n}\n"},
         "args": ["--files", "Source/Probe/A.cpp", "--whole-files"]},
        {"name": "random_device · rand(테스트 폴더)",
         "expectedViolation": "Test/Probe/B.cpp:2: random `std::random_device`",
         "files": {"Test/Probe/B.cpp": "#include <random>\nunsigned f() { std::random_device d; return d() ^ rand(); }\n"},
         "args": ["--files", "Test/Probe/B.cpp", "--whole-files"]},
        {"name": "ifstream · fopen",
         "expectedViolation": "Source/Probe/C.cpp:3: fileOpen `fopen`",
         "files": {"Source/Probe/C.cpp": "void f()\n{\n    FILE* p = fopen( \"a\", \"rb\" );\n    std::ifstream s( \"b\" );\n}\n"},
         "args": ["--files", "Source/Probe/C.cpp", "--whole-files"]},
        {"name": "std::rename · ::unlink · MoveFileExW · O_WRONLY open · 알고리즘 std::remove · 멤버 rename 선언은 통과",
         "expectedViolation": "Source/Probe/G.cpp:3: fileMutate `std::rename`",
         "unexpectedViolation": "G.cpp:8:",
         "files": {"Source/Probe/G.cpp": ("void f( const char* a, const char* b )\n{\n    std::rename( a, b );\n    ::unlink( a );\n    MoveFileExW( w, v, 0 );\n"
                                          "    int fd = open( a, O_WRONLY | O_CREAT, 0644 );\n    std::remove( a );\n    v.erase( std::remove( v.begin(), v.end(), 1 ), v.end() );\n"
                                          "    static bool rename( GameObject* p, const utf8* n );\n}\n")},
         "args": ["--files", "Source/Probe/G.cpp", "--whole-files"]},
        {"name": "PlatformFileUtil 쓰기 연산 · SQLite · DDS 저장은 게이트를 거친 자리만 예외",
         "expectedViolation": "Source/Probe/H.cpp:4: libraryWrite `sqlite3_open_v2`",
         "files": {"Source/Probe/H.cpp": ("void f()\n{\n    PlatformFileUtil::replaceFile( a, b );\n    sqlite3_open_v2( p, &d, 6, nullptr );\n"
                                          "    DirectX::SaveToDDSFile( i, n, m, 0, w );\n}\n")},
         "args": ["--files", "Source/Probe/H.cpp", "--whole-files"]},
        {"name": "popen · 멤버 system() 은 통과",
         "expectedViolation": "Source/Probe/D.cpp:3: processSpawn `popen`",
         "unexpectedViolation": "D.cpp:4",
         "files": {"Source/Probe/D.cpp": "void f( Host& host )\n{\n    popen( \"ls\", \"r\" );\n    host.system( 1 );\n}\n"},
         "args": ["--files", "Source/Probe/D.cpp", "--whole-files"]},
        {"name": "std::stoi · 주석 안의 atoi 는 통과",
         "expectedViolation": "Source/Probe/E.cpp:3: numberParse `std::stoi`",
         "unexpectedViolation": "E.cpp:2",
         "files": {"Source/Probe/E.cpp": "// atoi( x ) 는 쓰지 않는다\nint f( const std::string& s ) { return 0; }\nint g( const std::string& s ) { return std::stoi( s ); }\n"},
         "args": ["--files", "Source/Probe/E.cpp", "--whole-files"]},
        {"name": "전체 훑기 기준선 초과",
         "expectedViolation": "stdout 1건이 기준선 0 을 넘었다",
         "files": {"Source/Probe/F.cpp": "void f() { puts( \"x\" ); }\n"},
         "args": ["--baseline", "0"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        return findBypassCalls(source)


main = CheckFacilityBypassGate.run

if __name__ == "__main__":
    sys.exit(main())
