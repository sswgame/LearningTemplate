#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
**정적 저장 기간의 가변 변수**를 막는다 — 소유자 없는 숨은 전역 상태. 장소가 어디든 같은 모양이다.

`static T& getXxx() { static T s; return s; }` 는 이름이 무엇이든(`instance()` · `shared()` · `cache()` …) 전역 단일체다. 접근자만이 아니다 —
`static bool s_warned = false;` · `namespace { vector<X> s_list; }` · `struct FooInternal { static inline vector<X> s_list{}; }` ·
`ScopeOwned<vector<X>> s_listOwner;` 도 같은 모양이다. 수명과 초기화 순서가 숨고, 병렬 테스트와 둘째 인스턴스가 같은 칸을 나눠 쓰며, 끝낼 때 지워지는 시점이
정해져 있지 않다. 소유자가 든 객체(서브시스템 · 멤버)로 옮기면 수명이 소유자의 것이 된다. `ScopeOwned<T>` 로 감싸도 그 *칸*이 전역이면 같은 모양이다 —
소유자는 칸을 든 객체이지 칸을 감싼 형식이 아니다.
`CheckServiceAccess` 의 `ServiceAccess/Singleton` 은 **헤더의 접근자**만 보고 `.cpp` 의 익명 · `…Internal` 이름공간 접근자는 보지 못했다 — 이 게이트가 그 틈과 같은 모양 전부를 막는다.

이름이 아니라 **모양**으로 판정한다. 변수 선언의 타입 부분을 읽어 **객체 자체가 상수가 아니면** 가변이다. 세 곳을 본다:

  - `MutableStatic/Local`    — 함수 본문(람다 포함) 안의 `static` 변수 (`static T s;` · `static std::vector<T> cache;` · `static int counter = 0;` · `static auto& r = f();`)
  - `MutableStatic/Namespace` — 네임스페이스 수준의 변수. `static` 이든 익명 이름공간이든 `thread_local` 이든 `ScopeOwned<T> s_xOwner;` 든 같다
  - `MutableStatic/Member`   — 클래스의 `static` 데이터 멤버 (`static inline T s_x;` · `inline static T s_x{};` · 선언만 두고 밖에서 정의하는 것은 선언 쪽만 센다)

**허용(상수)**: `const` · `constexpr` — `static const hashed_string kName`, `static constexpr Row kTable[]`, 포인터는 `T* const`. `const T*` 는 *포인터가 가변*이라 상수가 아니다.
템플릿 인자 안의 `const`(`std::vector<const T*>`)도 세지 않는다. `constinit` 만으로는 상수가 아니다(초기화 순서만 고정할 뿐 값은 바뀐다).
`thread_local` 도 막는다 — 스레드마다 하나일 뿐 수명이 숨고, 같은 스레드의 테스트는 서로의 값을 밟는다. 정말 스레드별 저장소여야 하면(크래시 경로 · 할당 훅의 재귀 방지 깃발)
예외 표에 이유와 함께 올린다.
**대상 아님**: 인스턴스 멤버, 함수 선언 · 정의(`static void f()`), `static_assert`, 람다 안의 *비정적* 변수, `extern` 선언, `using` · `typedef`, 클래스 밖에서 한 static 멤버의 정의(`int S::s_n = 0;`),
전역 변수 매크로(`SW_GLOBAL_VARIABLE(…)` — 전역 변수 체계가 소유한다).

**한계(모양으로 판정할 수 없는 것)**: ① 네임스페이스 · 클래스 수준의 *괄호 초기화* 변수(`static Foo s( 1 );`)는 함수 선언과 글자가 같아(가장 성가신 구문 해석) 보지 않는다 — `= `와 `{ }` 초기화, 초기화 없는 선언은 본다.
함수 안에서는 괄호 초기화도 본다(블록 안에 정적 함수 선언은 없다). ② 매크로 본문(`#define`)은 지운다 — 매크로 안의 변수는 보지 않는다. ③ `struct S { … } g_s;` 처럼 정의에 붙은 변수는 보지 않는다.
④ 지역 클래스 안의 `static` 멤버 함수 *선언*(정의는 본다)은 괄호 초기화 변수와 구별되지 않아 변수로 센다.

