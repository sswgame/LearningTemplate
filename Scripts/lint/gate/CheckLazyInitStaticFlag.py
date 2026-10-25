#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""지연 초기화를 손으로 짠 곳을 잡는다 — 함수 안 정적 변수(`static T value = make();`) 나 `std::call_once` 로 바꿀 곳이다.

두 스레드가 동시에 첫 호출을 하면 둘 다 초기화를 돌거나 한쪽이 반쯤 만든 것을 읽는다(셰이더 리플렉션 로더에서 실제로 났다).
함수 안 정적 변수는 C++11 부터 초기화가 한 번만 일어나도록 보장되므로 이 모양을 손으로 적을 일이 없다. 규칙 둘:

  - `total`    : 맨 `static bool s_bTried{ false }; if ( s_bTried == false ) { ... }` 의 `static bool` 한 줄.
  - `lazyFill` : 변수를 먼저 검사하고 같은 블록 안에서 채우는 모양 — `static T* s_p = nullptr; if ( s_p == nullptr ) { s_p = ...; }`,
                 `static` 전역 · 파일 범위 전역(`bool` · 포인터 · `unique_ptr` · `shared_ptr` · `optional` · 컨테이너)을 `empty()` · `!x` ·
                 `== nullptr` 로 검사하고 채우는 곳. **검사와 대입이 같은 블록 · 같은 변수**일 때만 센다.

`std::call_once` · `once_flag` · `atomic` · `mutex`/`lock` 이 검사 앞에 있는 블록 · `const` · `constexpr` · `thread_local` 은 센 적 없다(이미 안전하다).
`static bool` 은 표준 낱말이라 형식으로 막을 수 없다 — 그래서 막지 못하는 틈에만 이 게이트를 둔다.
메인 스레드 전용이 보장되는 곳은 고치지 말고 `rules/CheckLazyInitStaticFlag.toml` 기준선 주석에 근거를 적는다.

  python Scripts/lint/gate/CheckLazyInitStaticFlag.py [--root <repo>] [--files a.cpp b.h] [--list]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402

_kScanRoots = ("Source",)
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

# `static bool name`(앞에 const · constexpr · thread_local 이 없고 atomic 이 아닌 것) — 줄 머리 기준.
_kStaticBoolRe = re.compile(r"^\s*static\s+bool\s+\w+\s*(?:[={;])")

# 변수 선언 한 줄 — `[static] 형 이름 [= nullptr | {}];`. 초기값이 비어 있거나 0 · false · nullptr 인 것만(값을 가진 선언은 지연 초기화가 아니다).
_kDeclRe = re.compile(
    r"^[ \t]*(?:inline\s+)?(?P<static>static\s+)?(?:inline\s+)?(?P<rest>[A-Za-z_][^;=(){}]*?)"
    r"\s*(?P<init>=\s*(?:nullptr|NULL|false|0)\s*|=\s*\{\s*\}\s*|\{\s*(?:nullptr|false|0)?\s*\})?;[ \t]*$")
_kStopWords = frozenset({"return", "using", "typedef", "friend", "extern", "goto", "delete", "else", "case", "throw", "new", "co_return"})
_kContainerRe = re.compile(r"^(?:std::)?(?:unique_ptr|shared_ptr|optional|vector|map|unordered_map|set|unordered_set|string|deque|list)\b")
# 잠금 · 원자 구문을 가리키는 낱말만 본다. 부분 문자열로 찾으면 `bLockWanted` · `getLockMode`(마우스 잠금) 같은 이름이 잠금으로 읽혀 위반이 가려졌다
# (`InputManagerX11` 의 `s_bWaylandLockReported` 가 그렇게 빠졌다). 낱말 경계를 두고 잠금 형식 이름만 센다.
_kLockRe = re.compile(
    r"\b(?:\w*_lock\w*|lock_\w+|lock|unlock|try_lock|\w*[Ss]coped\w*|\w*[Ss]pin\w*|\w*[Mm]utex\w*|\w*[Aa]tomic\w*|call_once|once_flag|compare_exchange\w*|critical\w*)\b")
_kIfRe = re.compile(r"\bif\s*(?:\(|constexpr)")
_kNamespaceRe = re.compile(r"\bnamespace\b|\bextern\b")
_kTypeBodyRe = re.compile(r"\b(?:class|struct|union|enum)\b")

# 채우는 말들. `==` 는 대입이 아니다.
_kFillMethods = r"(?:emplace|emplace_back|push_back|insert|reset|assign|resize|swap|insert_or_assign|try_emplace)"


