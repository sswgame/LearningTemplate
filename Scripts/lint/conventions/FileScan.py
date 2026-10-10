"""
CheckCodeConventions — 파일 하나를 훑어 범위별 규칙(`Model._kRulesByScope`)을 돌린다 — 파일마다 한 번 읽는 캐시(`scopedTextCache`)와 짝 헤더 멤버 읽기.

게이트 `Scripts/lint/gate/CheckCodeConventions.py` 의 한 묶음이다(묶음 지도는 `__init__.py`).
"""
from __future__ import annotations

import contextlib
import functools
import re
from pathlib import Path
from typing import Iterator

from common import kCppHeaderExtensions, kCppSourceExtensions, normalizePath
from . import LineRules  # noqa: F401 — 규칙 클래스는 import 될 때 `_kRulesByScope` 에 오른다(워커 프로세스도 이 모듈을 거친다)
from .Model import _kRulesByScope, ConventionViolation, LineScanContext
from .Patterns import (
    _kClassDeclRe,
    _kCommentStripRe,
    _kConstructorDefinitionRe,
    _kConstructorInitRe,
    _kConstructorSignatureRe,
    _kFunctionSignatureRe,
    _kLambdaParameterRe,
    _kStringLiteralStripRe,
    extractClassMembersInternal,
)
from .NamingChecks import splitParametersInternal


# --- 4. 파일별 컨벤션 검사 로직 ----------------------------------------------

class SourceTextCache:
    """
    검사 한 번 동안 **파일마다 한 번만 읽는다.** 바이트를 들고 있다가 자리마다 원하는 인코딩 · 오류 처리로 풉니다.

    파일별 검사 · 짝 헤더 멤버 · 헬퍼 이름 중복 · 헤더 멤버 초기값 · 비트필드 불리언이 같은 파일을 각자 열면 파일마다 네다섯 번
    열게 된다(윈도우는 여는 것이 느리다 — 필터 드라이버). 풀 때는 `Path.read_text` 와 같게 줄끝(CRLF · CR)을 LF 로 바꾼다
    — 텍스트 모드 읽기가 하는 일이다.
    """

    def __init__(self) -> None:
        self._mapBytes: dict[Path, bytes] = {}

    def readText(self, path: Path, encoding: str, errors: str = "strict") -> str:
        data = self._mapBytes.get(path)
        if data is None:
            data = path.read_bytes()
            self._mapBytes[path] = data
        return data.decode(encoding, errors).replace("\r\n", "\n").replace("\r", "\n")


#: `scopedTextCache` 안에서만 있다. 없으면 파일을 그냥 읽는다(다른 곳에서 함수를 따로 부를 때).
_s_textCache: SourceTextCache | None = None


@contextlib.contextmanager
def scopedTextCache() -> Iterator[None]:
    """이 블록 동안 파일을 한 번만 읽는다(`SourceTextCache`). 블록을 나가면 앞 캐시로 돌아간다 — 겹쳐 불러도 된다."""
    global _s_textCache
    previousCache = _s_textCache
    _s_textCache = SourceTextCache()
    try:
        yield
    finally:
        _s_textCache = previousCache


def readSourceTextInternal(path: Path, encoding: str, errors: str = "strict") -> str:
    """`Path.read_text` 와 같은 결과를, 검사가 도는 중이면 캐시에서."""
    cache = _s_textCache
    if cache is None:
        return path.read_text(encoding=encoding, errors=errors)
    return cache.readText(path, encoding, errors)


@functools.lru_cache(maxsize=None)
def readHeaderClassMembersInternal(headerPath: Path) -> dict:
    """
    짝 헤더의 클래스 멤버 맵. 헤더는 자기 차례에도 스캔되므로 캐시가 없으면 두 번 읽고 두 번 판다.

    읽지 못하면(`OSError` · `UnicodeDecodeError`) 예외를 그대로 낸다 — 부르는 쪽이 위반으로 알린다(빈 맵이면 생성자 순서 검사가 조용히 꺼진다).
    """
    return extractClassMembersInternal(readSourceTextInternal(headerPath, "utf-8"))


#: 줄 안에서 블록 주석 상태를 바꾸거나 막는 자리 — 문자열 · 문자 리터럴(그 안의 `/*` 는 주석이 아니다), `//`, `/*`.
_kBlockCommentTokenRe = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//|/\*')


def endsInsideBlockCommentInternal(line: str, bInsideAtStart: bool) -> bool:
    """
    줄 하나를 왼쪽부터 읽어, 줄이 끝날 때 블록 주석 안인지 돌려줍니다(`bInsideAtStart` 는 줄을 시작할 때의 상태).

    주석 밖에서는 문자열 · 문자 리터럴을 건너뛴다 — `"a/*b"` 의 `/*` 를 주석 시작으로 읽으면 그 뒤 `*/` 가 나올 때까지 모든 줄이
    검사에서 빠진다. `//` 뒤는 줄 끝까지 주석이다. 주석 안에서는 `*/` 만 찾는다(주석 안의 따옴표는 문자열이 아니다).
    """
    index = 0
    bInside = bInsideAtStart
    while True:
        if bInside:
            closeAt = line.find("*/", index)
            if closeAt < 0:
                return True
            bInside = False
            index = closeAt + 2
        tokenMatch = _kBlockCommentTokenRe.search(line, index)
        if tokenMatch is None:
            return False
        token = tokenMatch.group()
        if token == "//":
            return False
        bInside = token == "/*"
        index = tokenMatch.end()


