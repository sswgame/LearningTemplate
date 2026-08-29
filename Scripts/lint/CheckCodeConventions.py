#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CheckCodeConventions.py

SW Engine C++ 코딩 컨벤션 및 정적 규칙 자동 검사 스크립트.

검사 항목:
  1) Explicit Comparison: bool, pointer, container 상태(empty, contains) 암시적 평가 및 ! 부정문 검출
  2) Loop Counter Naming: for 루프 내 단일 문자 카운터(i, j, k) 검출
  3) Member Variable Naming:
     - 고정 배열: _arr 접두어 누락 검출
     - 가변 배열(vector, list, deque): _list 접두어 또는 List 접미어 누락 검출
     - 연관 컨테이너(map, unordered_map): _map 접두어 누락 검출
     - 고유 집합(set, unordered_set): _unique 접두어 누락 검출
     - 원시 포인터 멤버: _p 접두어 누락 검출
     - 전역 정적 변수: s_, _s_ 및 포인터 s_p, _s_p 누락 검출
  4) Constructor Initialization Rules:
     - 중괄호 균일 초기화 ({}) 사용 여부 검출
     - 한 줄에 1개 변수 초기화 및 다음 줄 ',' 시작 포맷 검출
     - 생성자 멤버 초기화 순서가 클래스 멤버 선언 순서와 일치하는지 검출
  5) auto 사용 제한: 리터럴/원시 타입 직접 대입에 auto 사용 검출
  6) Include 규칙: .cpp 파일 첫 줄 #include "pch.h" 여부

사용법:
  py -3 Scripts/lint/CheckCodeConventions.py [--root <repo>] [--json] [--files <files...>]
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import (
    collectSourceFiles,
    getLintSearchDirs,
    getProjectRoot,
    kCppAllExtensions,
    kCppHeaderExtensions,
    kCppSourceExtensions,
    normalizePath,
)

# --- 1. 자료구조 정의 --------------------------------------------------------

@dataclass
class ConventionViolation:
    file_path: str
    line_number: int
    rule_category: str
    message: str
    snippet: str
    suggested_fix: str | None = None


# --- 2. 정규표현식 패턴 ------------------------------------------------------

# [루프 인덱스 변수 명명 검사]
# for 루프에서 'i', 'j', 'k' 와 같이 의미를 알 수 없는 단일 문자 카운터 변수 선언을 검출합니다.
# 매칭 예시: for (int i = 0; ...), for (size_t j = 0; ...), for (auto k = 0; ...)
# 컨벤션 규칙: 최소 'index', 'partIndex', 'childIndex' 등의 의미 있는 이름을 사용해야 합니다.
_kLoopIndexRe = re.compile(
    r'\bfor\s*\(\s*(?:auto|int\w*|uint\w*|size_t)\s+([ijk])\s*=',
    re.MULTILINE,
)

# [PCH 인클루드 검사]
# .cpp 소스 파일의 첫 번째 유효한 코드 줄이 #include "pch.h" 인지 확인합니다.
# 매칭 예시: #include "pch.h", #include <pch.h>
# 컨벤션 규칙: 모든 번역 단위(.cpp)의 최상단에는 반드시 pch.h가 가장 먼저 인클루드되어야 합니다.
_kPchIncludeRe = re.compile(r'^\s*#\s*include\s*["<]pch\.h[">]')

# [일반 인클루드 경로 검사]
_kIncludePathRe = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')

_s_exactPathMap: dict[str, str] = {}


def getExactPathMapInternal(projectRoot: Path) -> dict[str, str]:
    """저장소 내 모든 소스/헤더 파일의 실제 대소문자 경로 맵을 생성합니다."""
    global _s_exactPathMap
    if not _s_exactPathMap:
        newMap = {}
        for searchDir in ["Source", "Test", "Tools"]:
            baseDir = projectRoot / searchDir
            if not baseDir.is_dir():
                continue
            for root, dirs, files in os.walk(baseDir):
                for f in files:
                    ext = os.path.splitext(f)[1].lower()
                    if ext in kCppAllExtensions or ext == ".inl":
                        fullPath = Path(root) / f
                        rel = fullPath.relative_to(baseDir).as_posix()
                        relRoot = fullPath.relative_to(projectRoot).as_posix()
                        newMap[rel.lower()] = rel
                        newMap[relRoot.lower()] = relRoot
        _s_exactPathMap = newMap
    return _s_exactPathMap