여는 중괄호가 무엇을 여는지(함수 본문 · 람다 · 네임스페이스 · 클래스)는 보고서 `RunFunctionSize` 가 쓰는 머리 읽기(`lint/CppScope.py`)를 그대로 쓴다.

예외는 `Scripts/lint/rules/CheckMutableStatics.toml` 의 `[exemption]`(`<파일>:<변수 이름>` → 이유) 하나다. 소유자가 든 객체로 못 옮기는 까닭이 있는 자리만 이유와 함께 올린다
(크래시 경로의 잠금 없는 저장소처럼). 그 변수가 더는 위반이 아니면 낡은 예외로 실패한다.
**형식으로 막지 못하는 틈이라 게이트를 둔다** — 정적 저장 기간 변수는 언어가 늘 허락하고, 소유자를 정하는 것은 쓰는 쪽이다.

대상은 `Source/` · `Test/` · `Tools/ReflectionParser/` · `Tools/OnlineLoadBot/` 의 `.h` · `.cpp` · `.inl` 이다(생성 `.gen.cpp` 와 ThirdParty 제외).
`Test/` 도 본다 — 테스트가 정적 변수에 상태를 두면 병렬로 도는 테스트가 서로의 상태를 밟는다.

  python Scripts/lint/gate/CheckMutableStatics.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import blankMatch  # noqa: E402
from common.Constants import kLintTargetRelDirs  # noqa: E402
from lint.CppScope import blankPreprocessor, classifyHead, isLambdaOpen  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult, joinFindings, scanSourceGates  # noqa: E402

_kListScanRoot = kLintTargetRelDirs
_kSuffixes = (".h", ".cpp", ".inl")

_kCategoryLocal = "MutableStatic/Local"
_kCategoryNamespace = "MutableStatic/Namespace"
_kCategoryMember = "MutableStatic/Member"
_kListCategory = (_kCategoryLocal, _kCategoryNamespace, _kCategoryMember)
_kMapPlace = {
    _kCategoryLocal: "함수 안 가변 static 지역",
    _kCategoryNamespace: "네임스페이스 수준 가변 변수(파일 전역)",
    _kCategoryMember: "클래스의 가변 static 데이터 멤버",
}

#: 걷는 자리 — 중괄호와 `;`(선언 자리를 끊는다), 그리고 `static` 낱말(`static_cast` · `static_assert` 는 낱말 경계에 걸리지 않는다).
_kTokenRe = re.compile(r"[{};]|\bstatic\b|\bthread_local\b")
_kStaticRe = re.compile(r"\bstatic\b")
_kAlignasRe = re.compile(r"\balignas\s*\([^()]*\)")
_kTemplateArgumentRe = re.compile(r"<[^<>]*>")
_kWordRe = re.compile(r"[A-Za-z_]\w*|\*|&")
#: 선언자 앞의 `(` 가 함수 포인터(`void ( *pFn )( int )`)인가 — 뒤에 `*` 가 온다.
_kFunctionPointerRe = re.compile(r"\(\s*\*\s*((?:const\s+)?)(\w+)")
#: 괄호 닫은 뒤에 이어지는 함수 정의의 꼬리(지역 클래스 안의 `static` 멤버 함수).
_kFunctionTailRe = re.compile(r"\s*(?:\{|const\b|noexcept\b|override\b|final\b|->|:)")
#: 네임스페이스 수준 문장의 첫 낱말이 이것이면 변수 선언이 아니다.
_kNotVariableHead = frozenset({"using", "typedef", "template", "friend", "extern", "class", "struct", "union", "enum", "namespace",
                               "return", "static_assert", "concept", "goto", "case", "default"})
