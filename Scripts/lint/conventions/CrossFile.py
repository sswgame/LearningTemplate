"""
CheckCodeConventions — 파일 하나로는 알 수 없는 검사 — 워커가 파일별 몫(`CrossFileFacts`)을 모으고 부모는 합치기만 한다.

게이트 `Scripts/lint/gate/CheckCodeConventions.py` 의 한 묶음이다(묶음 지도는 `__init__.py`).
"""
from __future__ import annotations

import functools
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from common import kCppAllExtensions, normalizePath
from .Model import ConventionViolation
from .Patterns import _kAnonHelperStructRe, _kBareConstantDeclRe, _kCommentStripRe, _kConstructorDefinitionLineRe, _kStringLiteralStripRe
from .FileScan import checkFileConventionsInternal, readSourceTextInternal, scopedTextCache


# --- 5. 공개 API 및 진입점 ---------------------------------------------------

def reportDuplicateSitesInternal(mapNameToSite: dict[str, list[tuple[str, int]]],
                                 makeViolation: Callable[[str, str, int, list[str]], ConventionViolation]) -> list[ConventionViolation]:
    """같은 이름이 두 파일 이상에 있으면 자리마다 위반 하나 — 다른 파일 목록을 메시지에 넣는다."""
    violations: list[ConventionViolation] = []
    for name, listSite in mapNameToSite.items():
        uniqueFiles = sorted({relPath for relPath, _ in listSite})
        if len(uniqueFiles) < 2:
            continue
        for relPath, lineNum in listSite:
            violations.append(makeViolation(name, relPath, lineNum, [other for other in uniqueFiles if other != relPath]))
    return violations


def reportDuplicateHelperNamesInternal(listFacts: list[CrossFileFacts]) -> list[ConventionViolation]:
    """
    여러 .cpp 가 같은 `XxxInternal` 헬퍼 이름을 쓰는지 검사합니다 (유니티 빌드 재정의 충돌 예방).

    익명 네임스페이스는 **번역 단위** 단위로만 이름을 가립니다. 유니티 빌드는 .cpp 를 묶어 하나의 TU 로 만들므로,
    묶인 파일들이 같은 이름을 쓰면 재정의 오류가 납니다. 파일 단위 검사로는 잡히지 않아 전체 스캔에서만 봅니다.
    """
    mapNameToSite: dict[str, list[tuple[str, int]]] = {}
    for facts in listFacts:
        for helperName, lineNum in facts.listHelperSite:
            mapNameToSite.setdefault(helperName, []).append((facts.relPath, lineNum))

    def makeViolationInternal(helperName: str, relPath: str, lineNum: int, others: list[str]) -> ConventionViolation:
        return ConventionViolation(
            file_path=relPath,
            line_number=lineNum,
            rule_category="Naming/DuplicateInternalHelper",
            message=(f"'{helperName}' 이 다른 .cpp 와 이름이 겹칩니다 ({', '.join(others)}). "
                     "유니티 빌드는 .cpp 를 한 TU 로 묶으므로 익명 네임스페이스라도 재정의로 충돌합니다."),
            snippet=f"struct {helperName}",
            suggested_fix="헬퍼 이름을 클래스가 아니라 **이 TU(파일)** 기준으로 지으세요 (예: VulkanRHIResourceFactoryPipelineInternal).",
        )

    return reportDuplicateSitesInternal(mapNameToSite, makeViolationInternal)



def findBareAnonymousConstantsInternal(content: str) -> list[tuple[str, int]]:
    """
    익명 네임스페이스 **바로 안**에 선언한 상수의 (이름, 줄 번호) 를 모읍니다.

    `XxxInternal` 구조체 안의 상수나 함수 지역 상수는 중괄호가 한 단 더 깊어 잡히지 않는다 — 그쪽은 구조체 이름이나 함수가 가린다.
    """
    listFound: list[tuple[str, int]] = []
    depth = 0
    anonymousDepth: int | None = None
    bPendingAnonymous = False
    bInBlockComment = False
    for lineNum, line in enumerate(content.splitlines(), 1):
        trimmed = line.strip()
        if bInBlockComment:
            if "*/" in trimmed:
                bInBlockComment = False
            continue
        if trimmed.startswith("/*") and "*/" not in trimmed:
            bInBlockComment = True
            continue
        code = _kCommentStripRe.sub("", _kStringLiteralStripRe.sub("", line))
        if anonymousDepth is not None and depth == anonymousDepth and trimmed.startswith("#") is False:
            if constantMatch := _kBareConstantDeclRe.match(code):
                listFound.append((constantMatch.group(1), lineNum))
        if trimmed in ("namespace", "namespace {", "namespace{"):
            bPendingAnonymous = True
        for ch in code:
            if ch == "{":
                depth += 1
                if bPendingAnonymous:
                    anonymousDepth = depth
                    bPendingAnonymous = False
            elif ch == "}":
                if anonymousDepth is not None and depth == anonymousDepth:
                    anonymousDepth = None
                depth -= 1
    return listFound


def reportDuplicateAnonymousConstantsInternal(listFacts: list[CrossFileFacts]) -> list[ConventionViolation]:
    """
    여러 .cpp 가 익명 네임스페이스 바로 안에 같은 이름의 상수를 두는지 검사합니다 (유니티 빌드 재정의 충돌 예방).

    `Naming/DuplicateInternalHelper` 와 같은 이유다 — 유니티 빌드(`CI-*`)가 .cpp 를 한 TU 로 묶으면 익명 네임스페이스끼리 이름이 겹친다.
    `XxxInternal` 구조체 안 상수와 함수 지역 상수는 그 구조체 · 함수가 가리므로 보지 않는다. 파일 짝이 필요해 전체 스캔에서만 돈다.
    """
    mapNameToSite: dict[str, list[tuple[str, int]]] = {}
    for facts in listFacts:
        for constantName, lineNum in facts.listAnonymousConstantSite:
            mapNameToSite.setdefault(constantName, []).append((facts.relPath, lineNum))

    def makeViolationInternal(constantName: str, relPath: str, lineNum: int, others: list[str]) -> ConventionViolation:
        return ConventionViolation(
            file_path=relPath,
            line_number=lineNum,
            rule_category="Naming/DuplicateAnonymousConstant",
            message=(f"익명 네임스페이스의 상수 '{constantName}' 가 다른 .cpp 와 이름이 겹칩니다 ({', '.join(others)}). "
                     "유니티 빌드는 .cpp 를 한 TU 로 묶으므로 재정의로 충돌합니다."),
            snippet=constantName,
            suggested_fix="이 TU 의 `XxxInternal` 구조체 안 `static constexpr` 로 옮기거나, 이름을 이 파일에 맞게 바꾸세요.",
        )

    return reportDuplicateSitesInternal(mapNameToSite, makeViolationInternal)