# [원시 포인터 멤버 변수 명명 검사]
# 클래스/구조체 헤더에서 '_p' 접두어가 붙지 않은 원시 포인터(*) 멤버 변수 선언을 검출합니다.
# 매칭 예시: GameObject* _target;, IRHIDevice* _device; (위반 -> _pTarget, _pDevice 이어야 함)
# 컨벤션 규칙: 멤버 포인터 변수는 '_p' 접두어로 시작해야 합니다.
_kMemberRawPointerRe = re.compile(
    r'^\s*(?:[A-Za-z0-9_:]+\s*\*)\s+(_[^pP\s][a-zA-Z0-9_]*)\s*;'
)

# [고정 크기 배열 멤버 변수 명명 검사]
# 원시 타입의 고정 크기 배열([]) 멤버 변수 중 '_arr' 접두어가 누락된 경우를 검출합니다.
# 매칭 예시: float32 _matrix[16];, uint32 _buffer[256]; (위반 -> _arrMatrix, _arrBuffer 이어야 함)
# 컨벤션 규칙: 고정 크기 배열은 '_arr' 접두어로 시작해야 합니다.
_kMemberFixedArrayRe = re.compile(
    r'^\s*(?:float32|float64|int32|int64|uint8|uint16|uint32|uint64|char|utf8|bool)\s+(_[^a\s][a-zA-Z0-9_]*)\s*\[[^\]]+\]\s*;'
)

# [가변 크기 배열/리스트 멤버 변수 명명 검사]
# std::vector, std::list, std::deque 등 동적 컨테이너 멤버 중 '_list' 접두어 또는 'List' 접미어가 없는 경우를 검출합니다.
# 매칭 예시: vector<Actor*> _actors; (위반 -> _listActor 또는 _actorsList 이어야 함)
# 컨벤션 규칙: 가변 크기 배열 컨테이너는 '_list' 접두어나 'List' 접미어를 사용해야 합니다.
_kMemberVectorRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:vector|list|deque))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
)

# [연관 컨테이너(맵) 멤버 변수 명명 검사]
# std::map, std::unordered_map 등 키-값 연관 컨테이너 멤버 중 '_map' 접두어가 누락된 경우를 검출합니다.
# 매칭 예시: unordered_map<string, int32> _table; (위반 -> _mapTable 이어야 함)
# 컨벤션 규칙: 연관 컨테이너 멤버는 '_map' 접두어로 시작해야 합니다.
_kMemberMapRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:unordered_map|map))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
)

# [고유 집합(세트) 컨테이너 멤버 변수 명명 검사]
# std::set, std::unordered_set 등 고유 키 집합 컨테이너 멤버 중 '_unique' 접두어가 누락된 경우를 검출합니다.
# 매칭 예시: set<uint32> _objectIds; (위반 -> _uniqueObjectIds 이어야 함)
# 컨벤션 규칙: 유일성이 보장되는 집합 컨테이너 멤버는 '_unique' 접두어로 시작해야 합니다.
_kMemberSetRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:unordered_set|set))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
)

# [리터럴/원시 타입 auto 남용 검사]
# 문자열, 불리언(true/false), nullptr 등 타입이 명확한 리터럴에 불필요하게 auto를 사용한 경우를 검출합니다.
# 매칭 예시: auto name = "Player";, auto bReady = true;, auto pObj = nullptr;
# 컨벤션 규칙: auto는 복잡한 반복자(iterator)나 구조화된 바인딩(structured binding)에만 제한적으로 사용해야 합니다.
_kLiteralAutoRe = re.compile(
    r'^\s*auto\s+([a-zA-Z0-9_]+)\s*=\s*(?:"[^"]*"|\'[^\']*\'|\btrue\b|\bfalse\b|\bnullptr\b)\s*;'
)