#: 정적 저장 기간이 아닌 이름 — 이름 앞이 `::` 이면 클래스 밖에서 한 static 멤버의 정의다.
_kQualifiedNameRe = re.compile(r"::\s*\w+\s*$")

_kFrameNamespace = "namespace"   # 네임스페이스 · `extern "C"` 본문(그리고 파일 수준)
_kFrameClass = "class"           # class · struct · union 본문
_kFrameSkip = "skip"             # enum · 초기화(`= { … }`)
_kFrameCode = "code"             # 함수 · 람다 본문과 그 안의 모든 블록
_kFrameInitCode = "initcode"     # 선언 자리 초기화 안의 람다 본문(`auto g = []{ … };`) — 코드지만 문장은 `;` 까지 이어진다
_kCodeFrame = (_kFrameCode, _kFrameInitCode)


def readFrameKind(code: str, braceIndex: int, headStart: int, listFrame: list[str]) -> str:
    """`code[braceIndex]` 의 `{` 가 여는 틀의 종류 — 선언 자리에서는 머리를 읽고, 함수 안에서는 모두 코드, 초기화 안에서는 람다만 코드다."""
    parent = listFrame[-1] if listFrame else _kFrameNamespace
    if parent in _kCodeFrame:
        return _kFrameCode
    if parent == _kFrameSkip:
        return _kFrameInitCode if isLambdaOpen(code, braceIndex) else _kFrameSkip
    head = code[headStart:braceIndex]
    kind = classifyHead(head)
    if kind == "scope":
        return _kFrameNamespace if re.search(r"\bnamespace\b|^\s*extern\b", head) is not None else _kFrameClass
    if kind.startswith("function:"):
        return _kFrameCode
    return _kFrameInitCode if kind == "skip" and isLambdaOpen(code, braceIndex) else _kFrameSkip


def findDeclaratorEnd(code: str, start: int) -> tuple[int, str]:
    """선언자(타입 + 이름)의 끝을 `start` 부터 찾아 (끝 위치, 끝을 가른 글자)를 돌려줍니다. 꺾쇠 안의 `=` `(` `,` 는 건너뛴다."""
    angle = 0
    index = start
    while index < len(code):
        character = code[index]
        if code.startswith("[[", index):   # 속성 `[[maybe_unused]]` 는 배열 첨자가 아니다
            index = code.find("]]", index) + 2 if "]]" in code[index:] else len(code)
            continue
        if character == "<":
            angle += 1
        elif character == ">" and code[index - 1] != "-":
            angle = max(0, angle - 1)
        elif character in ";{" or (angle == 0 and character in "=([,"):
            return index, character
        index += 1
    return len(code), ""