_kHeaderTypeHeadRe = re.compile(r"^(\s*)(class|struct)\s+(?:SW_\w+\s+|[A-Z]\w*_API\s+)?([A-Z]\w*)\b")
_kHeaderMemberRe = re.compile(
    r"^(?P<indent>\s+)"
    r"(?P<attr>(?:\[\[[^\]]*\]\]\s*|alignas\s*\([^)]*\)\s*|mutable\s+)*)"
    r"(?P<decl>(?!return\b|using\b|typedef\b|friend\b|static\b|enum\b|struct\b|class\b)"
    r"[A-Za-z_][\w:<>,\*&\s]*?)"
    r"\s(?P<name>_\w+)\s*(?P<arr>(?:\[[^\]]*\]\s*)*)"
    r"(?P<bits>:\s*\d+\s*)?"
    r"(?P<init>(?:\{[^{}]*\}|=\s*[^;]+))?\s*;\s*(?P<trail>(?://|/\*).*)?$")


def findCtorInitListClassesInternal(cppContent: str) -> set[str]:
    """`.cpp` 에서 **초기화 리스트를 가진** 생성자를 정의하는 클래스 이름들을 모읍니다 (위임 생성자는 제외)."""
    result: set[str] = set()
    lines = cppContent.splitlines()
    for index, line in enumerate(lines):
        match = _kConstructorDefinitionLineRe.match(line)
        if match is None:
            continue
        className = match.group(1)
        depth, cursor = 0, index
        while cursor < len(lines):
            depth += lines[cursor].count("(") - lines[cursor].count(")")
            if depth <= 0:
                break
            cursor += 1
        tail = lines[cursor].rsplit(")", 1)[-1].strip()
        if tail.startswith("="):
            continue
        listLine = cursor if tail.startswith(":") else cursor + 1
        while listLine < len(lines) and lines[listLine].strip() == "":
            listLine += 1
        if listLine >= len(lines):
            continue
        stripped = lines[listLine].strip()
        if not stripped.startswith(":"):
            continue
        # 위임 생성자(`: ClassName( ... )`)는 멤버 초기화자를 가질 수 없다 — 규칙의 대상이 아니다.
        if re.match(r"^:\s*" + re.escape(className) + r"\s*\(", stripped):
            continue
        result.add(className)
    return result


def checkHeaderMemberInitializersInternal(filesToScan: list[Path], projectRoot: Path) -> list[ConventionViolation]:
    """
    생성자가 초기화 리스트로 멤버를 채우는 클래스가, 헤더에서도 기본값을 주고 있는지 검사합니다.

    한 멤버의 초기값이 두 곳에 적히면 어느 쪽이 이기는지 읽어서는 알 수 없고(생성자가 이긴다), 값을 고칠 때
    한쪽만 고치는 일이 생깁니다. 정본은 하나여야 합니다 — 생성자가 있으면 생성자입니다.
    헤더와 `.cpp` 를 같이 봐야 알 수 있으므로 **전체 스캔에서만** 돕니다.
    """
    violations: list[ConventionViolation] = []
    mapCppToHeader = {}
    for filePath in filesToScan:
        if filePath.suffix.lower() == ".h":
            mapCppToHeader[filePath] = filePath.with_suffix(".cpp")

    for headerPath, cppPath in mapCppToHeader.items():
        if not cppPath.is_file():
            continue
        try:
            headerContent = readSourceTextInternal(headerPath, "utf-8", "ignore")
            cppContent = readSourceTextInternal(cppPath, "utf-8", "ignore")
        except OSError:
            continue
        ctorClasses = findCtorInitListClassesInternal(cppContent)
        if not ctorClasses:
            continue
        try:
            relPath = normalizePath(headerPath.relative_to(projectRoot))
        except ValueError:
            relPath = normalizePath(headerPath)

        # 헤더에서 = default 기본 생성자나 인라인 생성자를 가진 클래스는 헤더 기본값이 유일한 초기화다.
        # `constexpr TaskHandle() = default;` 처럼 지정자가 앞에 붙어도 같은 기본 생성자다 — explicit 만 보다가 놓쳤다.
        exempt: set[str] = set()
        for className in ctorClasses:
            pattern = re.compile(r"^\s*(?:(?:constexpr|explicit|inline)\s+)*" + re.escape(className) + r"\s*\(\s*\)\s*(.*)$", re.M)
            for m in pattern.finditer(headerContent):
                tail = m.group(1).strip()
                if "=default" in tail.replace(" ", "") or tail.startswith("{") or tail.startswith(":"):
                    exempt.add(className)

        stack: list[tuple[str, int]] = []
        depth = 0
        pending: str | None = None
        for lineNum, line in enumerate(headerContent.splitlines(), start=1):
            stripped = line.strip()
            if stripped.startswith(("//", "*", "/*", "#")):
                depth += line.count("{") - line.count("}")
                while stack and depth <= stack[-1][1]:
                    stack.pop()
                continue
            headMatch = _kHeaderTypeHeadRe.match(line)
            if headMatch is not None and not stripped.endswith(";"):
                pending = headMatch.group(3)
            openBraces = line.count("{")
            if openBraces and pending is not None:
                stack.append((pending, depth))
                pending = None
            current = stack[-1][0] if stack else None
            if current in ctorClasses and current not in exempt:
                memberMatch = _kHeaderMemberRe.match(line.rstrip())
                if (memberMatch is not None
                        and "(" not in memberMatch.group("decl")
                        and memberMatch.group("bits") is None
                        and memberMatch.group("init")):
                    violations.append(ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Style/HeaderMemberInitializer",
                        message=(f"'{current}' 는 생성자에서 초기화 리스트를 쓰는데 "
                                 f"멤버 '{memberMatch.group('name')}' 는 헤더에서도 기본값을 줍니다. "
                                 "초기값의 정본은 한 곳이어야 합니다."),
                        snippet=stripped,
                        suggested_fix=f"헤더의 기본값을 지우고 {current} 생성자의 초기화 리스트에 "
                                      "**선언 순서대로** 넣으세요.",
                    ))
            depth += openBraces - line.count("}")
            while stack and depth <= stack[-1][1]:
                stack.pop()
    return violations