# [불필요한 '== true' 명시 비교 검사]
# 조건문(if) 안에서 불리언 표현식을 장황하게 '== true' 또는 'true ==' 와 비교하는 구문을 검출합니다.
# 매칭 예시: if (bValid == true), if (true == isReady)
# 컨벤션 규칙: 참 비교는 'if (bValid)' 와 같이 간결하게 평가해야 합니다.
_kExplicitTrueRe = re.compile(
    r'\bif\s*\(\s*([a-zA-Z0-9_>.-]+\s*==\s*true|true\s*==\s*[a-zA-Z0-9_>.-]+)\s*\)'
)

# [암시적 부정(!expr) 조건문 통합 검사]
# 조건문(if) 안에서 부정 연산자(!)를 이용해 암시적으로 거짓/널을 평가하는 구문을 포괄 검출합니다.
# 매칭 예시: if (!_bValid), if (!pActor), if (!list.empty())
# 컨벤션 규칙: 피연산자의 성격에 따라 명시적 비교('== false', '== nullptr')를 사용해야 합니다.
_kNegatedConditionRe = re.compile(
    r'\bif\s*\(\s*!\s*([a-zA-Z0-9_>.:()]+(?:\.[a-zA-Z0-9_]+(?:\([^)]*\))?)?)\s*\)'
)

# [암시적 포인터 널 체크 검사]
# 게터 함수 반환값 등의 포인터에 대해 '!= nullptr' 없이 암시적으로 null을 검사하는 구문을 검출합니다.
# 매칭 예시: if ( getOwner() ), if ( getScene() && getPlayer() )
# 컨벤션 규칙: 포인터 검사는 'if ( getOwner() != nullptr )' 와 같이 명시적 nullptr 비교를 사용해야 합니다.
_kImplicitPointerNullRe = re.compile(
    r'\bif\s*\(\s*(get[A-Z][a-zA-Z0-9_]*\(\)(?:\s*&&\s*get[A-Z][a-zA-Z0-9_]*\(\))*)\s*\)'
)

# [상수 명명 규칙 검사]
# static constexpr 상수가 'k' 접두어 + PascalCase 규칙을 준수하지 않은 경우를 검출합니다.
# 매칭 예시: static constexpr uint32 MAX_SIZE = 100; (위반 -> kMaxSize 이어야 함)
# 컨벤션 규칙: 상수는 'kPascalCase' 명명 규칙을 준수해야 합니다.
_kConstantNamingRe = re.compile(
    r'^\s*static\s+constexpr\s+(?:\w+)\s+([A-Z][a-zA-Z0-9_]*)\s*='
)

# [원시 기본 자료형 사용 검사]
# C++ 표준 원시 타입(int, float, unsigned int 등)을 직접 선언한 경우를 검출합니다.
# 매칭 예시: int count;, unsigned long size;, double weight;
# 컨벤션 규칙: 플랫폼 독립적 크기 보장을 위해 Types.h에 정의된 별칭(int32, uint32, float32, float64 등)을 사용해야 합니다.
_kBasicTypesRe = re.compile(
    r'\b(?:unsigned\s+int|unsigned\s+short|unsigned\s+long\s+long|unsigned\s+char|long\s+long|unsigned\s+long|long|int|float|double|short|char|wchar_t)\b'
)

# [생성자 멤버 초기화 리스트 괄호 검사]
# 생성자 초기화 리스트에서 소괄호 '()' 또는 중괄호 '{}'로 멤버 변수를 초기화하는 구문을 캡처합니다.
# 매칭 예시: : _member(0), , _pOwner{nullptr}
# 컨벤션 규칙: 생성자 초기화 시 균일 초기화 중괄호 '{}'를 사용해야 합니다.
_kConstructorInitRe = re.compile(
    r'^[,\:]\s*([a-zA-Z0-9_]+)\s*(\([^\)]*\)|\{[^\}]*\})'
)