class _Block:
    """중괄호 블록 하나 — `kind` 는 namespace · type · other(함수 · 람다 · 제어문 · 초기화 중괄호)."""

    __slots__ = ("open", "close", "kind")

    def __init__(self, open_: int, kind: str) -> None:
        self.open = open_
        self.close = -1
        self.kind = kind


def _blankPreprocessor(masked: str) -> str:
    """`#if defined( X )` 같은 줄의 괄호가 다음 블록의 머리로 섞여 들지 않게 전처리 줄을 같은 길이의 공백으로 지운다(줄바꿈은 둔다)."""
    return re.sub(r"(?m)^[ \t]*#(?:[^\n\\]|\\\n|\\)*", lambda match: re.sub(r"[^\n]", " ", match.group(0)), masked)


def _parseBlocks(masked: str) -> list[_Block]:
    masked = _blankPreprocessor(masked)
    listBlock: list[_Block] = []
    stack: list[_Block] = []
    boundary = 0
    for index, char in enumerate(masked):
        if char == "{":
            header = masked[boundary:index]
            if "(" not in header and _kTypeBodyRe.search(header) is not None:
                kind = "type"
            elif "(" not in header and _kNamespaceRe.search(header) is not None:
                kind = "namespace"
            else:
                kind = "other"
            block = _Block(index, kind)
            listBlock.append(block)
            stack.append(block)
            boundary = index + 1
        elif char == "}":
            if stack:
                stack.pop().close = index
            boundary = index + 1
        elif char == ";":
            boundary = index + 1
    for block in stack:
        block.close = len(masked)
    return listBlock


def _ancestors(listBlock: list[_Block], position: int) -> list[_Block]:
    """`position` 을 감싼 블록 — 바깥에서 안쪽 순."""
    return [block for block in listBlock if block.open < position < block.close]