# `_b` **다음 글자가 대문자**여야 불리언 이름이다 — 저장소 규칙이 `_bPascalCase` 이기 때문이다.
# 주의: `_b\w+` 로 넓히면 `_buttonMask` · `_bytes` 같은 평범한 이름까지 "uint8 불리언" 으로 읽히고,
# 그 이름이 어딘가에 `uint8` 로 한 번이라도 선언돼 있으면(예: `MouseDevice::_buttonMask`)
# **다른 파일의 `uint64 _buttonMask` 까지** 같이 지적당한다.
_kBoolNamePattern = r"(_b[A-Z]\w*)"
_kU8BoolDeclRe = re.compile(
    r"^\s*(?:\[\[[^\]]*\]\]\s*|mutable\s+|static\s+)*uint8\s+" + _kBoolNamePattern + r"\s*(?::\s*\d+\s*)?"
    r"(?:\{[^{}]*\}|=\s*[^;]+)?\s*;")
_kBoolDeclRe = re.compile(
    r"^\s*(?:\[\[[^\]]*\]\]\s*|mutable\s+|static\s+)*(?:std::)?(?:atomic\s*<\s*bool\s*>|bool)\s+" + _kBoolNamePattern)
# 같은 이름이 **다른 폭의 정수**로도 선언돼 있으면 어느 쪽인지 단정할 수 없다 — `bool` 과 같은 이유로 건너뛴다.
_kWiderIntDeclRe = re.compile(
    r"^\s*(?:\[\[[^\]]*\]\]\s*|mutable\s+|static\s+)*(?:uint(?:16|32|64)|int(?:8|16|32|64))\s+" + _kBoolNamePattern)
_kAnyStringLiteralRe = re.compile(r'"(?:[^"\\]|\\.)*"')


def reportBitfieldBooleanLiteralsInternal(listFacts: list[CrossFileFacts]) -> list[ConventionViolation]:
    """
    `uint8` 불리언 멤버(`_b*`)에 `SW_TRUE`/`SW_FALSE` 대신 생 `1`/`0` 이나 `true`/`false` 를 쓴 자리를 찾습니다.

    `uint8 _bFlag : 1;` 에 `true` 를 대입하면 컴파일러 경고가 날 수 있고, 생 `1` 은 "이게 불리언인가 개수인가" 를
    읽는 사람이 판단해야 한다. 값이 아니라 **의미**를 적는다: `_bFlag = SW_TRUE;` · `if ( _bFlag == SW_TRUE )`.

    선언 타입을 알아야 하므로 **전체 스캔에서만** 돕니다. 같은 이름이 다른 곳에서 `bool` 이나 더 넓은
    정수로도 선언돼 있으면 어느 쪽인지 단정할 수 없으므로 건너뜁니다(오탐보다 누락이 낫다).

    이름은 `_b` **다음이 대문자**여야 봅니다 — 저장소 규칙이 `_bPascalCase` 이기 때문입니다(`_kBoolNamePattern` 의 주의 참고).
    """
    mapU8: set[str] = set()
    mapBool: set[str] = set()
    mapWiderInt: set[str] = set()
    for facts in listFacts:
        mapU8 |= facts.uniqueU8BoolName
        mapBool |= facts.uniqueBoolName
        mapWiderInt |= facts.uniqueWiderIntName

    names = sorted(mapU8 - mapBool - mapWiderInt, key=len, reverse=True)
    if not names:
        return []

    alternation = "|".join(re.escape(n) for n in names)
    patterns = (
        re.compile(r"(?<![A-Za-z0-9_])(" + alternation + r")\s*=(?!=)\s*([01]|true|false)\s*;"),
        re.compile(r"(?<![A-Za-z0-9_])(" + alternation + r")\s*(?:==|!=)\s*([01]|true|false)(?![A-Za-z0-9_.])"),
        re.compile(r"(?<![A-Za-z0-9_])(" + alternation + r")\s*\{\s*([01]|true|false)\s*\}"),
    )

    violations: list[ConventionViolation] = []
    for facts in listFacts:
        for lineNum, line, stripped in facts.listBoolCandidateLine:
            literalSpans = [m.span() for m in _kAnyStringLiteralRe.finditer(line)]
            for pattern in patterns:
                for match in pattern.finditer(line):
                    if any(start <= match.start() < end for start, end in literalSpans):
                        continue  # 문자열 리터럴 안 — 코드가 아니다
                    literal = match.group(2)
                    wanted = "SW_FALSE" if literal in ("0", "false") else "SW_TRUE"
                    violations.append(ConventionViolation(
                        file_path=facts.relPath,
                        line_number=lineNum,
                        rule_category="Style/BitfieldBoolean",
                        message=(f"'{match.group(1)}' 는 uint8 불리언인데 '{literal}' 을(를) 씁니다. "
                                 f"값이 아니라 의미를 적으세요."),
                        snippet=stripped,
                        suggested_fix=f"'{literal}' 대신 {wanted} 를 쓰세요.",
                    ))
    return violations



# --- 생성자가 필드를 빠뜨리지 않는가 (Style/ConstructorInitializesEveryField) ------------------
#
# 기본 초기화가 값을 정하지 않는 필드(정수 · 실수 · bool · 열거형 · 포인터 · 그 배열 · 스칼라 atomic · 비트필드)는 헤더 기본값이나
# 생성자 초기화 목록 중 **한 곳**에 값이 있어야 한다. `-Wreorder-ctor` 와 `Style/ConstructorOrder` 는 목록에 **있는** 필드의 순서만
# 본다 — 목록에서 **빠진** 필드는 아무도 보지 않아 쓰레기 값으로 남는다. 타입을 글자로 판정하므로 모르는 타입(구조체 · 별칭이 여러 뜻인 이름)은
# 건너뛴다(오탐보다 누락이 낫다). 클래스 정의와 생성자 정의가 다른 파일에 있으므로 **전체 스캔에서만** 돈다.