_kClassDeclRe = re.compile(
    r'^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(?:(?:SW_\w*API|alignas\([^)]*\))\s+)*([A-Za-z0-9_]+)(?:\s*final|\s*:\s*[^{;]+)?\s*\{?'
)
_kClassMemberRe = re.compile(
    r'^\s*(?:\[\[[^\]]*\]\]\s*)?(?:(?:mutable|static|inline|const|volatile|constexpr)\s+)*(?:[A-Za-z0-9_:]+(?:<[^;]+>)?\s*[\*&]?\s+)(_[a-zA-Z0-9_]+)\s*(?::\s*\d+)?\s*(?:\[[^\]]*\])?\s*(?:\{[^}]*\}|\([^)]*\))?\s*(?:=\s*[^;]+)?\s*;'
)
_kClassMemberFnPtrRe = re.compile(
    r'^\s*(?:\[\[[^\]]*\]\]\s*)?(?:[A-Za-z0-9_:]+\s+)?\(\s*\*\s*(_[a-zA-Z0-9_]+)\s*\)\s*\([^)]*\)\s*;'
)


# --- 3. 클래스 멤버 변수 선언 추출 헬퍼 ---------------------------------------

def extractClassMembersInternal(content: str) -> dict[str, list[str]]:
    """
    C++ 소스/헤더 내용에서 클래스/구조체별 멤버 변수(_로 시작) 선언 순서 목록을 추출합니다.
    """
    classMembers: dict[str, list[str]] = {}
    classStack: list[tuple[str, int]] = []  # (className, entryBraceDepth)
    braceDepth = 0

    for rawLine in content.splitlines():
        line = rawLine.strip()
        if not line or line.startswith("//"):
            continue

        # class / struct 선언 시작 감지 (전방 선언 제외)
        if match := _kClassDeclRe.search(line):
            if not line.endswith(";"):
                newClass = match.group(1)
                classMembers.setdefault(newClass, [])
                classStack.append((newClass, braceDepth))

        braceDepth += line.count("{") - line.count("}")

        while classStack and braceDepth <= classStack[-1][1] and "}" in line:
            classStack.pop()

        if classStack and not any(line.startswith(k) for k in ("return", "SW_ASSERT", "using", "typedef", "friend")):
            currClass = classStack[-1][0]
            # 일반 멤버 변수 또는 함수 포인터 멤버 변수 추출
            memberMatch = _kClassMemberRe.search(line) or _kClassMemberFnPtrRe.search(line)
            if memberMatch:
                member = memberMatch.group(1)
                if member not in classMembers[currClass]:
                    classMembers[currClass].append(member)

    return classMembers


# --- 4. 파일별 컨벤션 검사 로직 ----------------------------------------------