def closingParenIndex(code: str, openIndex: int) -> int:
    depth = 0
    for index in range(openIndex, len(code)):
        if code[index] == "(":
            depth += 1
        elif code[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    return -1


def readMutableVariableName(code: str, begin: int, listSpecifier: list[str], bScopeLevel: bool, bStatement: bool) -> str | None:
    """
    `code[begin:]` 에서 시작하는 변수 선언이 **가변**이면 그 이름을, 상수 · 변수가 아니면 None 을 돌려줍니다.
    `listSpecifier` 는 선언자 앞에서 이미 읽은 낱말(`static` 앞의 `thread_local` · `constexpr`)이다. 타입 부분(이름 앞)의 낱말만 본다 — 템플릿 인자는 지운다.
    포인터면 `*` 뒤의 `const` 가, 아니면 어디든 `const` 가 상수다. `bScopeLevel` 이면 괄호 초기화는 함수 선언과 구별되지 않아 보지 않는다.
    `bStatement` 이면 `static` 없이 시작하는 네임스페이스 수준 문장이라 첫 낱말이 변수가 아닌 선언 키워드(`using` · `typedef` …)인 것을 거른다.
    """
    end, separator = findDeclaratorEnd(code, begin)
    head = code[begin:end]
    if separator == "(":
        if bScopeLevel:
            return None
        pointer = _kFunctionPointerRe.match(code, end)
        if pointer is not None:   # 함수 포인터 변수: 앞이 반환 형식, 이름은 `( *` 뒤, 상수는 `* const`
            return None if pointer.group(1) else pointer.group(2)
        closeIndex = closingParenIndex(code, end)
        if closeIndex < 0 or _kFunctionTailRe.match(code, closeIndex + 1) is not None:
            return None   # 지역 클래스의 `static` 멤버 함수 정의
    while True:
        stripped = _kTemplateArgumentRe.sub(" ", head)
        if stripped == head:
            break
        head = stripped
    if _kQualifiedNameRe.search(head) is not None:
        return None   # `int S::s_n = 0;` — 클래스 안 선언 쪽이 이미 센다
    listWord = _kWordRe.findall(head)
    nameIndex = max((index for index, word in enumerate(listWord) if word not in ("*", "&")), default=-1)
    if nameIndex < 1 or all(word in ("*", "&") for word in listWord[:nameIndex]):   # 타입과 이름이 둘 다 있어야 변수 선언이다
        return None
    if "operator" in listWord or (bStatement and listWord[0] in _kNotVariableHead):
        return None
    name = listWord[nameIndex]
    listType = listSpecifier + listWord[:nameIndex]
    if "constexpr" in listType or "consteval" in listType:
        return None
    lastStar = max((index for index, word in enumerate(listType) if word == "*"), default=-1)
    if "const" in listType[lastStar + 1:]:
        return None
    return name


def readSpecifierBefore(code: str, staticOffset: int) -> list[str]:
    """`static` 앞에 온 낱말(`thread_local static` · `constexpr static`)도 같은 선언의 지정자다 — 앞 문장 끝(`;` `{` `}`)부터 읽는다."""
    statementStart = max(code.rfind(separatorCharacter, 0, staticOffset) for separatorCharacter in ";{}") + 1
    return _kWordRe.findall(code[statementStart:staticOffset])


def isStaticInStatement(code: str, offset: int) -> bool:
    """`offset` 를 든 문장(앞 `;` `{` `}` 부터 다음 `;` 까지)에 `static` 낱말이 있는가."""
    statementStart = max(code.rfind(separatorCharacter, 0, offset) for separatorCharacter in ";{}") + 1
    statementEnd = code.find(";", offset)
    return _kStaticRe.search(code, statementStart, len(code) if statementEnd < 0 else statementEnd) is not None


def findMutableStatics(code: str) -> list[tuple[int, str, str]]:
    """
    (위치, 변수 이름, 범주) 목록. `code` 는 주석 · 문자열 · 전처리 줄을 지운 글이다.
    틀을 걸으며 `static` 낱말은 함수 안이면 지역, 클래스 안이면 멤버, 네임스페이스 수준이면 파일 전역으로 읽고,
    네임스페이스 수준의 `static` 없는 문장(익명 이름공간의 `T g_x;` · `thread_local T t_x;`)은 `;` 에서 읽는다.
    """
    listFound: list[tuple[int, str, str]] = []
    listFrame: list[str] = []
    headStart = 0
    code = _kAlignasRe.sub(blankMatch, code)   # `alignas( 16 )` 의 괄호가 선언자를 함수 선언처럼 보이게 한다 — 같은 길이의 공백으로 지운다
    for match in _kTokenRe.finditer(code):
        token = match.group(0)
        index = match.start()
        top = listFrame[-1] if listFrame else _kFrameNamespace
        if token == "static" or token == "thread_local":
            if top == _kFrameSkip or (token == "thread_local" and (top not in _kCodeFrame or isStaticInStatement(code, index))):
                continue   # 함수 밖의 `thread_local` 은 문장 쪽이 읽고, `static` 과 함께 쓴 것은 `static` 쪽이 읽는다
            name = readMutableVariableName(code, index + len(token), readSpecifierBefore(code, index), top not in _kCodeFrame, False)
            if name is not None:
                category = _kCategoryLocal if top in _kCodeFrame else _kCategoryMember if top == _kFrameClass else _kCategoryNamespace
                listFound.append((index, name, category))
        elif token == ";":
            if top == _kFrameNamespace:
                statement = code[headStart:index]
                if _kStaticRe.search(statement) is None:
                    name = readMutableVariableName(code, headStart, [], True, True)
                    if name is not None:
                        listFound.append((headStart + len(statement) - len(statement.lstrip()), name, _kCategoryNamespace))
            if top in (_kFrameNamespace, _kFrameClass):
                headStart = index + 1
        elif token == "{":
            frame = readFrameKind(code, index, headStart, listFrame)
            listFrame.append(frame)
            if frame in (_kFrameNamespace, _kFrameClass):
                headStart = index + 1
        elif listFrame:   # "}"
            frame = listFrame.pop()
            if frame not in (_kFrameSkip, _kFrameInitCode) and (not listFrame or listFrame[-1] in (_kFrameNamespace, _kFrameClass)):
                headStart = index + 1
    return listFound


def findMutableStaticViolations(source: SourceFile) -> list[str]:
    """한 파일의 정적 저장 기간 가변 변수를 위반 줄로 돌려줍니다."""
    relative = source.relative
    if relative.endswith(".gen.cpp") or "/ThirdParty/" in f"/{relative}":
        return []
    code = blankPreprocessor(source.masked)
    gate = CheckMutableStaticsGate
    listViolation: list[str] = []
    for offset, name, category in findMutableStatics(code):
        key = f"{relative}:{name}"
        if key in gate.mapExemption:
            gate.useExemption(key)
            continue
        lineNumber = code.count("\n", 0, offset) + 1
        listViolation.append(f"{relative}:{lineNumber}: [{category}] {name} — {_kMapPlace[category]}입니다(소유자 없는 숨은 전역 상태). "
                             f"소유자가 든 객체(서브시스템 · 멤버)로 옮기거나 상수면 const / constexpr 를 붙이십시오 | {source.listLine[lineNumber - 1].strip()}")
    return listViolation


def findMutableStaticViolationsInTree(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """`repositoryRoot`(셀프테스트의 조각 트리)를 이 게이트 하나로 훑은 위반 — 게이트와 같은 훑기다."""
    gate = CheckMutableStaticsGate()
    args = argparse.Namespace(files=listTargetFile)
    return gate.finishSourceScan(repositoryRoot, args, scanSourceGates(repositoryRoot, [(gate, args)])[0]).listViolation


class CheckMutableStaticsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 허용 쪽(상수 · 멤버 · 선언)은 `selftest/CheckMutableStaticsSelfTest.py` 가 본다."""

    description = "정적 저장 기간의 가변 변수(함수 안 static · 네임스페이스 수준 · 클래스 static 멤버 · thread_local)가 없는지 검사 — 이름이 아니라 모양으로"
    buildComment = "Checking mutable variables with static storage duration..."
    timeoutSeconds = 60
    # 아래 두 줄이 이 게이트를 CTest · 커밋 훅에서 뺀다. main 의 위반이 소유자 있는 객체로 다 옮겨진 뒤(staticeditor · staticselftest · staticengine 병합 뒤 위반 0)
    # 두 줄을 지우면 켜진다. 직접 실행(`py -3 -m Scripts gate CheckMutableStatics`)은 지금도 같다.
    ctestSkipReason = "main 에 남은 정적 가변 변수가 소유자 있는 객체로 옮겨지는 동안 켜지 않는다 — 위반 0 이 되면 이 줄과 preCommitSkipReason 을 지운다"
    preCommitSkipReason = "ctestSkipReason 과 같다 — 위반이 0 이 되면 켠다"
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    violationHeader = "정적 저장 기간의 가변 변수"
    hint = (
        "  정적 변수는 수명과 순서가 숨고 병렬 테스트 · 둘째 인스턴스가 같은 칸을 나눠 씁니다. 소유자를 정합니다:\n"
        "      서비스 · 캐시 · 카운터 · 버퍼 -> 서브시스템이나 그것을 쓰는 객체의 멤버(ScopeOwned 로 감싼 전역 칸도 같은 모양입니다)\n"
        "      한 번만 하는 일 -> 소유자가 든 깃발     상수 표 · 이름표 -> static const / static constexpr (허용)\n"
        "  정말 못 옮기는 까닭이 있으면 Scripts/lint/rules/CheckMutableStatics.toml 의 [exemption] 에 \"<파일>:<변수 이름>\" 과 이유를 적습니다."
    )
    selfTestCases = [
        {
            "name": "함수 안 static T s 를 돌려주는 .cpp 익명 접근자(이름 getInstance 가 아니다)",
            "expectedViolation": "Source/Probe/ProbeCache.cpp:5: [MutableStatic/Local] s_cache",
            "files": {
                "Source/Probe/ProbeCache.cpp": "namespace\n{\n    ProbeCache& shared()\n    {\n        static ProbeCache s_cache;\n        return s_cache;\n    }\n}\n",
            },
        },
        {
            "name": "함수 안 컨테이너 캐시",
            "expectedViolation": "Source/Probe/ProbeVector.cpp:4: [MutableStatic/Local] cache",
            "files": {
                "Source/Probe/ProbeVector.cpp": "void probe()\n{\n    int a = 0;\n    static std::vector<int> cache;\n    cache.push_back( a );\n}\n",
            },
        },
        {
            "name": "함수 안 카운터",
            "expectedViolation": "Source/Probe/ProbeCounter.cpp:3: [MutableStatic/Local] counter",
            "files": {
                "Source/Probe/ProbeCounter.cpp": "void probe()\n{\n    static int counter = 0;\n    ++counter;\n}\n",
            },
        },
        {
            "name": "헤더의 인라인 함수 안 static",
            "expectedViolation": "Source/Probe/ProbeInline.h:3: [MutableStatic/Local] s_registry",
            "files": {
                "Source/Probe/ProbeInline.h": "inline Registry& probe()\n{\n    static Registry s_registry;\n    return s_registry;\n}\n",
            },
        },
        {
            "name": "익명 이름공간의 ScopeOwned 칸",
            "expectedViolation": "Source/Probe/ProbeOwner.cpp:3: [MutableStatic/Namespace] s_listOwner",
            "files": {
                "Source/Probe/ProbeOwner.cpp": "namespace\n{\n    ScopeOwned<vector<int>> s_listOwner;\n}\n",
            },
        },
        {
            "name": "Internal 구조체의 inline static 가변 멤버",
            "expectedViolation": "Source/Probe/ProbeMember.cpp:5: [MutableStatic/Member] s_buffer",
            "files": {
                "Source/Probe/ProbeMember.cpp": "namespace\n{\n    struct ProbeInternal\n    {\n        inline static vector<int> s_buffer{};\n    };\n}\n",
            },
        },
        {
            "name": "thread_local 가변 변수",
            "expectedViolation": "Source/Probe/ProbeThread.cpp:3: [MutableStatic/Namespace] t_depth",
            "files": {
                "Source/Probe/ProbeThread.cpp": "namespace\n{\n    thread_local uint32 t_depth = 0;\n}\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source · Test · Tools 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findMutableStaticViolations(source))

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        listViolation, _ = joinFindings(result.listFindings)
        listCount = [sum(1 for line in listViolation if f"[{category}]" in line) for category in _kListCategory]
        summary = " · ".join(f"{category} {count}" for category, count in zip(_kListCategory, listCount))
        return GateResult(listViolation=listViolation, summary=f"{' · '.join(_kListScanRoot)} — {summary}")


main = CheckMutableStaticsGate.run


if __name__ == "__main__":
    sys.exit(main())