#: 기본 초기화가 값을 정하지 않는 내장 타입 이름.
_kCtorScalarBuiltinName: frozenset[str] = frozenset({
    "bool", "char", "wchar_t", "char8_t", "char16_t", "char32_t", "short", "int", "long", "unsigned", "signed", "float", "double",
    "size_t", "ptrdiff_t", "intptr_t", "uintptr_t", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float32", "float64", "utf8", "utf16", "utf32", "byte",
})
#: 버퍼로 쓰는 배열의 원소 타입 — 이 배열은 일부러 비워 둔다.
_kCtorByteElementName: frozenset[str] = frozenset({"char", "utf8", "uint8", "int8", "byte", "uint8_t", "char8_t"})
_kCtorEnumDeclRe = re.compile(r"\benum\s+(?:class\s+|struct\s+)?([A-Za-z_]\w*)\s*(?::\s*[\w:\s]+?)?\s*[{;]")
_kCtorAliasRe = re.compile(r"\busing\s+([A-Za-z_]\w*)\s*=\s*([^;{}()=]+?)\s*;")
_kCtorTypedefRe = re.compile(r"\btypedef\s+([^;{}()]+?)\s+([A-Za-z_]\w*)\s*;")
_kCtorRecordNameRe = re.compile(r"(?<!enum\s)\b(?:class|struct|union)\s+(?:(?:SW_\w+|alignas\s*\([^)]*\)|\[\[[^\]]*\]\])\s+)*([A-Za-z_]\w*)\s*(?:final\b\s*)?[:{]")
_kCtorClassHeadRe = re.compile(
    r"^\s*(?:template\s*<.*>\s*)?(?:class|struct)\s+(?:(?:SW_\w+|alignas\s*\([^)]*\)|\[\[[^\]]*\]\])\s+)*([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*(?:final\b)?\s*(?::(?!:)[^;{]*)?(?:\{.*)?$")
_kCtorMultiMemberRe = re.compile(r"^\s+(?P<decl>(?!return\b|using\b|typedef\b|static\b|friend\b)[A-Za-z_][\w:<>\s]*?)\s(?P<names>_\w+(?:\s*,\s*_\w+)+)\s*;\s*$")
_kCtorOutOfLineRe = re.compile(
    r"^\s*(?:template\s*<.*>\s*)?(?:(?:inline|constexpr)\s+)*((?:[A-Za-z_]\w*(?:<[^<>]*>)?::)*)([A-Za-z_]\w*)(?:<[^<>]*>)?::(\2)\s*\(")


#: 평범한 코드 사이에서 멈출 자리 — 주석 시작 · 날 문자열 · 따옴표 · 줄바꿈. 그 사이는 덩어리째 옮긴다.
_kCtorStripStopRe = re.compile(r'/\*|//|R"|["\'\n]')
#: 문자열 · 문자 리터럴 본문 — 백슬래시는 다음 글자(줄바꿈 포함)와 함께 건너뛴다. 닫는 따옴표 · 줄바꿈 앞에서 멈춘다.
_kCtorStripDoubleQuoteBodyRe = re.compile(r'(?:\\[\s\S]|[^"\\\n])*')
_kCtorStripSingleQuoteBodyRe = re.compile(r"(?:\\[\s\S]|[^'\\\n])*")


def stripCodeForCtorScanInternal(content: str) -> list[str]:
    """
    주석 · 문자열 · 문자 리터럴을 지운 줄 목록(줄 번호는 그대로). 중괄호 깊이와 초기화 목록을 글자 단위로 세려면 필요하다.

    글자마다 파이썬 루프를 돌지 않고 멈출 자리(`_kCtorStripStopRe`)까지 덩어리째 옮긴다 — 글자 루프는 트리 전체에서 수십 초가
    든다. 날 문자열이 여러 줄이면 그 줄 수만큼 빈 줄을 **앞에** 넣고 닫힌 뒤의 글자는 그 줄에 이어 붙인다(줄 수만 맞춘다).
    """
    listOut: list[str] = []
    current: list[str] = []
    index = 0
    length = len(content)
    while index < length:
        stopMatch = _kCtorStripStopRe.search(content, index)
        if stopMatch is None:
            current.append(content[index:])
            break
        start = stopMatch.start()
        if start > index:
            current.append(content[index:start])
        token = stopMatch.group()
        if token == "\n":
            listOut.append("".join(current))
            current = []
            index = start + 1
        elif token == "/*":
            closeAt = content.find("*/", start + 2)
            end = length if closeAt < 0 else closeAt
            newlineCount = content.count("\n", start + 2, end)
            if newlineCount:
                listOut.append("".join(current))
                current = []
                listOut.extend([""] * (newlineCount - 1))
            index = length if closeAt < 0 else closeAt + 2
        elif token == "//":
            newline = content.find("\n", start)
            index = length if newline < 0 else newline
        elif token == 'R"':
            index = start
            if index == 0 or not (content[index - 1].isalnum() or content[index - 1] == "_"):
                openParen = content.find("(", index + 2)
                if openParen > 0:
                    delimiter = ")" + content[index + 2:openParen] + '"'
                    closeAt = content.find(delimiter, openParen)
                    if closeAt > 0:
                        current.append('""')
                        listOut.extend([""] * content.count("\n", index, closeAt))
                        index = closeAt + len(delimiter)
                        continue
            current.append("R")
            index += 1
        else:
            bodyRe = _kCtorStripDoubleQuoteBodyRe if token == '"' else _kCtorStripSingleQuoteBodyRe
            index = bodyRe.match(content, start + 1).end() + 1
            current.append('""' if token == '"' else "' '")
    listOut.append("".join(current))
    return listOut


#: 줄마다 걸린 전처리 조건 — (조건 글자, 참인 쪽인가) 의 튜플.
CtorConditionStack = tuple[tuple[str, bool], ...]


def blankPreprocessorLinesInternal(listLine: list[str]) -> list[CtorConditionStack]:
    """
    전처리 줄(이어지는 `\\` 줄 포함)을 비우고, 줄마다 걸린 `#if` 조건을 돌려준다. 매크로 본문의 중괄호가 깊이를 흔들지 않게 하고,
    `#if A` 의 필드를 `#else` 쪽 생성자에 요구하지 않으려는 것이다(`#elif` 는 가지마다 다른 조건으로 본다).
    """
    listStack: list[CtorConditionStack] = []
    stack: list[tuple[str, bool]] = []
    bContinuation = False
    for lineIndex, line in enumerate(listLine):
        stripped = line.strip()
        if bContinuation or stripped.startswith("#"):
            if bContinuation is False:
                directive = re.match(r"#\s*(\w+)\s*(.*)$", stripped)
                keyword = directive.group(1) if directive else ""
                condition = "".join((directive.group(2) if directive else "").rstrip("\\").split())
                if keyword == "if":
                    stack.append((condition, True))
                elif keyword == "ifdef":
                    stack.append((f"defined({condition})", True))
                elif keyword == "ifndef":
                    stack.append((f"defined({condition})", False))
                elif keyword == "elif" and stack:
                    stack[-1] = (f"elif:{condition}", True)
                elif keyword == "else" and stack:
                    stack[-1] = (stack[-1][0], stack[-1][1] is False)
                elif keyword == "endif" and stack:
                    stack.pop()
            bContinuation = stripped.endswith("\\")
            listLine[lineIndex] = ""
        listStack.append(tuple(stack))
    return listStack