def checkFileConventionsInternal(filePath: Path, rootDir: Path) -> list[ConventionViolation]:
    violations: list[ConventionViolation] = []
    relPath = normalizePath(filePath.relative_to(rootDir))
    isHeader = filePath.suffix.lower() in kCppHeaderExtensions
    isSource = filePath.suffix.lower() in kCppSourceExtensions

    try:
        content = filePath.read_text(encoding="utf-8-sig")
    except UnicodeDecodeError:
        try:
            content = filePath.read_text(encoding="latin-1")
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
                headerContent = matchingHeader.read_text(encoding="utf-8")
                classMemberMap.update(extractClassMembersInternal(headerContent))
            except Exception:
                pass

    # 1. .cpp 소스 파일의 첫 include "pch.h" 검사
    if isSource:
        firstCodeLine = None
        firstCodeLineNum = 1
        for idx, line in enumerate(lines, start=1):
            trimmed = line.strip()
            if not trimmed or trimmed.startswith("//") or trimmed.startswith("/*") or trimmed.startswith("*"):
                continue
            firstCodeLine = trimmed
            firstCodeLineNum = idx
            break

        if firstCodeLine and not _kPchIncludeRe.match(firstCodeLine):
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=firstCodeLineNum,
                    rule_category="Include/PCH",
                    message='.cpp 소스 파일의 첫 번째 인클루드는 반드시 #include "pch.h" 이어야 합니다.',
                    snippet=firstCodeLine,
                )
            )

    # 2. 줄 단위 규칙 검사
    inBlockComment = False
    currentCtorClass: str | None = None
    ctorInitMembers: list[str] = []
    ctorInitStartLine: int = 0
    inCtorInitList = False
    headerClassStack: list[tuple[str, int]] = []
    headerBraceDepth = 0

    for lineNum, line in enumerate(lines, start=1):
        trimmed = line.strip()

        # 블록 주석 처리
        if "/*" in trimmed and "*/" not in trimmed:
            inBlockComment = True
            continue
        if inBlockComment:
            if "*/" in trimmed:
                inBlockComment = False
            continue
        if trimmed.startswith("//") or not trimmed:
            continue

        # 인클루드 경로 파일명 및 대소문자 일치 검사 (#include "Core/String/FormatString.h" vs "Core/String/formatString.h")
        if includeMatch := _kIncludePathRe.match(trimmed):
            includeType = includeMatch.group(1)
            includePath = includeMatch.group(2).replace("\\", "/")
            if includeType == '"' and includePath != "pch.h":
                exactMap = getExactPathMapInternal(rootDir)
                includeLower = includePath.lower()
                if includeLower in exactMap:
                    exactPath = exactMap[includeLower]
                    if includePath != exactPath:
                        violations.append(
                            ConventionViolation(
                                file_path=relPath,
                                line_number=lineNum,
                                rule_category="Include/PathCasing",
                                message=f"인클루드 경로 '{includePath}'의 대소문자가 실제 파일 시스템 경로 '{exactPath}'와 일치하지 않습니다.",
                                snippet=trimmed,
                                suggested_fix=f'#include "{exactPath}"',
                            )
                        )

        # 헤더 내 현재 클래스 스코프 추적
        if isHeader:
            if classMatch := _kClassDeclRe.search(trimmed):
                if not trimmed.endswith(";"):
                    headerClassStack.append((classMatch.group(1), headerBraceDepth))
            headerBraceDepth += trimmed.count("{") - trimmed.count("}")
            while headerClassStack and headerBraceDepth <= headerClassStack[-1][1] and "}" in trimmed:
                headerClassStack.pop()

        # 단일 문자 루프 카운터(i, j, k) 검사
        if loopMatch := _kLoopIndexRe.search(line):
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/LoopVariable",
                    message=f"단일 문자 루프 변수 '{loopMatch.group(1)}' 사용이 검출되었습니다. 의미 있는 이름(예: index, childIndex)을 사용하세요.",
                    snippet=trimmed,
                )
            )

        # 리터럴/원시 타입 직접 대입 시 auto 사용 검사
        if autoMatch := _kLiteralAutoRe.search(line):
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/AutoUsage",
                    message=f"명시적 리터럴/원시 타입 대입 변수 '{autoMatch.group(1)}'에 auto를 사용하지 마세요.",
                    snippet=trimmed,
                )
            )

        # 불필요한 '== true' 명시 검사
        if _kExplicitTrueRe.search(line):
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/ExplicitTrueCheck",
                    message="불리언을 '== true'와 명시적으로 비교하지 마세요. 'if (bValid)' 형태를 사용하세요.",
                    snippet=trimmed,
                )
            )

        # 부정(!expr) 조건문 검사 (피연산자 유형별 명시적 비교 안내)
        if negatedMatch := _kNegatedConditionRe.search(line):
            expr = negatedMatch.group(1).strip()
            # 1) 포인터 변수 (_p..., p...)
            if re.match(r'^_?p[A-Z]', expr) or "->" in expr:
                msg = f"포인터 부정 조건 'if ( !{expr} )' 대신 명시적 'if ( {expr} == nullptr )' 비교를 사용하세요."
            # 2) 상태 메서드 (.empty(), .contains() 등)
            elif expr.endswith(".empty()") or expr.endswith(".contains()"):
                msg = f"상태 부정 조건 'if ( !{expr} )' 대신 명시적 'if ( {expr} == false )' 비교를 사용하세요."
            # 3) 불리언 변수 (_b..., b..., is..., has..., can...)
            elif re.match(r'^_?b[A-Z]', expr) or expr.startswith(("is", "has", "can")):
                msg = f"불리언 부정 조건 'if ( !{expr} )' 대신 명시적 'if ( {expr} == false )' 비교를 사용하세요."
            # 4) 일반 기타 표현식
            else:
                msg = f"부정 연산자 'if ( !{expr} )' 대신 명시적 비교('== false' 또는 '== nullptr')를 사용하세요."

            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/NegatedComparison",
                    message=msg,
                    snippet=trimmed,
                )
            )

        # 암시적 포인터 널 검사 (e.g. if ( getOwner() ))
        if ptrMatch := _kImplicitPointerNullRe.search(line):
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/ImplicitPointerNullCheck",
                    message=f"암시적 포인터 검사 '{ptrMatch.group(1)}'가 검출되었습니다. 명시적 '!= nullptr' 비교를 사용하세요.",
                    snippet=trimmed,
                )
            )

        # 상수 네이밍 검사 (static constexpr Mask -> kMask)
        if constMatch := _kConstantNamingRe.search(line):
            varName = constMatch.group(1)
            if "Math" not in relPath:
                if not varName.startswith("k") or (len(varName) > 1 and not varName[1].isupper()):
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/Constant",
                            message=f"상수 '{varName}'는 kPascalCase 명명 규칙을 따라야 합니다.",
                            snippet=trimmed,
                        )
                    )

        # 원시 기본 자료형 사용 검사 (int -> int32, float -> float32 등 Types.h 별칭 권장)
        if not trimmed.startswith("#") and "int main" not in trimmed and "Types.h" not in relPath:
            codeWithoutStrings = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '', line)
            if basicTypeMatch := _kBasicTypesRe.search(codeWithoutStrings):
                typeName = basicTypeMatch.group(0)
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Style/BasicTypeAlias",
                        message=f"기본 자료형 '{typeName}' 대신 Types.h의 별칭(int32, float32 등)을 사용하세요.",
                        snippet=trimmed,
                    )
                )

        # --- 생성자 초기화 리스트 검사 (포맷, 중괄호, 선언 순서) ---
        # 생성자 정의 헤더 감지
        if isSource:
            if ctorMatch := re.search(r'\b([A-Za-z0-9_]+)::\1\s*\([^)]*\)', trimmed):
                currentCtorClass = ctorMatch.group(1)
                ctorInitMembers = []
                ctorInitStartLine = lineNum
                inCtorInitList = False
        else:
            if headerClassStack and (ctorMatch := re.search(rf'\b{headerClassStack[-1][0]}\s*\([^)]*\)', trimmed)):
                if not trimmed.endswith(";"):
                    currentCtorClass = headerClassStack[-1][0]
                    ctorInitMembers = []
                    ctorInitStartLine = lineNum
                    inCtorInitList = False

        if trimmed.startswith(":") or (inCtorInitList and trimmed.startswith(",")):
            inCtorInitList = True
            # 1. 소괄호 () 대신 중괄호 {} 사용 검사
            if initMatch := _kConstructorInitRe.search(trimmed):
                initVar = initMatch.group(1)
                initVal = initMatch.group(2)
                if initVal.startswith("(") and not initVar.startswith("super") and not initVar.endswith("Base"):
                    if "_" in initVar:
                        violations.append(
                            ConventionViolation(
                                file_path=relPath,
                                line_number=lineNum,
                                rule_category="Style/ConstructorBraces",
                                message=f"생성자 멤버 초기화 '{initVar}'는 소괄호 '()' 대신 중괄호 '{{}}'를 사용해야 합니다.",
                                snippet=trimmed,
                            )
                        )
                if initVar.startswith("_"):
                    ctorInitMembers.append(initVar)

            # 2. 한 줄에 2개 이상의 멤버 초기화 금지 (: _a{0}, _b{0})
            if (trimmed.startswith(":") or trimmed.startswith(",")) and ("," in trimmed[1:]):
                if re.search(r'[\}\)]\s*,\s*_[a-zA-Z0-9_]+', trimmed):
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Style/ConstructorOnePerLine",
                            message="생성자 멤버 초기화는 한 줄에 하나의 변수만 와야 하며, 다음 줄에 ','로 시작해야 합니다.",
                            snippet=trimmed,
                        )
                    )

        # 생성자 본문 시작 { 도달 시 선언 순서 검증
        if inCtorInitList and "{" in trimmed and not trimmed.startswith(":") and not trimmed.startswith(","):
            inCtorInitList = False
            if currentCtorClass and currentCtorClass in classMemberMap:
                declaredOrder = classMemberMap[currentCtorClass]
                # #if / #else 조건부 분기로 인한 중복 초기화 제거 (순서 보존)
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
                                )
                            )
                            break
            currentCtorClass = None
            ctorInitMembers = []

        # 헤더 멤버 변수 명명 규칙 검사
        if isHeader:
            # 원시 포인터 멤버 접두어(_p) 검사
            if ptrMemberMatch := _kMemberRawPointerRe.match(line):
                varName = ptrMemberMatch.group(1)
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/RawPointer",
                        message=f"원시 포인터 멤버 변수 '{varName}'는 '_p' 접두어로 시작해야 합니다.",
                        snippet=trimmed,
                    )
                )

            # 고정 배열 멤버 접두어(_arr) 검사
            if arrMatch := _kMemberFixedArrayRe.match(line):
                varName = arrMatch.group(1)
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/FixedArray",
                        message=f"고정 배열 멤버 변수 '{varName}'는 '_arr' 접두어로 시작해야 합니다.",
                        snippet=trimmed,
                    )
                )

            # 가변 배열/리스트 멤버 접두어(_list) 또는 접미어(List) 검사
            if vecMatch := _kMemberVectorRe.match(line):
                varName = vecMatch.group(1)
                if not varName.startswith("_list") and not varName.endswith("List") and not varName.startswith("_s_list"):
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/DynamicContainer",
                            message=f"동적 배열/벡터 멤버 변수 '{varName}'는 '_list' 접두어로 시작하거나 'List' 접미어로 끝나야 합니다.",
                            snippet=trimmed,
                        )
                    )

            # 맵 컨테이너 멤버 접두어(_map) 검사
            if mapMatch := _kMemberMapRe.match(line):
                varName = mapMatch.group(1)
                if not varName.startswith("_map") and not varName.startswith("_s_map"):
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/MapContainer",
                            message=f"연관 컨테이너 멤버 변수 '{varName}'는 '_map' 접두어로 시작해야 합니다.",
                            snippet=trimmed,
                        )
                    )

            # 집합 컨테이너 멤버 접두어(_unique) 검사
            if setMatch := _kMemberSetRe.match(line):
                varName = setMatch.group(1)
                if not varName.startswith("_unique") and not varName.startswith("_s_unique"):
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/SetContainer",
                            message=f"고유 집합 컨테이너 멤버 변수 '{varName}'는 '_unique' 접두어로 시작해야 합니다.",
                            snippet=trimmed,
                        )
                    )

            # 컨테이너 멤버 변수 단수형 명명 규칙 검사 (_list*, _map*, _unique*, _arr*)
            for prefix in ("_list", "_map", "_unique", "_arr"):
                if varName := None:
                    pass
                match = re.match(rf'^\s*(?:(?:sw::)?(?:vector|list|deque|unordered_map|map|unordered_set|set))\s*<[^>]+>\s+({prefix}[A-Z][a-zA-Z0-9_]*)\s*;', line)
                if match:
                    vName = match.group(1)
                    # Non-plural exceptions
                    if not any(vName.endswith(exc) for exc in ("Bounds", "Status", "Pass", "Address", "Axis", "Process", "Class", "Cross", "Loss", "Mass", "Press", "Canvas", "Args", "Bytes", "Bindless", "RtvIndex", "DsvIndex", "Matrix", "Vertex", "Alias")):
                        if vName.endswith(("ies", "es", "s")):
                            violations.append(
                                ConventionViolation(
                                    file_path=relPath,
                                    line_number=lineNum,
                                    rule_category="Naming/ContainerSingular",
                                    message=f"컨테이너 멤버 변수 '{vName}'는 복수형 대신 단수형 명사를 사용해야 합니다.",
                                    snippet=trimmed,
                                )
                            )

    return violations