def _matchParen(masked: str, openIndex: int) -> int:
    depth = 0
    for index in range(openIndex, len(masked)):
        if masked[index] == "(":
            depth += 1
        elif masked[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    return -1


def _parseDeclaration(line: str) -> tuple[str, bool] | None:
    """선언 한 줄이 가변 지연 초기화 후보면 (이름, static 인가). 아니면 None."""
    match = _kDeclRe.match(line)
    if match is None:
        return None
    rest = match.group("rest").strip()
    nameMatch = re.search(r"(?:[A-Za-z_]\w*\s*::\s*)*([A-Za-z_]\w*)\s*$", rest)   # `Class::s_x` 정의도 이름은 마지막 낱말
    if nameMatch is None:
        return None
    name = nameMatch.group(1)
    typeText = rest[:nameMatch.start()].strip()
    if not typeText or typeText.split()[0] in _kStopWords or name in _kStopWords:
        return None
    if re.search(r"\b(?:constexpr|thread_local|atomic|mutex|once_flag|static)\b|atomic<|&", typeText) is not None:
        return None
    if "*" in typeText:
        if re.search(r"\bconst\s*$", typeText) is not None:   # `T* const` — 포인터 자체가 상수
            return None
    elif re.search(r"\bconst\b", typeText) is not None:
        return None
    bPointer = "*" in typeText
    bBool = typeText == "bool"
    bContainer = _kContainerRe.match(typeText) is not None
    bNullInit = re.search(r"nullptr|NULL", match.group("init") or "") is not None   # `HANDLE h{ nullptr }` 처럼 형이 포인터로 안 보여도 널로 시작하면 포인터다
    if not (bPointer or bBool or bContainer or bNullInit):
        return None
    return name, match.group("static") is not None


def _testsVariable(condition: str, name: str) -> bool:
    """`if` 조건이 변수를 **비었는지 · 만들었는지** 검사하는가 — 다른 값과 견주는 `s_p == pOther` 는 검사가 아니다."""
    n = r"(?:\w+\s*::\s*)*" + re.escape(name)   # `Internal::s_bTried` 처럼 이름을 한정한 모양도 같은 변수다
    nullWord = r"(?:nullptr|NULL|false|true|0)"
    return re.search(
        rf"\b{n}\s*[=!]=\s*{nullWord}\b|\b{nullWord}\s*[=!]=\s*{n}\b|!\s*{n}\b|"
        rf"\b{n}\s*\.\s*(?:empty|has_value|get|find|count|contains|size)\s*\(|"
        rf"\(\s*{n}\s*\)|\b{n}\s*(?:&&|\|\|)|(?:&&|\|\|)\s*{n}\s*(?:\)|&&|\|\|)", condition) is not None


class CheckLazyInitStaticFlagGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "지연 초기화가 맨 static bool 플래그 · 검사 후 채우기가 아니라 함수 안 정적 변수 · call_once 인지 검사(기준선 래칫)"
    buildComment = "Checking that lazy initialization avoids bare static bool flags and check-then-fill globals..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoots)
    sourceScanRoots = _kScanRoots
    sourceSuffixes = _kSuffixes
    baselineRules = ("total", "lazyFill")
    violationHeader = "지연 초기화 경쟁"
    countedNoun = "lazy-init races"
    baselineExceededHint = "함수 안 정적 변수 · std::call_once · mutex 로 바꾼다(메인 스레드 전용이면 기준선 주석에 근거)"
    hint = ("  static const Loader s_loader = makeLoader();      // C++11 이 초기화를 한 번만 보장한다\n"
            "  static std::once_flag s_once; std::call_once( s_once, init );\n"
            "  `static bool s_bTried` + `if` · `if ( s_p == nullptr ) s_p = make();` 는 두 스레드가 동시에 첫 호출을 하면 둘 다 초기화한다. 전체를 보려면 --list.")
    selfTestCases = [
        {"name": "함수 안 맨 플래그", "expectedViolation": "1  Source/Probe/A.cpp",
         "files": {"Source/Probe/A.cpp": "void load()\n{\n    static bool s_bTried{ false };\n    if ( s_bTried == false )\n    {\n        s_bTried = true;\n    }\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "=로 초기화", "expectedViolation": "1  Source/Probe/B.cpp",
         "files": {"Source/Probe/B.cpp": "void load()\n{\n    static bool s_bDone = false;\n}\n"}, "args": ["--baseline", "0"]},
        {"name": "함수 안 static 포인터 검사 후 채우기", "expectedViolation": "1  Source/Probe/C.cpp",
         "files": {"Source/Probe/C.cpp": "Loader* get()\n{\n    static Loader* s_p = nullptr;\n    if ( s_p == nullptr )\n    {\n        s_p = makeLoader();\n    }\n    return s_p;\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "파일 범위 unique_ptr 를 !x 로 검사 후 채우기", "expectedViolation": "1  Source/Probe/D.cpp",
         "files": {"Source/Probe/D.cpp": "namespace\n{\nstd::unique_ptr<Loader> g_loader;\n}\nLoader& get()\n{\n    if ( !g_loader )\n    {\n        g_loader = std::make_unique<Loader>();\n    }\n    return *g_loader;\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "static 컨테이너를 empty 로 검사 후 채우기", "expectedViolation": "1  Source/Probe/E.cpp",
         "files": {"Source/Probe/E.cpp": "const std::vector<int>& table()\n{\n    static std::vector<int> s_table;\n    if ( s_table.empty() )\n    {\n        s_table.push_back( 1 );\n    }\n    return s_table;\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "파일 범위 맨 bool 를 검사 후 채우기", "expectedViolation": "1  Source/Probe/F.cpp",
         "files": {"Source/Probe/F.cpp": "bool g_bReady = false;\nvoid init()\n{\n    if ( g_bReady == false )\n    {\n        g_bReady = true;\n    }\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "call_once 와 lock 은 이미 안전하다", "expectedViolation": "1  Source/Probe/G.cpp",
         "unexpectedViolation": "Source/Probe/H.cpp",
         "files": {"Source/Probe/G.cpp": "void load()\n{\n    static bool s_bTried{ false };\n}\n",
                   "Source/Probe/H.cpp": "Loader* get()\n{\n    static Loader* s_p = nullptr;\n    std::lock_guard guard( g_mutex );\n    if ( s_p == nullptr )\n    {\n        s_p = makeLoader();\n    }\n    return s_p;\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "이름에 Lock 이 든 변수는 잠금이 아니다(inline static · 뒤따르는 대입)", "expectedViolation": "1  Source/Probe/I.cpp",
         "files": {"Source/Probe/I.cpp": "namespace\n{\ninline static bool s_bLockReported{ false };\n}\nvoid sync( bool bLockWanted )\n{\n    if ( bLockWanted && s_bLockReported == false )\n        report();\n    s_bLockReported = s_bLockReported || bLockWanted;\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "조기 반환형 — 클래스로 한정한 플래그를 검사하고 반환한 뒤 채운다", "expectedViolation": "1  Source/Probe/J.cpp",
         "files": {"Source/Probe/J.cpp": "namespace\n{\nstruct Internal\n{\n    inline static bool _s_bTried = false;\n};\n}\nvoid init()\n{\n    if ( Internal::_s_bTried )\n        return;\n    Internal::_s_bTried = true;\n    work();\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "조기 반환형 — 원자 교환은 이미 안전하다", "unexpectedViolation": "Source/Probe/K.cpp",
         "expectedViolation": "1  Source/Probe/L.cpp",
         "files": {"Source/Probe/K.cpp": "namespace\n{\nstruct Internal\n{\n    inline static atomic<bool> s_bInstalled{ false };\n};\n}\nvoid init()\n{\n    if ( Internal::s_bInstalled.exchange( true ) )\n        return;\n    work();\n}\n",
                   "Source/Probe/L.cpp": "namespace\n{\nstruct Internal\n{\n    static inline bool s_bInstalled{ false };\n};\n}\nvoid init()\n{\n    if ( Internal::s_bInstalled )\n        return;\n    work();\n    Internal::s_bInstalled = true;\n}\n"},
         "args": ["--baseline", "0"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        listFound: list[CountedFinding] = []
        for lineIndex, line in enumerate(source.listMaskedLine, start=1):
            if _kStaticBoolRe.match(line) is not None:
                listFound.append(CountedFinding("total", source.relative, lineIndex, lineIndex,
                                                "맨 static bool 플래그 — 함수 안 정적 변수나 std::call_once 로", source.listLine[lineIndex - 1].strip()))
        listFound += self.findCheckThenFill(source)
        return listFound

    @staticmethod
    def findCheckThenFill(source: SourceFile) -> list[CountedFinding]:
        """변수를 `if` 로 검사하고 같은 블록에서 채우는 곳. 후보 선언이 없는 파일은 블록을 풀지 않고 바로 돌려준다."""
        masked = source.masked
        listLine = source.listMaskedLine
        listCandidate: list[tuple[str, int, int, int]] = []   # (이름, 선언 줄, 범위 시작, 범위 끝)
        listBlock: list[_Block] | None = None
        offset = 0
        for lineIndex, line in enumerate(listLine, start=1):
            lineStart = offset
            offset += len(line) + 1
            parsed = _parseDeclaration(line)
            if parsed is None or _kStaticBoolRe.match(line) is not None:
                continue
            if listBlock is None:
                listBlock = _parseBlocks(masked)
            name, bStatic = parsed
            listChain = _ancestors(listBlock, lineStart + len(line) - len(line.lstrip()))
            listOther = [block for block in listChain if block.kind == "other"]
            if not listOther and not bStatic and any(block.kind == "type" for block in listChain):
                continue   # 인스턴스 멤버는 그 인스턴스의 일이다 — 정적 멤버만 본다
            if listOther:
                if not bStatic:
                    continue   # 함수 안 일반 지역 변수는 지연 초기화가 아니다
                inner = listOther[-1]
                if listChain[-1] is not inner:
                    continue
                listCandidate.append((name, lineIndex, inner.open, inner.close))
            else:
                listCandidate.append((name, lineIndex, 0, len(masked)))
        if not listCandidate or listBlock is None:
            return []

        listFound: list[CountedFinding] = []
        setSeen: set[tuple[str, int]] = set()
        for ifMatch in _kIfRe.finditer(masked):
            parenOpen = masked.find("(", ifMatch.start())
            parenClose = _matchParen(masked, parenOpen)
            if parenClose < 0:
                continue
            condition = masked[parenOpen:parenClose + 1]
            listChain = _ancestors(listBlock, ifMatch.start())
            if not listChain:
                continue
            parent = listChain[-1]
            for name, declLine, scopeStart, scopeEnd in listCandidate:
                if not (scopeStart <= ifMatch.start() <= scopeEnd) or not _testsVariable(condition, name):
                    continue
                key = (name, parent.open)
                if key in setSeen:
                    continue
                region = masked[parenClose:parent.close]
                # 채움 = 널 · false · 0 이 아닌 값을 넣는 것. `p = nullptr` · `b = false` · `p.reset()` 은 지우는 일이라 지연 초기화가 아니다.
                if re.search(rf"\b{re.escape(name)}\s*(?:\[[^\]]*\]\s*)?=(?!=)(?!\s*(?:nullptr|NULL|false|0)\s*[;,)])|"
                             rf"\b{re.escape(name)}\s*\.\s*{_kFillMethods}\s*\(\s*[^)\s]", region) is None:
                    continue
                if any(_kLockRe.search(masked[block.open:ifMatch.start()]) is not None for block in listChain if block.kind == "other"):
                    continue
                setSeen.add(key)
                lineNumber = masked.count("\n", 0, ifMatch.start()) + 1
                listFound.append(CountedFinding("lazyFill", source.relative, lineNumber, lineNumber,
                                                f"{name}(선언 {declLine}줄)을 검사하고 같은 블록에서 채운다 — 함수 안 정적 변수 · call_once 로",
                                                source.listLine[lineNumber - 1].strip()))
        return listFound


main = CheckLazyInitStaticFlagGate.run

if __name__ == "__main__":
    sys.exit(main())