def areConditionsCompatibleInternal(first: CtorConditionStack, second: CtorConditionStack) -> bool:
    """두 줄이 한 구성에서 함께 컴파일될 수 있는가 — 같은 조건을 반대 쪽으로 쓰고 있으면 아니다."""
    mapConditionToPolarity = dict(first)
    return all(mapConditionToPolarity.get(condition, polarity) == polarity for condition, polarity in second)


@dataclass
class CtorScanMember:
    name: str
    typeText: str
    lineNum: int
    bHasDefault: bool
    bIsBitfield: bool
    bIsArray: bool
    bIsRawStorage: bool
    conditions: CtorConditionStack = ()


@dataclass
class CtorScanConstructor:
    lineNum: int
    relPath: str
    kind: str                     # "definition" · "defaulted" · "declaration" · "deleted"
    bIsDefaultConstructor: bool
    bDelegating: bool
    uniqueInitialized: set[str]
    conditions: CtorConditionStack = ()


@dataclass
class CtorScanClass:
    chain: tuple[str, ...]        # 바깥 클래스부터의 이름
    relPath: str
    listMember: list[CtorScanMember] = field(default_factory=list)
    listConstructor: list[CtorScanConstructor] = field(default_factory=list)


@dataclass
class CrossFileFacts:
    """
    교차 파일 검사 다섯이 파일 하나에서 쓰는 것 — 워커가 파일별 검사와 함께 만들고, 부모가 파일 순서대로 합친다(프로세스 사이로 피클된다).

    파일을 읽고 줄을 훑는 일은 파일마다 따로라 워커에서 돈다. 부모에 남는 것은 이름 표 합치기 · 클래스 밖 생성자 붙이기뿐이다.
    """

    relPath: str
    listHelperSite: list[tuple[str, int]] = field(default_factory=list)
    listAnonymousConstantSite: list[tuple[str, int]] = field(default_factory=list)
    listHeaderInitializerViolation: list[ConventionViolation] = field(default_factory=list)
    uniqueU8BoolName: set[str] = field(default_factory=set)
    uniqueBoolName: set[str] = field(default_factory=set)
    uniqueWiderIntName: set[str] = field(default_factory=set)
    listBoolCandidateLine: list[tuple[int, str, str]] = field(default_factory=list)   # (줄 번호, 줄, strip 한 줄)
    bHasCtorScan: bool = False
    listCtorClass: list[CtorScanClass] = field(default_factory=list)
    listCtorOutOfLine: list[tuple[tuple[str, ...], CtorScanConstructor]] = field(default_factory=list)
    listEnumName: list[str] = field(default_factory=list)
    listRecordName: list[str] = field(default_factory=list)
    listAlias: list[tuple[str, str]] = field(default_factory=list)


#: 비트필드 불리언 2 단계가 볼 수 있는 줄 — 세 정규식 모두 `(?<![A-Za-z0-9_])` 뒤 `_b[A-Z]` 로 시작하는 이름을 요구한다(그 상위 집합).
_kBoolNameStartRe = re.compile(r"(?<![A-Za-z0-9_])_b[A-Z]")


def relPathOfInternal(filePath: Path, projectRoot: Path) -> str:
    try:
        return normalizePath(filePath.relative_to(projectRoot))
    except ValueError:
        return normalizePath(filePath)


def collectCrossFileFactsInternal(filePath: Path, projectRoot: Path) -> CrossFileFacts:
    """파일 하나에서 교차 파일 검사 다섯이 쓰는 것을 모읍니다. 읽기 · 디코드는 검사들과 같다(`utf-8` · `ignore`)."""
    facts = CrossFileFacts(relPath=relPathOfInternal(filePath, projectRoot))
    suffix = filePath.suffix.lower()
    try:
        content = readSourceTextInternal(filePath, "utf-8", "ignore")
    except OSError:
        return facts

    if suffix == ".cpp":
        for match in _kAnonHelperStructRe.finditer(content):
            facts.listHelperSite.append((match.group(1), content.count("\n", 0, match.start()) + 1))
        if "namespace" in content:
            facts.listAnonymousConstantSite = findBareAnonymousConstantsInternal(content)

    # 헤더와 짝 .cpp 둘만 보는 검사라 파일 하나 안에서 끝난다.
    if suffix == ".h":
        facts.listHeaderInitializerViolation = checkHeaderMemberInitializersInternal([filePath], projectRoot)

    if "_b" in content:
        for lineNum, line in enumerate(content.splitlines(), start=1):
            if "_b" not in line:
                continue
            stripped = line.strip()
            if stripped.startswith(("//", "*", "/*")):
                continue
            if match := _kU8BoolDeclRe.match(line):
                facts.uniqueU8BoolName.add(match.group(1))
            elif match := _kBoolDeclRe.match(line):
                facts.uniqueBoolName.add(match.group(1))
            elif match := _kWiderIntDeclRe.match(line):
                facts.uniqueWiderIntName.add(match.group(1))
            if _kBoolNameStartRe.search(line):
                facts.listBoolCandidateLine.append((lineNum, line, stripped))

    if suffix in kCppAllExtensions:
        listLine = stripCodeForCtorScanInternal(content)
        listCondition = blankPreprocessorLinesInternal(listLine)
        text = "\n".join(listLine)
        facts.bHasCtorScan = True
        facts.listEnumName = _kCtorEnumDeclRe.findall(text)
        facts.listRecordName = _kCtorRecordNameRe.findall(text)
        facts.listAlias = _kCtorAliasRe.findall(text) + [(name, target) for target, name in _kCtorTypedefRe.findall(text)]
        facts.listCtorClass, facts.listCtorOutOfLine = scanCtorFileInternal(facts.relPath, listLine, listCondition)
    return facts