@functools.lru_cache(maxsize=None)
def compileHeaderCtorReInternal(className: str) -> re.Pattern:
    """헤더 클래스 본문의 생성자 선언(`Name(...)`). 클래스 이름이 수천 개라 `re` 모듈 캐시(512)로는 줄마다 다시 컴파일된다."""
    return re.compile(rf'\b{className}\s*\([^)]*\)')


def checkFileConventionsInternal(filePath: Path, rootDir: Path) -> list[ConventionViolation]:
    violations: list[ConventionViolation] = []
    relPath = normalizePath(filePath.relative_to(rootDir))
    isHeader = filePath.suffix.lower() in kCppHeaderExtensions
    isSource = filePath.suffix.lower() in kCppSourceExtensions

    try:
        content = readSourceTextInternal(filePath, "utf-8-sig")
    except UnicodeDecodeError:
        try:
            content = readSourceTextInternal(filePath, "latin-1")
        except Exception:
            return violations

    lines = content.splitlines()

    # 클래스 멤버 변수 선언 순서 맵 로드 (현재 파일 및 매칭되는 헤더 파일)
    classMemberMap = extractClassMembersInternal(content)
    if isSource:
        matchingHeader = filePath.with_suffix(".h")
        if not matchingHeader.is_file():
            matchingHeader = filePath.with_suffix(".hpp")
        if matchingHeader.is_file():
            try:
                classMemberMap.update(readHeaderClassMembersInternal(matchingHeader))
            except (OSError, UnicodeDecodeError) as error:
                # 헤더를 못 읽으면 생성자 순서 검사가 이 파일에서 성립하지 않는다 — 통과로 두지 않고 위반으로 알린다.
                violations.append(ConventionViolation(
                    file_path=relPath, line_number=1, rule_category="Style/ConstructorOrder",
                    message=f"짝 헤더 {matchingHeader.name} 를 읽지 못해 생성자 순서를 검사할 수 없습니다 ({error}) — 헤더를 UTF-8 로 저장하십시오",
                    snippet=matchingHeader.name))

    # 1. 파일 단위 규칙
    fileContext = LineScanContext(relPath=relPath, rootDir=rootDir, lineNum=0, line="", trimmed="", codeWithoutStrings="",
                                  isHeader=isHeader, isSource=isSource)
    for rule in _kRulesByScope["file"]:
        violations.extend(rule.onFile(fileContext, lines))

    # 2. 줄 단위 규칙 검사
    inBlockComment = False
    currentCtorClass: str | None = None
    ctorInitMembers: list[str] = []
    ctorInitStartLine: int = 0
    inCtorInitList = False

    classStack: list[tuple[str, int]] = []  # (className, entryBraceDepth)
    funcStack: list[int] = []               # entryBraceDepth
    braceDepth = 0
    pendingClass: str | None = None
    pendingFunc = False

    for lineNum, line in enumerate(lines, start=1):
        trimmed = line.strip()

        # 블록 주석 처리 — 주석을 여는 줄 · 닫는 줄은 통째로 건너뛴다. 문자열 · `//` 안의 `/*` 는 주석을 열지 않는다.
        if inBlockComment:
            inBlockComment = endsInsideBlockCommentInternal(trimmed, True)
            continue
        if "/*" in trimmed and endsInsideBlockCommentInternal(trimmed, False):
            inBlockComment = True
            continue
        if trimmed.startswith("//") or not trimmed:
            continue

        codeWithoutStrings = _kStringLiteralStripRe.sub('', line)
        # 주석도 코드가 아니다. 이걸 안 지우면 "float 판은 …" 같은 산문이 타입 사용으로 잡힌다
        # (실제로 ZoneTracker.h 의 한국어 주석이 Style/BasicTypeAlias 로 신고됐다).
        codeWithoutStrings = _kCommentStripRe.sub("", codeWithoutStrings)

        scanContext = LineScanContext(
            relPath=relPath,
            rootDir=rootDir,
            lineNum=lineNum,
            line=line,
            trimmed=trimmed,
            codeWithoutStrings=codeWithoutStrings,
            isHeader=isHeader,
            isSource=isSource,
        )
        for rule in _kRulesByScope["line"]:
            violations.extend(rule.onLine(scanContext))

        # class / struct 선언 감지
        if classMatch := _kClassDeclRe.search(trimmed):
            if not trimmed.endswith(";"):
                pendingClass = classMatch.group(1)

        # 함수 선언 / 정의 감지
        isFnSig = False
        sigParamStr = None
        if not trimmed.startswith(("#", "//", "/*", "*", "return", "if", "while", "for", "switch", "catch")) and \
           not any(k in trimmed for k in ("SW_ASSERT", "SW_LOG", "SW_STATIC_ASSERT", "SW_EXPECT")):
            if lambdaMatch := _kLambdaParameterRe.search(codeWithoutStrings):
                sigParamStr = lambdaMatch.group(1)
                isFnSig = True
            elif fnMatch := _kFunctionSignatureRe.search(codeWithoutStrings):
                fnName = fnMatch.group(1)
                if fnName not in ("if", "while", "for", "switch", "catch", "sizeof", "decltype", "alignas", "return"):
                    sigParamStr = fnMatch.group(2)
                    isFnSig = True
            elif ctorSigMatch := _kConstructorSignatureRe.search(codeWithoutStrings):
                c1 = ctorSigMatch.group(1)
                c2 = ctorSigMatch.group(2)
                if (c2 is None and classStack and c1 == classStack[-1][0]) or (c2 is not None and c1 == c2):
                    sigParamStr = ctorSigMatch.group(3)
                    isFnSig = True

        if isFnSig and not trimmed.endswith(";"):
            pendingFunc = True

        # 함수 매개변수 — 서명을 쪼갠 매개변수마다 규칙에 넘긴다
        if sigParamStr is not None and sigParamStr.strip():
            for parameterText in splitParametersInternal(sigParamStr):
                for rule in _kRulesByScope["parameter"]:
                    violations.extend(rule.onParameter(scanContext, parameterText))


        # --- 생성자 초기화 리스트 검사 (포맷, 중괄호, 선언 순서) ---
        if isSource:
            if ctorMatch := _kConstructorDefinitionRe.search(trimmed):
                currentCtorClass = ctorMatch.group(1)
                ctorInitMembers = []
                ctorInitStartLine = lineNum
                inCtorInitList = False
        else:
            if classStack and classStack[-1][0] in trimmed and compileHeaderCtorReInternal(classStack[-1][0]).search(trimmed):
                if not trimmed.endswith(";"):
                    currentCtorClass = classStack[-1][0]
                    ctorInitMembers = []
                    ctorInitStartLine = lineNum
                    inCtorInitList = False

        if trimmed.startswith(":") or (inCtorInitList and trimmed.startswith(",")):
            inCtorInitList = True
            initMatch = _kConstructorInitRe.search(trimmed)
            for rule in _kRulesByScope["constructorInitializer"]:
                violations.extend(rule.onInitializerLine(scanContext, initMatch))
            if initMatch is not None and initMatch.group(1).startswith("_"):
                ctorInitMembers.append(initMatch.group(1))

        if inCtorInitList and "{" in trimmed and not trimmed.startswith(":") and not trimmed.startswith(","):
            inCtorInitList = False
            if currentCtorClass and currentCtorClass in classMemberMap:
                declaredOrder = classMemberMap[currentCtorClass]
                dedupedInitMembers = list(dict.fromkeys(ctorInitMembers))
                expectedOrder = [m for m in declaredOrder if m in dedupedInitMembers]
                if dedupedInitMembers != expectedOrder:
                    for actual, expected in zip(dedupedInitMembers, expectedOrder):
                        if actual != expected:
                            violations.append(
                                ConventionViolation(
                                    file_path=relPath,
                                    line_number=ctorInitStartLine,
                                    rule_category="Style/ConstructorOrder",
                                    message=f"생성자 '{currentCtorClass}'의 멤버 초기화 순서가 클래스 선언 순서와 다릅니다 ('{actual}' 항목이 '{expected}'보다 먼저 나열됨).",
                                    snippet=lines[ctorInitStartLine - 1].strip(),
                                    suggested_fix=expected,
                                )
                            )
                            break
            currentCtorClass = None
            ctorInitMembers = []

        # 중괄호 열림 { 처리
        if "{" in trimmed:
            openCount = trimmed.count("{")
            if pendingClass:
                classStack.append((pendingClass, braceDepth))
                pendingClass = None
            if pendingFunc:
                funcStack.append(braceDepth)
                pendingFunc = False

        # 현재 스코프 판정
        isInsideClass = bool(classStack and (not funcStack or classStack[-1][1] >= funcStack[-1]) and braceDepth == classStack[-1][1] + 1)
        isInsideFunction = bool(funcStack and not isInsideClass)

        # 1) 함수 내부 -> 지역 변수 규칙
        if isInsideFunction and not isInsideClass and not trimmed.startswith((": ", ", ")):
            for rule in _kRulesByScope["functionLocal"]:
                violations.extend(rule.onLine(scanContext))

        # 2) 클래스 본문 직속 -> 멤버 변수 검사
        if isInsideClass:
            for rule in _kRulesByScope["classMember"]:
                violations.extend(rule.onLine(scanContext))

        # 중괄호 증감 및 스택 정리
        openBraces = trimmed.count("{")
        closeBraces = trimmed.count("}")
        braceDepth += openBraces - closeBraces

        while funcStack and braceDepth <= funcStack[-1] and "}" in trimmed:
            funcStack.pop()

        while classStack and braceDepth <= classStack[-1][1] and "}" in trimmed:
            classStack.pop()

    return violations