# --- 5. 공개 API 및 진입점 ---------------------------------------------------

def runConventionsCheck(rootDir: Path | None = None,
                        specificFiles: list[str] | None = None) -> list[ConventionViolation]:
    """
    지정된 소스 파일 또는 전체 소스 디렉터리를 순회하며 코딩 컨벤션 위반 항목을 검사합니다.
    """
    projectRoot = rootDir or Path(getProjectRoot())
    getExactPathMapInternal(projectRoot)
    allViolations: list[ConventionViolation] = []

    if specificFiles:
        for fileString in specificFiles:
            filePath = Path(fileString).resolve()
            if not filePath.is_file():
                continue
            normPath = filePath.as_posix()
            if "ThirdParty" in normPath or "build" in normPath or ".vcpkg" in normPath:
                continue
            if filePath.suffix.lower() not in kCppAllExtensions:
                continue

            relRootDir = projectRoot
            if projectRoot not in filePath.parents:
                for parent in filePath.parents:
                    if (parent / "Source").is_dir() or (parent / "Test").is_dir() or (parent / "Tools").is_dir():
                        relRootDir = parent
                        break

            allViolations.extend(checkFileConventionsInternal(filePath, relRootDir))
        return allViolations

    searchDirs = getLintSearchDirs(projectRoot)
    filesToScan = collectSourceFiles(
        searchDirs, excludeSubdirs=["ThirdParty", "build", ".vcpkg"]
    )
    maxWorkers = min(32, (os.cpu_count() or 4) * 2)
    with concurrent.futures.ThreadPoolExecutor(max_workers=maxWorkers) as executor:
        futures = [executor.submit(checkFileConventionsInternal, filePath, projectRoot) for filePath in filesToScan]
        for future in concurrent.futures.as_completed(futures):
            allViolations.extend(future.result())

    return allViolations