def checkFileAndCollectFactsInternal(filePath: Path, rootDir: Path) -> list[tuple[list[ConventionViolation], CrossFileFacts]]:
    """
    전체 스캔의 워커가 파일 하나에 하는 일 전부 — 파일별 규칙 + 교차 파일 사실. 파일은 한 번만 읽는다(`SourceTextCache`).
    프로세스로 넘기려고 모듈 최상위에 둔다. 이 프로세스에서 돌 때(파일이 적을 때)는 부모의 캐시를 잠깐 비켜 두었다가 되돌린다.
    """
    with scopedTextCache():
        return [(checkFileConventionsInternal(filePath, rootDir), collectCrossFileFactsInternal(filePath, rootDir))]


def findMatchingCloseInternal(text: str, openIndex: int) -> int:
    """`text[openIndex]` 의 `(` · `{` · `<` 짝이 닫히는 자리(없으면 -1)."""
    pairs = {"(": ")", "{": "}", "[": "]"}
    stack: list[str] = []
    for index in range(openIndex, len(text)):
        character = text[index]
        if character in pairs:
            stack.append(pairs[character])
        elif stack and character == stack[-1]:
            stack.pop()
            if not stack:
                return index
        elif character in ")}]":
            return -1
    return -1


def parseConstructorTailInternal(text: str, openParenIndex: int, className: str) -> tuple[str, bool, set[str]] | None:
    """
    생성자 이름 뒤 `(` 부터 읽어 (종류, 위임 생성자인가, 초기화 목록의 이름들) 을 돌려준다. 모양을 모르면 None.
    """
    closeParen = findMatchingCloseInternal(text, openParenIndex)
    if closeParen < 0:
        return None
    cursor = closeParen + 1
    tailMatch = re.compile(r"\s*(?:noexcept\s*(?:\([^()]*\))?\s*|SW_\w+\s*)*").match(text, cursor)
    cursor = tailMatch.end() if tailMatch else cursor
    rest = text[cursor:].lstrip()
    if rest.startswith(";"):
        return ("declaration", False, set())
    if re.match(r"=\s*default\b", rest):
        return ("defaulted", False, set())
    if re.match(r"=\s*delete\b", rest):
        return ("deleted", False, set())
    if rest.startswith("{"):
        return ("definition", False, collectBodyAssignmentsInternal(text, text.index("{", cursor)))
    if not rest.startswith(":"):
        return None
    cursor = text.index(":", cursor) + 1
    uniqueName: set[str] = set()
    bDelegating = False
    bFirst = True
    nameRe = re.compile(r"\s*((?:[A-Za-z_]\w*(?:<[^<>]*>)?::)*[A-Za-z_]\w*)(?:<[^<>]*>)?\s*")
    while True:
        nameMatch = nameRe.match(text, cursor)
        if nameMatch is None:
            return None
        name = nameMatch.group(1).split("::")[-1]
        cursor = nameMatch.end()
        if cursor >= len(text) or text[cursor] not in "({":
            return None
        closeAt = findMatchingCloseInternal(text, cursor)
        if closeAt < 0:
            return None
        if bFirst and name == className:
            bDelegating = True
        bFirst = False
        uniqueName.add(name)
        cursor = closeAt + 1
        while cursor < len(text) and text[cursor] in " \t\r\n":
            cursor += 1
        # `#if` 가지마다 목록을 `:` 로 다시 여는 생성자(`FunctionMetadata`)도 있다 — 전처리 줄을 비운 글자에서는 `:` 가 이음표로 보인다.
        if cursor < len(text) and text[cursor] in ",:":
            cursor += 1
            continue
        if cursor < len(text) and text[cursor] == "{":
            return ("definition", bDelegating, uniqueName | collectBodyAssignmentsInternal(text, cursor))
        return None


_kCtorBodyAssignmentRe = re.compile(r"(?<![\w.>:])(_[A-Za-z]\w*)\s*(?:=(?!=)|\.store\s*\()")


def collectBodyAssignmentsInternal(text: str, openBraceIndex: int) -> set[str]:
    """생성자 본문에서 값을 대입하는 필드 — 원본 객체를 잠근 뒤 옮기는 이동 생성자처럼 목록에 둘 수 없는 경우를 받는다."""
    closeBrace = findMatchingCloseInternal(text, openBraceIndex)
    body = text[openBraceIndex:closeBrace if closeBrace > 0 else len(text)]
    return set(_kCtorBodyAssignmentRe.findall(body))


def isDefaultConstructorParameterListInternal(text: str, openParenIndex: int) -> bool:
    closeParen = findMatchingCloseInternal(text, openParenIndex)
    if closeParen < 0:
        return False
    inner = text[openParenIndex + 1:closeParen].strip()
    return inner in ("", "void")


@functools.lru_cache(maxsize=None)
def compileInClassCtorReInternal(className: str) -> re.Pattern:
    """클래스 본문 안의 생성자 선언 머리(`explicit Name(`). 클래스 이름이 수천 개라 `re` 모듈 캐시(512)로는 다시 컴파일된다 — 이름마다 한 번 만든다."""
    return re.compile(r"^\s*(?:(?:explicit|constexpr|inline|SW_\w+)\s+)*(?:explicit\s*\([^)]*\)\s*)?" + re.escape(className) + r"\s*\(")


def scanCtorFileInternal(relPath: str, listLine: list[str], listCondition: list[CtorConditionStack]) -> tuple[list[CtorScanClass], list[tuple[tuple[str, ...], CtorScanConstructor]]]:
    """
    파일 하나에서 클래스(멤버 · 클래스 안 생성자)와 클래스 밖 생성자 정의(`A::B::B(`)를 모은다.
    """
    listClass: list[CtorScanClass] = []
    listOutOfLine: list[tuple[tuple[str, ...], CtorScanConstructor]] = []
    classStack: list[tuple[CtorScanClass, int]] = []    # (클래스, 여는 중괄호 앞 깊이)
    depth = 0
    pendingName: str | None = None

    for lineIndex, line in enumerate(listLine):
        lineNum = lineIndex + 1
        stripped = line.strip()
        depthAtStart = depth

        if stripped:
            headMatch = _kCtorClassHeadRe.match(line) if ("class" in line or "struct" in line) else None
            if headMatch is not None and not stripped.endswith(";") and not re.match(r"^\s*enum\b", line) \
                    and not re.search(r"\bfriend\b", line):
                pendingName = headMatch.group(1)

            topClass = classStack[-1][0] if classStack else None
            bAtMemberDepth = topClass is not None and depthAtStart == classStack[-1][1] + 1
            if bAtMemberDepth:
                className = topClass.chain[-1]
                ctorMatch = compileInClassCtorReInternal(className).match(line) if className in line else None
                if ctorMatch is not None:
                    joined = "\n".join(listLine[lineIndex:lineIndex + 80])
                    openParen = ctorMatch.end() - 1
                    parsed = parseConstructorTailInternal(joined, openParen, className)
                    if parsed is not None:
                        kind, bDelegating, uniqueInit = parsed
                        topClass.listConstructor.append(CtorScanConstructor(
                            lineNum, relPath, kind, isDefaultConstructorParameterListInternal(joined, openParen), bDelegating, uniqueInit,
                            listCondition[lineIndex]))
                elif line.count("{") == line.count("}"):
                    multiMatch = _kCtorMultiMemberRe.match(line.rstrip())
                    if multiMatch is not None:
                        # `float32 _31, _32, _33, _34;` — 한 줄에 여럿을 선언한 필드.
                        for name in re.findall(r"_\w+", multiMatch.group("names")):
                            topClass.listMember.append(CtorScanMember(
                                name=name, typeText=multiMatch.group("decl").strip(), lineNum=lineNum, bHasDefault=False,
                                bIsBitfield=False, bIsArray=False, bIsRawStorage=False, conditions=listCondition[lineIndex]))
                    memberMatch = _kHeaderMemberRe.match(line.rstrip()) if multiMatch is None else None
                    if memberMatch is not None and "(" not in memberMatch.group("decl") and "operator" not in memberMatch.group("decl"):
                        decl = memberMatch.group("decl").strip()
                        attr = memberMatch.group("attr") or ""
                        listWord = decl.split()
                        bStatic = any(word in ("static", "constexpr", "typedef", "using", "return") for word in listWord)
                        bIsArray = bool((memberMatch.group("arr") or "").strip())
                        # 바이트 배열은 버퍼다 — 쓰는 쪽이 길이와 함께 채운다(`StringBuilder::_arrStaticBuffer`). 0 으로 미리 채우면 낭비다.
                        bIsByteBuffer = bIsArray and listWord and listWord[-1].split("::")[-1] in _kCtorByteElementName
                        if not bStatic:
                            topClass.listMember.append(CtorScanMember(
                                name=memberMatch.group("name"),
                                typeText=decl,
                                lineNum=lineNum,
                                bHasDefault=bool(memberMatch.group("init")),
                                bIsBitfield=memberMatch.group("bits") is not None,
                                bIsArray=bIsArray,
                                bIsRawStorage=bool(bIsByteBuffer) or (bIsArray and ("alignas" in attr or "alignas" in decl)),
                                conditions=listCondition[lineIndex],
                            ))
            elif not classStack or depthAtStart <= classStack[-1][1]:
                outMatch = _kCtorOutOfLineRe.match(line) if "::" in line else None
                if outMatch is not None:
                    chain = tuple(part.split("<")[0] for part in outMatch.group(1).split("::") if part) + (outMatch.group(2),)
                    joined = "\n".join(listLine[lineIndex:lineIndex + 80])
                    openParen = outMatch.end() - 1
                    parsed = parseConstructorTailInternal(joined, openParen, outMatch.group(2))
                    if parsed is not None:
                        kind, bDelegating, uniqueInit = parsed
                        listOutOfLine.append((chain, CtorScanConstructor(
                            lineNum, relPath, kind, isDefaultConstructorParameterListInternal(joined, openParen), bDelegating, uniqueInit,
                            listCondition[lineIndex])))

        if "{" not in line and "}" not in line:
            # 중괄호가 없는 줄은 깊이가 그대로라 `;` 하나만 본다(아래 글자 루프와 같은 판정).
            if pendingName is not None and ";" in line:
                pendingName = None
            continue
        for character in line:
            if character == "{":
                if pendingName is not None:
                    outerChain = classStack[-1][0].chain if classStack else ()
                    # `struct Outer::Impl` 처럼 바깥 이름을 붙여 정의하면 그 이름들이 사슬이 된다(pImpl).
                    scanned = CtorScanClass(chain=outerChain + tuple(pendingName.split("::")), relPath=relPath)
                    listClass.append(scanned)
                    classStack.append((scanned, depth))
                    pendingName = None
                depth += 1
            elif character == "}":
                depth -= 1
                while classStack and depth <= classStack[-1][1]:
                    classStack.pop()
            elif character == ";" and pendingName is not None and depth == depthAtStart:
                pendingName = None
    return listClass, listOutOfLine


def collectCtorScalarNamesInternal(listFacts: list[CrossFileFacts]) -> set[str]:
    """
    기본 초기화가 값을 정하지 않는 타입 이름 — 내장 타입 · 열거형 · 그것들의 별칭. 같은 이름이 레코드로도 선언돼 있으면 뺀다.
    """
    uniqueEnum: set[str] = set()
    uniqueRecord: set[str] = set()
    listAlias: list[tuple[str, str]] = []
    for facts in listFacts:
        if facts.bHasCtorScan is False:
            continue
        uniqueEnum.update(facts.listEnumName)
        uniqueRecord.update(facts.listRecordName)
        listAlias.extend(facts.listAlias)

    uniqueScalar = set(_kCtorScalarBuiltinName) | uniqueEnum
    mapAliasToTarget: dict[str, set[str]] = {}
    for name, target in listAlias:
        mapAliasToTarget.setdefault(name, set()).add(" ".join(target.split()))
    bChanged = True
    while bChanged:
        bChanged = False
        for name, uniqueTarget in mapAliasToTarget.items():
            if name in uniqueScalar:
                continue
            if all(isCtorScalarTypeTextInternal(target, uniqueScalar) for target in uniqueTarget):
                uniqueScalar.add(name)
                bChanged = True
    # 열거형 이름이 레코드 이름과 겹치면(다른 네임스페이스의 같은 이름) 어느 쪽인지 단정할 수 없다.
    return uniqueScalar - (uniqueRecord - set(_kCtorScalarBuiltinName))