def main() -> int:
    parser = argparse.ArgumentParser(description="SW Engine C++ 코딩 컨벤션 검사기")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트 디렉터리 경로")
    parser.add_argument("--category", type=str, default=None, help="특정 규칙 카테고리 필터링")
    parser.add_argument("--exclude-category", type=str, default=None, help="제외할 규칙 카테고리")
    parser.add_argument("--json", action="store_true", help="결과를 JSON 형식으로 출력")
    parser.add_argument("--files", nargs="*", help="전체 스캔 대신 검사할 개별 파일 목록")
    args = parser.parse_args()

    rootDir = args.root or Path(getProjectRoot())
    violations = runConventionsCheck(rootDir, args.files)

    if args.category:
        violations = [v for v in violations if args.category.lower() in v.rule_category.lower()]
    if args.exclude_category:
        violations = [v for v in violations if args.exclude_category.lower() not in v.rule_category.lower()]

    if args.json:
        outputJsonData = [asdict(v) for v in violations]
        print(json.dumps(outputJsonData, indent=2, ensure_ascii=False))
    else:
        print("\n========================================================")
        print("  SW Engine C++ 코딩 컨벤션 검사 보고서")
        print(f"  저장소: {rootDir}")
        print(f"  발견된 위반 항목: {len(violations)}건")
        print("========================================================\n")

        categoryGroups: dict[str, list[ConventionViolation]] = {}
        for violation in violations:
            categoryGroups.setdefault(violation.rule_category, []).append(violation)

        for category, items in sorted(categoryGroups.items()):
            print(f"[{category}] ({len(items)}건):")
            for item in items:
                print(f"  {item.file_path}:{item.line_number} -> {item.message}")
                print(f"      코드: {item.snippet}")
            print()

        print("--------------------------------------------------------")
        print("카테고리별 요약:")
        for category, items in sorted(categoryGroups.items()):
            print(f"  - {category:<25}: {len(items):>3}건")
        print("========================================================\n")

    return 0 if len(violations) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