def isCtorScalarTypeTextInternal(typeText: str, uniqueScalar: set[str]) -> bool:
    """선언의 타입 부분이 기본 초기화로 값이 정해지지 않는 타입인가."""
    text = re.sub(r"\b(?:const|volatile|mutable|inline|typename|struct|class|enum)\b", " ", typeText).strip()
    angleDepth = 0
    for character in text:
        if character == "<":
            angleDepth += 1
        elif character == ">":
            angleDepth -= 1
        elif character == "*" and angleDepth == 0:
            return True
    if "&" in text:
        return False
    templateMatch = re.match(r"^(?:std::|sw::)?atomic\s*<\s*(.+)\s*>$", text)
    if templateMatch is not None:
        return isCtorScalarTypeTextInternal(templateMatch.group(1), uniqueScalar)
    if "<" in text:
        return False
    listWord = text.split()
    if not listWord:
        return False
    return all(word.split("::")[-1] in uniqueScalar for word in listWord)


def reportConstructorInitializesEveryFieldInternal(filesToScan: list[Path], listFacts: list[CrossFileFacts]) -> list[ConventionViolation]:
    """
    생성자가 있는 클래스에서 값이 정해지지 않는 필드(스칼라 · 포인터 · 열거형 · 비트필드)가 헤더 기본값도, 생성자 초기화 목록도 없는지 검사합니다.

    - 초기화 목록을 가진 생성자(복사 · 이동 생성자 포함, 위임 생성자 제외)는 그런 필드를 **전부** 목록에 둬야 한다.
    - `X() = default` 인 클래스는 그런 필드에 헤더 기본값이 있어야 한다 — 비트필드는 헤더 기본값을 가질 수 없으니 생성자를 쓴다.
    `alignas` 를 단 원시 저장 배열(인라인 버퍼)은 일부러 비워 둔 것이라 보지 않는다.
    """
    uniqueScalar = collectCtorScalarNamesInternal(listFacts)

    # 폴더 → (파일 이름 줄기 → 클래스). 클래스 밖 생성자는 같은 폴더의 줄기만 보므로 폴더로 먼저 가른다 — 트리 전체 열쇠를 생성자마다
    # 훑으면 생성자 수 × 파일 수다.
    mapDirToStemClass: dict[Path, dict[str, list[CtorScanClass]]] = {}
    listOutOfLineSite: list[tuple[Path, tuple[str, ...], CtorScanConstructor]] = []
    listAllClass: list[CtorScanClass] = []
    for filePath, facts in zip(filesToScan, listFacts):
        if facts.bHasCtorScan is False:
            continue
        listAllClass.extend(facts.listCtorClass)
        mapDirToStemClass.setdefault(filePath.parent, {}).setdefault(filePath.stem, []).extend(facts.listCtorClass)
        listOutOfLineSite.extend((filePath, chain, constructor) for chain, constructor in facts.listCtorOutOfLine)

    # 클래스 밖 생성자를 그 클래스에 붙인다: 같은 파일, 또는 같은 폴더에서 이름이 파일 이름의 앞부분인 헤더(FrameRendererCompute.cpp → FrameRenderer.h).
    for filePath, chain, constructor in listOutOfLineSite:
        listCandidate: list[CtorScanClass] = []
        fileStem = filePath.stem
        for stem, listClass in mapDirToStemClass.get(filePath.parent, {}).items():
            if fileStem.startswith(stem) is False:
                continue
            listCandidate.extend(scanned for scanned in listClass if scanned.chain[-len(chain):] == chain)
        if not listCandidate:
            # 정의가 다른 이름의 파일에 있다(RHITypes.h 의 `RHITextureDesc` → RHI.cpp). 트리 전체에서 그 이름이 하나뿐일 때만 붙인다.
            listCandidate = [scanned for scanned in listAllClass if scanned.chain[-len(chain):] == chain]
            if len(listCandidate) != 1:
                continue
        # 같은 헤더 묶음 안의 같은 이름은 `#if` 가지마다 다시 적은 같은 클래스다(D3D12RHIDevice 의 비 Windows 껍데기) — 모두에 붙인다.
        for scanned in listCandidate:
            scanned.listConstructor.append(constructor)

    violations: list[ConventionViolation] = []
    for scanned in listAllClass:
        if not scanned.listConstructor:
            continue
        listNeedsInit = [member for member in scanned.listMember
                         if member.bHasDefault is False and member.bIsRawStorage is False
                         and (member.bIsBitfield or isCtorScalarTypeTextInternal(member.typeText, uniqueScalar))]
        if not listNeedsInit:
            continue
        qualifiedName = "::".join(scanned.chain)
        for constructor in scanned.listConstructor:
            if constructor.kind == "definition" and constructor.bDelegating is False:
                listMissing = [member.name for member in listNeedsInit
                               if member.name not in constructor.uniqueInitialized
                               and areConditionsCompatibleInternal(member.conditions, constructor.conditions)]
                if listMissing:
                    violations.append(ConventionViolation(
                        file_path=constructor.relPath,
                        line_number=constructor.lineNum,
                        rule_category="Style/ConstructorInitializesEveryField",
                        message=(f"'{qualifiedName}' 생성자가 기본값 없는 필드 {', '.join(repr(name) for name in listMissing)} 를 "
                                 "초기화 목록에 두지 않습니다. 그 필드는 쓰레기 값으로 시작합니다."),
                        snippet=qualifiedName,
                        suggested_fix="빠진 필드를 초기화 목록에 **선언 순서대로** 넣으세요(본문 대입이 아니라 목록).",
                    ))
            elif constructor.kind == "defaulted" and constructor.bIsDefaultConstructor:
                for member in listNeedsInit:
                    if areConditionsCompatibleInternal(member.conditions, constructor.conditions) is False:
                        continue
                    violations.append(ConventionViolation(
                        file_path=scanned.relPath,
                        line_number=member.lineNum,
                        rule_category="Style/ConstructorInitializesEveryField",
                        message=(f"'{qualifiedName}' 의 기본 생성자는 `= default` 인데 '{member.name}' 에 헤더 기본값이 없습니다. "
                                 "그 필드는 쓰레기 값으로 시작합니다."),
                        snippet=member.name,
                        suggested_fix=("헤더 기본값을 주세요." if member.bIsBitfield is False
                                       else "비트필드는 헤더 기본값을 가질 수 없습니다 — 생성자를 쓰고 초기화 목록에 넣으세요."),
                    ))
    return violations
