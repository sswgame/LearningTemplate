#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CheckCodeConventions.py

# SW Engine C++ 코딩 컨벤션 및 정적 규칙 자동 검사 스크립트.

검사 항목:
  1) Explicit Comparison: bool, pointer, container 상태(empty, contains) 암시적 평가 및 ! 부정문 검출
  2) Loop Counter Naming: for 루프 내 단일 문자 카운터(i, j, k) 검출
  3) Member Variable Naming:
     - 고정 배열: _arr 접두어 누락 검출
     - 가변 배열(vector, list, deque): _list 접두어 누락 및 List 접미어 검출 (단, byte 단어가 포함된 바이트 벡터는 list 생략)
     - 연관 컨테이너(map, unordered_map): _map 접두어 누락 검출
     - 고유 집합(set, unordered_set): _unique 접두어 누락 검출
     - 원시 포인터 멤버: _p (단일), _pp (이중) 접두어 누락 검출
     - 삼중 포인터 이상(ppp, _ppp, ***) 금지 검출
     - 전역 정적 변수: s_, _s_ 및 포인터 s_p, _s_p 누락 검출
  4) Out-Parameter Naming:
     - 출력 매개변수 out 접두어(outList, outMap, outUnique, outArr, inout) 누락 검출
     - 포인터 출력 매개변수는 예외적으로 pOut, ppOut, pInOut, ppInOut 사용 강제 (outP, outPP 등 검출)
  5) Constructor Initialization Rules:
     - 중괄호 균일 초기화 ({}) 사용 여부 검출
     - 한 줄에 1개 변수 초기화 및 다음 줄 ',' 시작 포맷 검출
     - 생성자 멤버 초기화 순서가 클래스 멤버 선언 순서와 일치하는지 검출
  6) auto 사용 제한: 리터럴/원시 타입 직접 대입에 auto 사용 검출
  7) Include 규칙: .cpp 파일 첫 줄 #include "pch.h" 여부

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
# 정규식 패턴: r'\bfor\s*\(\s*(?:auto|int\w*|uint\w*|size_t)\s+([ijk])\s*='
#   - \bfor\s*\(                      : for 루프의 시작 괄호 매칭
#   - (?:auto|int\w*|uint\w*|size_t)   : 카운터 타입 (auto, int, int32, uint32, size_t 등)
#   - ([ijk])                         : 금지 대상 단일 문자 변수명 (i, j, k) 캡처
#   - \s*=                            : 초기화 대입 연산자 매칭
# 매칭 예시 (위반): for (int i = 0; ...), for (size_t j = 0; ...), for (auto k = 0; ...)
# 올바른 예시: for (int32 index = 0; ...), for (size_t childIndex = 0; ...)
# 컨벤션 규칙: 의미를 알 수 없는 단일 문자 카운터는 금지되며, 최소 'index' 이상의 구체적 이름을 부여해야 합니다.
_kLoopIndexRe = re.compile(
    r'\bfor\s*\(\s*(?:auto|int\w*|uint\w*|size_t)\s+([ijk])\s*=',
    re.MULTILINE,
)

# [PCH 인클루드 검사]
# 정규식 패턴: r'^\s*#\s*include\s*["<]pch\.h[">]'
#   - ^\s*#\s*include\s*              : 줄 시작 부분의 #include 지시문 매칭
#   - ["<]pch\.h[">]                  : "pch.h" 또는 <pch.h> 인클루드 경로 매칭
# 매칭 예시 (정상): #include "pch.h", #include <pch.h>
# 올바른 구조: .cpp 번역 단위의 가장 첫 번째 유효 코드는 반드시 pch.h 여야 합니다.
# 컨벤션 규칙: 빠른 컴파일을 위해 모든 cpp 소스는 pch.h를 첫 번째로 인클루드해야 합니다.
_kPchIncludeRe = re.compile(r'^\s*#\s*include\s*["<]pch\.h[">]')

# [일반 인클루드 경로 검사]
# 정규식 패턴: r'^\s*#\s*include\s*([<"])([^>"]+)[>"]'
#   - ([<"])                          : 인클루드 경로 시작 기호 (< 또는 ") 캡처 (그룹 1)
#   - ([^>"]+)                        : 인클루드 상대/절대 파일 경로 캡처 (그룹 2)
#   - [>"]                            : 인클루드 경로 종료 기호 (> 또는 ") 매칭
# 용도: 실제 파일 시스템 상의 대소문자(Exact Path Case)와 일치하는지 대조 검증
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
# 정규식 패턴: r'^\s*(?:[A-Za-z0-9_:]+\s*\*)\s+(_[^pP\s][a-zA-Z0-9_]*)\s*;'
#   - ^\s*(?:[A-Za-z0-9_:]+\s*\*)     : 포인터 타입 선언 (예: Type*, Namespace::Type*) 매칭
#   - \s+(_[^pP\s][a-zA-Z0-9_]*)      : 멤버 변수명 중 '_'로 시작하지만 두 번째 글자가 'p'/'P'가 아닌 이름 캡처 (그룹 1)
#   - \s*;                            : 세미콜론 종료 매칭
# 매칭 예시 (위반): GameObject* _target;, IRHIDevice* _device;
# 올바른 예시: GameObject* _pTarget;, IRHIDevice* _pDevice;, Node** _ppNode;
# 컨벤션 규칙: 멤버 원시 포인터는 단일 포인터 '_p', 이중 포인터는 '_pp' 접두어를 필수 사용해야 합니다.
_kMemberRawPointerRe = re.compile(
    r'^\s*(?:[A-Za-z0-9_:]+\s*\*)\s+(_[^pP\s][a-zA-Z0-9_]*)\s*;'
)

# [삼중 포인터 이상 금지 검사]
# 정규식 패턴: r'\b_?(?:s_)?p{3,}[A-Za-z0-9_]*\b|\*\s*\*\s*\*'
#   - \b_?(?:s_)?p{3,}[A-Za-z0-9_]*\b : 'p'가 3개 이상 연속되는 식별자 (pppVar, _pppVar, s_pppGlobal, _s_pppGlobal, pppOut) 매칭
#   - |\*\s*\*\s*\*                   : 포인터 역참조 기호가 3개 연속(***)되는 타입 선언 매칭
# 매칭 예시 (위반): int*** pppPtr;, void* _pppHandle;, s_pppGlobal, Node*** pOutNode
# 올바른 예시: Actor* pActor, Node** ppNode, void* pBuffer
# 컨벤션 규칙: 삼중 포인터 이상(ppp, ***)은 구조적 설계 결함으로 간주하여 전면 금지합니다.
_kTriplePointerRe = re.compile(
    r'\b_?(?:s_)?p{3,}[A-Za-z0-9_]*\b|\*\s*\*\s*\*'
)

# [고정 크기 배열 멤버 변수 명명 검사]
# 정규식 패턴: r'^\s*(?:float32|float64|int32|int64|uint8|uint16|uint32|uint64|char|utf8|bool)\s+(_[^a\s][a-zA-Z0-9_]*)\s*\[[^\]]+\]\s*;'
#   - ^\s*(?:float32|...|bool)        : 원시 기본 자료형 매칭
#   - \s+(_[^a\s][a-zA-Z0-9_]*)       : 멤버 변수명 중 '_'로 시작하지만 'a'로 시작하지 않는(_arr가 아닌) 이름 캡처
#   - \s*\[[^\]]+\]\s*;               : 고정 배열 대괄호 및 세미콜론 매칭
# 매칭 예시 (위반): float32 _matrix[16];, uint32 _buffer[256];
# 올바른 예시: float32 _arrMatrix[16];, uint32 _arrBuffer[256];
# 컨벤션 규칙: 고정 크기 배열 멤버 변수는 반드시 '_arr' 접두어로 시작해야 합니다.
_kMemberFixedArrayRe = re.compile(
    r'^\s*(?:float32|float64|int32|int64|uint8|uint16|uint32|uint64|char|utf8|bool)\s+(_[^a\s][a-zA-Z0-9_]*)\s*\[[^\]]+\]\s*;'
)

# [가변 크기 배열/리스트 멤버 변수 명명 검사]
# 정규식 패턴: r'^\s*(?:(?:sw::)?(?:vector|list|deque))\s*<([^>]+)>\s+(_[a-zA-Z0-9_]+)\s*;'
#   - ^\s*(?:(?:sw::)?(?:vector|list|deque)) : 가변 컨테이너 타입(vector, list, deque) 매칭
#   - <([^>]+)>                              : 내부 요소 템플릿 인자 타입 캡처 (그룹 1: byte 계열 타입 판정용)
#   - \s+(_[a-zA-Z0-9_]+)\s*;                : 멤버 변수명 캡처 (그룹 2)
# 매칭 예시 (위반): vector<Actor*> _actors;, vector<Actor*> _actorList;, vector<uint8> _listBytes;
# 올바른 예시: vector<Actor*> _listActor;, vector<uint8> _bytes;, vector<uint8> _rawBytes;
# 컨벤션 규칙:
#   1. 가변 컨테이너는 반드시 '_list' 접두어를 사용하며 단수형 명사를 씁니다 ('List' 접미어 금지).
#   2. 단, uint8/int8/utf8 등 원시 바이트 컨테이너 중 이름에 'byte'/'bytes'가 포함된 경우 '_list' 접두어를 생략합니다.
_kMemberVectorRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:vector|list|deque))\s*<([^>]+)>\s+(_[a-zA-Z0-9_]+)\s*;'
)

# [출력 매개변수 명명 검사]
# 정규식 패턴:
#   r'\b(?:(?:const\s+)?(?:[A-Za-z0-9_:]+(?:<[^>]+>)?)\s*[\*&]+\s+|\b)'
#   r'(outP(?!ath)[A-Za-z0-9_]*|outPP[A-Za-z0-9_]*|inoutP[A-Za-z0-9_]*|inoutPP[A-Za-z0-9_]*|'
#   r'listOut[A-Za-z0-9_]*|mapOut[A-Za-z0-9_]*|uniqueOut[A-Za-z0-9_]*|arrOut[A-Za-z0-9_]*|'
#   r'listInOut[A-Za-z0-9_]*|mapInOut[A-Za-z0-9_]*|out_[a-zA-Z0-9_]+|out[A-Z][a-zA-Z0-9_]*List|'
#   r'outList[A-Za-z0-9_]*Bytes?|outListByte[A-Za-z0-9_]*)\b'
_kOutParamNamingRe = re.compile(
    r'\b(?:(?:const\s+)?(?:[A-Za-z0-9_:]+(?:<[^>]+>)?)\s*[\*&]+\s+|\b)'
    r'(outP[A-Z][A-Za-z0-9_]*|outPP[A-Z][A-Za-z0-9_]*|inoutP[A-Z][A-Za-z0-9_]*|inoutPP[A-Z][A-Za-z0-9_]*|'
    r'listOut(?!puts?\b)[A-Za-z0-9_]*|mapOut[A-Za-z0-9_]*|uniqueOut[A-Za-z0-9_]*|arrOut[A-Za-z0-9_]*|'
    r'listInOut[A-Za-z0-9_]*|mapInOut[A-Za-z0-9_]*|(?<!::)\bout_(?!of_range\b)[a-zA-Z0-9_]+|out[A-Z][a-zA-Z0-9_]*List|'
    r'outList[A-Za-z0-9_]*Bytes?|outListByte[A-Za-z0-9_]*)\b'
)

# 타입세이프 포매터(SW_LOG_* / formatstring / appendFormat / SW_ASSERT_MSG)를 호출하는 라인
_kFormatterCallRe = re.compile(
    r'\b(?:SW_LOG_(?:INFO|WARNING|ERROR|DEBUG|FATAL|TRACE|VERBOSE)|SW_ASSERT_MSG|formatstring|appendFormat|appendFormatLine)\s*\('
)
# 이 포매터가 파싱하지 못하는 printf 스펙: % 뒤에 플래그/폭/정밀도(. - + 공백 0-9)가 붙은 변환.
# `%#`(플레이스홀더)과 `%s %d %u %f %x %p %zu %lld` 같은 맨 변환은 정상 처리되므로 제외한다.
_kBadPrintfSpecRe = re.compile(
    r'%[-+ #0]*(?:\d+\.?\d*|\.\d+)[-+ #]*(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGaAcsp]'
)
_kStringLiteralRe = re.compile(r'"((?:\\.|[^"\\])*)"')

# [비복수형 예외 단어 목록]
kNonPluralExceptions = (
    "Bounds", "Status", "Pass", "Address", "Axis", "Process", "Class", "Cross",
    "Loss", "Mass", "Press", "Canvas", "Args", "Bytes", "Bindless", "RtvIndex",
    "DsvIndex", "Matrix", "Vertex", "Alias", "Species", "Series", "Focus", "Radius",
    "Bias", "Lens", "Basis", "Crisis", "Analysis", "Mesh", "This", "Index", "Context",
    "Params", "Settings", "Coordinates", "Options", "Details", "Stats"
)


def makeSingularInternal(word: str) -> str:
    """단어 끝의 복수형 어미를 단수형으로 변환합니다."""
    if word.endswith("ies"):
        return word[:-3] + "y"
    if any(word.endswith(end) for end in ("sses", "shes", "ches", "xes", "zes")):
        return word[:-2]
    if word.endswith("s") and not word.endswith("ss"):
        return word[:-1]
    return word


def isPluralWordInternal(word: str) -> bool:
    """단어가 복수형 어미를 가지는지 판정합니다 (예외 목록 제외)."""
    if any(word.endswith(exc) for exc in kNonPluralExceptions):
        return False
    if word.endswith(("ies", "es", "s")) and not word.endswith("ss"):
        return True
    return False


def splitParametersInternal(signatureParams: str) -> list[str]:
    """
    함수 매개변수 시그니처 문자열을 템플릿(< >) 및 괄호 깊이를 보존하며 쉼표로 분리합니다.
    """
    params: list[str] = []
    current: list[str] = []
    templateDepth = 0
    parenDepth = 0
    braceDepth = 0

    for ch in signatureParams:
        if ch == '<':
            templateDepth += 1
            current.append(ch)
        elif ch == '>':
            if templateDepth > 0:
                templateDepth -= 1
            current.append(ch)
        elif ch == '(':
            parenDepth += 1
            current.append(ch)
        elif ch == ')':
            if parenDepth > 0:
                parenDepth -= 1
            current.append(ch)
        elif ch == '{':
            braceDepth += 1
            current.append(ch)
        elif ch == '}':
            if braceDepth > 0:
                braceDepth -= 1
            current.append(ch)
        elif ch == ',' and templateDepth == 0 and parenDepth == 0 and braceDepth == 0:
            pStr = "".join(current).strip()
            if pStr:
                params.append(pStr)
            current = []
        else:
            current.append(ch)

    pStr = "".join(current).strip()
    if pStr:
        params.append(pStr)
    return params


def getSuggestedOutParamFixInternal(name: str) -> tuple[str, str] | None:
    """
    잘못된 출력 매개변수 이름을 정규 컨벤션 이름과 설명 메시지로 변환합니다.
    """
    # 1. 이중 포인터 출력 (outPP... -> ppOut...)
    if name.startswith("outPP"):
        rest = name[5:]
        fix = "ppOut" + rest if rest else "ppOut"
        return fix, f"이중 포인터 출력 매개변수 '{name}'는 예외적으로 'ppOut' 접두어로 시작해야 합니다."

    # 2. 단일 포인터 출력 (outP... -> pOut...)
    if name.startswith("outP") and not name.startswith("outPath"):
        rest = name[4:]
        fix = "pOut" + rest if rest else "pOut"
        return fix, f"원시 포인터 출력 매개변수 '{name}'는 예외적으로 'pOut' 접두어로 시작해야 합니다."

    # 3. 이중 포인터 입출력 (inoutPP... -> ppInOut...)
    if name.startswith("inoutPP"):
        rest = name[7:]
        fix = "ppInOut" + rest if rest else "ppInOut"
        return fix, f"이중 포인터 입출력 매개변수 '{name}'는 'ppInOut' 접두어로 시작해야 합니다."

    # 4. 단일 포인터 입출력 (inoutP... -> pInOut...)
    if name.startswith("inoutP"):
        rest = name[6:]
        fix = "pInOut" + rest if rest else "pInOut"
        return fix, f"포인터 입출력 매개변수 '{name}'는 'pInOut' 접두어로 시작해야 합니다."

    # 5. 리스트 출력 (listOut... -> outList... / byte 예외)
    if name.startswith("listOut"):
        rest = name[7:]
        if "byte" in rest.lower():
            fix = "out" + rest
            return fix, f"바이트 벡터 출력 매개변수 '{name}'는 'out' 접두어로 시작하고 'list'를 생략해야 합니다."
        fix = "outList" + rest if rest else "outList"
        return fix, f"출력 리스트 매개변수 '{name}'는 'outList' 접두어로 시작해야 합니다."

    # 6. 리스트 입출력 (listInOut... -> inoutList...)
    if name.startswith("listInOut"):
        rest = name[9:]
        fix = "inoutList" + rest if rest else "inoutList"
        return fix, f"입출력 리스트 매개변수 '{name}'는 'inoutList' 접두어로 시작해야 합니다."

    # 7. 맵 출력 (mapOut... -> outMap...)
    if name.startswith("mapOut"):
        rest = name[6:]
        fix = "outMap" + rest if rest else "outMap"
        return fix, f"출력 맵 매개변수 '{name}'는 'outMap' 접두어로 시작해야 합니다."

    # 8. 맵 입출력 (mapInOut... -> inoutMap...)
    if name.startswith("mapInOut"):
        rest = name[8:]
        fix = "inoutMap" + rest if rest else "inoutMap"
        return fix, f"입출력 맵 매개변수 '{name}'는 'inoutMap' 접두어로 시작해야 합니다."

    # 9. 집합 출력 (uniqueOut... -> outUnique...)
    if name.startswith("uniqueOut"):
        rest = name[9:]
        fix = "outUnique" + rest if rest else "outUnique"
        return fix, f"출력 셋 매개변수 '{name}'는 'outUnique' 접두어로 시작해야 합니다."

    # 10. 고정 배열 출력 (arrOut... -> outArr...)
    if name.startswith("arrOut"):
        rest = name[6:]
        fix = "outArr" + rest if rest else "outArr"
        return fix, f"출력 배열 매개변수 '{name}'는 'outArr' 접두어로 시작해야 합니다."

    # 11. 바이트 벡터 접두어/접미어 중복 정리 (outListBytes / outBytesList -> outBytes)
    if (name.startswith("outList") or name.startswith("out")) and "byte" in name.lower() and name.endswith("List"):
        middle = name[3:-4]
        if middle.startswith("List"):
            middle = middle[4:]
        fix = "out" + middle
        return fix, f"바이트 벡터 출력 매개변수 '{name}'는 'List' 접미어를 사용하지 않고 '{fix}' 형태를 사용해야 합니다."
    if name.startswith("outList") and "byte" in name.lower():
        rest = name[7:]
        fix = "out" + rest
        return fix, f"바이트 벡터 출력 매개변수 '{name}'는 'list' 접두어를 생략해야 합니다."

    # 12. List 접미어 -> outList 접두어 변환 (outActorList -> outListActor)
    if name.startswith("out") and name.endswith("List") and len(name) > 7:
        middle = name[3:-4]
        fix = "outList" + middle
        return fix, f"출력 컨테이너 매개변수 '{name}'는 'List' 접미어 대신 'outList' 접두어를 사용해야 합니다."

    # 13. snake_case 출력 변수 -> camelCase 변환 (out_buffer -> outBuffer)
    if name.startswith("out_"):
        parts = name.split("_")
        fix = "out" + "".join(p.capitalize() for p in parts[1:])
        return fix, f"출력 매개변수 '{name}'는 camelCase 형태('{fix}')를 사용해야 합니다."

    # 14. 컨테이너 복수형 검사 (outList, outMap, outArr - unique 및 bytes 제외)
    if name.startswith(("outList", "outMap", "outArr")):
        if isPluralWordInternal(name):
            singularFix = makeSingularInternal(name)
            return singularFix, f"출력 컨테이너 매개변수 '{name}'는 복수형 대신 단수형 명사('{singularFix}')를 사용해야 합니다."

    return None


def checkParameterItemInternal(paramStr: str, relPath: str, lineNum: int, snippet: str) -> list[ConventionViolation]:
    """
    함수 매개변수 1개의 타입/변수명을 검사하여 규칙 위반 항목을 반환합니다.
    """
    violations: list[ConventionViolation] = []

    # 기본값 분리 (e.g. const Vector3& pos = Vector3::kZero)
    if "=" in paramStr:
        eqIdx = paramStr.find("=")
        paramDecl = paramStr[:eqIdx].strip()
    else:
        paramDecl = paramStr.strip()

    if not paramDecl or paramDecl == "void" or paramDecl == "...":
        return violations

    # 표현식(객체 생성 인자, 연산식 등) 오탐 필터링
    # 매개변수 선언이 아닌 식 (e.g. other._mutex, a + b, obj.x)
    if any(op in paramDecl for op in (".", "->", " + ", " - ", " * ", " / ", "%")):
        return violations

    # 고정 배열 파싱 (e.g. uint8 arrBuffer[256])
    arrayMatch = re.search(r'\[[^\]]*\]$', paramDecl)
    isArray = bool(arrayMatch)
    if isArray and arrayMatch:
        paramDecl = paramDecl[:arrayMatch.start()].strip()

    # 식별자(변수명) 분리
    match = re.search(r'([A-Za-z0-9_]+)$', paramDecl)
    if not match:
        return violations

    paramName = match.group(1)
    typePart = paramDecl[:match.start()].strip()

    # 타입만 명시되고 변수명이 생략된 선언 또는 네임스페이스 한정 식 (e.g. Scope::_s_var)
    if not typePart or typePart.endswith("::"):
        return violations

    # typePart가 유효한 C++ 타입 패턴인지 검증 (표현식 제외)
    if not re.match(r'^(?:(?:const|volatile|register)\s+)?(?:[A-Za-z0-9_:]+(?:<[^>]+>)?(?:\s*[*&]+)?\s*)+$', typePart):
        return violations

    # 1. '_' 접두어 검사 (매개변수/지역변수에는 '_' 사용 금지)
    if paramName.startswith("_"):
        fix = paramName.lstrip("_")
        violations.append(
            ConventionViolation(
                file_path=relPath,
                line_number=lineNum,
                rule_category="Naming/ParameterNoUnderscore",
                message=f"매개변수 '{paramName}'는 '_' 접두어를 사용할 수 없습니다 ('{fix}' 권장).",
                snippet=snippet,
                suggested_fix=fix,
            )
        )
        paramName = fix

    # 2. 원시 포인터 매개변수 검사 (템플릿 인자 <...> 내부의 *는 제외)
    typeWithoutTemplate = re.sub(r'<[^>]*>', '', typePart)
    numPtr = typeWithoutTemplate.count("*")
    if numPtr >= 1:
        if paramName in ("argv", "argc", "env", "this"):
            return violations

        if numPtr == 1:
            # 단일 포인터: p, pOut, pInOut로 시작해야 함
            if not paramName.startswith(("pOut", "pInOut")):
                if not paramName.startswith("p") or (len(paramName) > 1 and not paramName[1].isupper()):
                    fix = "p" + paramName[0].upper() + paramName[1:] if paramName else "pPtr"
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/ParameterPointer",
                            message=f"원시 포인터 매개변수 '{paramName}'는 'p' 접두어로 시작해야 합니다 ('{fix}' 권장).",
                            snippet=snippet,
                            suggested_fix=fix,
                        )
                    )
        elif numPtr == 2:
            # 이중 포인터: pp, ppOut, ppInOut로 시작해야 함
            if not paramName.startswith(("ppOut", "ppInOut")):
                if not paramName.startswith("pp") or (len(paramName) > 2 and not paramName[2].isupper()):
                    fix = "pp" + paramName[0].upper() + paramName[1:] if paramName else "ppPtr"
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/ParameterPointer",
                            message=f"이중 포인터 매개변수 '{paramName}'는 'pp' 접두어로 시작해야 합니다 ('{fix}' 권장).",
                            snippet=snippet,
                            suggested_fix=fix,
                        )
                    )

    # 3. 컨테이너 매개변수 검사
    # vector / list / deque
    elif re.search(r'\b(?:(?:sw::)?(?:vector|list|deque))\s*<([^>]+)>', typePart):
        innerTypeMatch = re.search(r'\b(?:(?:sw::)?(?:vector|list|deque))\s*<([^>]+)>', typePart)
        innerType = innerTypeMatch.group(1).strip() if innerTypeMatch else ""
        isByteVec = bool(re.search(r'\b(?:uint8|int8|utf8|char|byte)\b', innerType, re.IGNORECASE))
        hasByteWord = any(w in paramName.lower() for w in ("byte", "bytes", "buffer"))

        if isByteVec and hasByteWord:
            if paramName.startswith("list"):
                fix = paramName[4].lower() + paramName[5:] if len(paramName) > 4 else "bytes"
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/ParameterContainer",
                        message=f"바이트 벡터 매개변수 '{paramName}'는 'byte/buffer' 단어가 포함된 경우 'list' 접두어를 생략해야 합니다 ('{fix}' 권장).",
                        snippet=snippet,
                        suggested_fix=fix,
                    )
                )
        else:
            if not paramName.startswith(("list", "outList", "inoutList")):
                if paramName in ("out", "_out"):
                    candidate = "outList"
                elif paramName.startswith("out") and len(paramName) > 3:
                    candidate = "outList" + paramName[3:]
                elif paramName.startswith("inout") and len(paramName) > 5:
                    candidate = "inoutList" + paramName[5:]
                else:
                    candidate = "list" + paramName[0].upper() + paramName[1:] if paramName else "list"
                if isPluralWordInternal(candidate):
                    candidate = makeSingularInternal(candidate)
                fix = candidate
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/ParameterContainer",
                        message=f"동적 배열/리스트 매개변수 '{paramName}'는 'list' (출력은 'outList') 접두어로 시작해야 합니다 ('{fix}' 권장).",
                        snippet=snippet,
                        suggested_fix=fix,
                    )
                )
            # List 접미어 검사 (e.g. actorList -> listActor, outActorList -> outListActor)
            if paramName.endswith("List") and paramName not in ("list", "outList", "inoutList", "pOutList", "ppOutList"):
                if paramName.startswith("out") and len(paramName) > 7:
                    candidate = "outList" + paramName[3:-4]
                elif paramName.startswith("inout") and len(paramName) > 9:
                    candidate = "inoutList" + paramName[5:-4]
                else:
                    candidate = "list" + paramName[:-4]
                if isPluralWordInternal(candidate):
                    candidate = makeSingularInternal(candidate)
                fix = candidate
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/ParameterContainer",
                        message=f"매개변수 '{paramName}'는 'List' 접미어 대신 '{fix}' 형태를 사용해야 합니다.",
                        snippet=snippet,
                        suggested_fix=fix,
                    )
                )
            if paramName.startswith(("list", "outList", "inoutList")) and isPluralWordInternal(paramName):
                singularFix = makeSingularInternal(paramName)
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/ContainerSingular",
                        message=f"컨테이너 매개변수 '{paramName}'는 복수형 대신 단수형 명사를 사용해야 합니다 ('{singularFix}' 권장).",
                        snippet=snippet,
                        suggested_fix=singularFix,
                    )
                )

    # map / unordered_map
    elif re.search(r'\b(?:(?:sw::)?(?:unordered_map|map))\s*<', typePart):
        if not paramName.startswith(("map", "outMap", "inoutMap")):
            candidate = "map" + paramName[0].upper() + paramName[1:] if paramName else "map"
            if isPluralWordInternal(candidate):
                candidate = makeSingularInternal(candidate)
            fix = candidate
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/ParameterContainer",
                    message=f"연관 컨테이너 매개변수 '{paramName}'는 'map' 접두어로 시작해야 합니다 ('{fix}' 권장).",
                    snippet=snippet,
                    suggested_fix=fix,
                )
            )
        if paramName.startswith(("map", "outMap", "inoutMap")) and isPluralWordInternal(paramName):
            singularFix = makeSingularInternal(paramName)
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/ContainerSingular",
                    message=f"맵 매개변수 '{paramName}'는 복수형 대신 단수형 명사를 사용해야 합니다 ('{singularFix}' 권장).",
                    snippet=snippet,
                    suggested_fix=singularFix,
                )
            )

    # set / unordered_set
    elif re.search(r'\b(?:(?:sw::)?(?:unordered_set|set))\s*<', typePart):
        if not paramName.startswith(("unique", "outUnique", "inoutUnique")):
            fix = "unique" + paramName[0].upper() + paramName[1:] if paramName else "unique"
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/ParameterContainer",
                    message=f"고유 집합 매개변수 '{paramName}'는 'unique' 접두어로 시작해야 합니다 ('{fix}' 권장).",
                    snippet=snippet,
                    suggested_fix=fix,
                )
            )

    # 고정 배열
    elif isArray:
        if not paramName.startswith(("arr", "outArr", "inoutArr")):
            candidate = "arr" + paramName[0].upper() + paramName[1:] if paramName else "arr"
            if isPluralWordInternal(candidate):
                candidate = makeSingularInternal(candidate)
            fix = candidate
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/ParameterContainer",
                    message=f"고정 배열 매개변수 '{paramName}'는 'arr' 접두어로 시작해야 합니다 ('{fix}' 권장).",
                    snippet=snippet,
                    suggested_fix=fix,
                )
            )
        if isPluralWordInternal(paramName):
            singularFix = makeSingularInternal(paramName)
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/ContainerSingular",
                    message=f"배열 매개변수 '{paramName}'는 복수형 대신 단수형 명사를 사용해야 합니다 ('{singularFix}' 권장).",
                    snippet=snippet,
                    suggested_fix=singularFix,
                )
            )

    return violations


def checkLocalVariableItemInternal(line: str, relPath: str, lineNum: int) -> list[ConventionViolation]:
    """
    함수 내부의 지역 변수 선언문을 검사하여 규칙 위반 항목을 반환합니다.
    """
    violations: list[ConventionViolation] = []
    trimmed = line.strip()
    if trimmed.startswith(("#", "//", "/*", "*", "return", "if", "while", "for", "switch", "case", "using", "typedef", "friend", "struct", "class", "enum", "union")):
        return violations
    if "SW_ASSERT" in trimmed or "SW_LOG" in trimmed or "SW_STATIC_ASSERT" in trimmed or "catch" in trimmed:
        return violations
    if trimmed.startswith(("delete ", "delete[] ", "throw ", "goto ", "break;", "continue;")):
        return violations

    codeClean = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '', line)

    # 함수 선언/정의 시그니처인 경우 변수 선언이 아님
    if re.search(r'\([a-zA-Z0-9_,\s*&:<>=./"-]*\)\s*(?:const|override|final|noexcept|SW_\w*API)*\s*[{;=]', trimmed):
        return violations

    # 1. 지역 변수 '_' 접두어 검사 (예: int32 _val = 0;, auto _pPtr = ...)
    localUnderscoreMatch = re.search(r'^\s*(?:const\s+|static\s+|constexpr\s+|auto\s+)?(?:[A-Za-z0-9_:]+(?:<[^;]+>)?\s*[*&]*\s+)(_[a-zA-Z0-9]+)\s*(?:=|;|,|\{|\()', codeClean)
    if localUnderscoreMatch:
        varName = localUnderscoreMatch.group(1)
        if not varName.startswith(("_s_", "__")):
            fix = varName.lstrip("_")
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/LocalNoUnderscore",
                    message=f"지역 변수 '{varName}'는 '_' 접두어를 사용할 수 없습니다 ('{fix}' 권장).",
                    snippet=trimmed,
                    suggested_fix=fix,
                )
            )

    # 2. 로컬 원시 포인터 변수 명명 검사
    # 함수 정의/선언 (뒤에 괄호 ( 가 오는 함수 이름)는 변수가 아니므로 제외
    if not re.search(r'\b[A-Za-z0-9_:]+\s*\(', codeClean):
        ptrDeclMatch = re.search(r'^\s*(?:const\s+|static\s+|constexpr\s+)?([A-Za-z0-9_:]+)\s*(\*{1,2})\s*(?:const\s+)?([a-zA-Z0-9_]+)\s*(?:=|;|,|\{)', codeClean)
        if ptrDeclMatch:
            typeName = ptrDeclMatch.group(1)
            stars = ptrDeclMatch.group(2)
            varName = ptrDeclMatch.group(3)
            if typeName not in ("delete", "return", "sizeof", "static_cast", "reinterpret_cast", "dynamic_cast", "const_cast", "case", "default") and varName not in ("argv", "this"):
                isStatic = "static" in codeClean
                if len(stars) == 1:
                    # 단일 포인터: p로 시작하거나 static인 경우 s_p / _s_p 허용
                    isValidPointerName = (
                        (varName.startswith("p") and len(varName) > 1 and varName[1].isupper()) or
                        (varName.startswith("s_p") and len(varName) > 3 and varName[3].isupper()) or
                        (varName.startswith("_s_p") and len(varName) > 4 and varName[4].isupper()) or
                        varName.startswith("pOut")
                    )
                    if not isValidPointerName:
                        fix = "p" + varName.lstrip("_")[0].upper() + varName.lstrip("_")[1:] if varName.lstrip("_") else "pPtr"
                        violations.append(
                            ConventionViolation(
                                file_path=relPath,
                                line_number=lineNum,
                                rule_category="Naming/LocalPointer",
                                message=f"원시 포인터 지역 변수 '{varName}'는 'p' (정적 변수는 's_p') 접두어로 시작해야 합니다 ('{fix}' 권장).",
                                snippet=trimmed,
                                suggested_fix=fix,
                            )
                        )
                elif len(stars) == 2:
                    # 이중 포인터: pp로 시작하거나 static인 경우 s_pp / _s_pp 허용
                    isValidPointerName = (
                        (varName.startswith("pp") and len(varName) > 2 and varName[2].isupper()) or
                        (varName.startswith("s_pp") and len(varName) > 4 and varName[4].isupper()) or
                        (varName.startswith("_s_pp") and len(varName) > 5 and varName[5].isupper()) or
                        varName.startswith("ppOut")
                    )
                    if not isValidPointerName:
                        fix = "pp" + varName.lstrip("_")[0].upper() + varName.lstrip("_")[1:] if varName.lstrip("_") else "ppPtr"
                        violations.append(
                            ConventionViolation(
                                file_path=relPath,
                                line_number=lineNum,
                                rule_category="Naming/LocalPointer",
                                message=f"이중 포인터 지역 변수 '{varName}'는 'pp' (정적 변수는 's_pp') 접두어로 시작해야 합니다 ('{fix}' 권장).",
                                snippet=trimmed,
                                suggested_fix=fix,
                            )
                        )

    # 3. 로컬 가변 컨테이너 (vector, list, deque) 변수 명명 검사
    vecMatch = re.search(r'^\s*(?:(?:sw::)?(?:vector|list|deque))\s*<([^>]+)>\s+([a-zA-Z0-9_]+)\s*(?:=|;|\{|\()', codeClean)
    if vecMatch:
        innerType = vecMatch.group(1).strip()
        varName = vecMatch.group(2)
        isByteVec = bool(re.search(r'\b(?:uint8|int8|utf8|char|byte)\b', innerType, re.IGNORECASE))
        hasByteWord = any(w in varName.lower() for w in ("byte", "bytes", "buffer"))
        if isByteVec and hasByteWord:
            if varName.startswith("list") or varName.startswith("_list"):
                cleanName = varName.lstrip("_")
                fix = cleanName[4].lower() + cleanName[5:] if len(cleanName) > 4 else "bytes"
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/LocalContainer",
                        message=f"바이트 벡터 지역 변수 '{varName}'는 'byte/buffer' 단어가 포함된 경우 'list' 접두어를 생략해야 합니다 ('{fix}' 권장).",
                        snippet=trimmed,
                        suggested_fix=fix,
                    )
                )
        else:
            if not varName.startswith("list") and not varName.startswith("outList") and not varName.startswith("inoutList"):
                if varName in ("out", "_out"):
                    candidate = "outList"
                elif varName.lstrip("_").startswith("out") and len(varName.lstrip("_")) > 3:
                    candidate = "outList" + varName.lstrip("_")[3:]
                elif varName.lstrip("_").startswith("inout") and len(varName.lstrip("_")) > 5:
                    candidate = "inoutList" + varName.lstrip("_")[5:]
                else:
                    candidate = "list" + varName.lstrip("_")[0].upper() + varName.lstrip("_")[1:] if varName.lstrip("_") else "list"
                if isPluralWordInternal(candidate):
                    candidate = makeSingularInternal(candidate)
                fix = candidate
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/LocalContainer",
                        message=f"동적 배열/리스트 지역 변수 '{varName}'는 'list' (출력은 'outList') 접두어로 시작해야 합니다 ('{fix}' 권장).",
                        snippet=trimmed,
                        suggested_fix=fix,
                    )
                )
            # List 접미어 검사 (e.g. actorList -> listActor, outActorList -> outListActor)
            if varName.endswith("List") and varName not in ("list", "outList", "inoutList", "pOutList", "ppOutList"):
                if varName.startswith("out") and len(varName) > 7:
                    candidate = "outList" + varName[3:-4]
                elif varName.startswith("inout") and len(varName) > 9:
                    candidate = "inoutList" + varName[5:-4]
                else:
                    candidate = "list" + varName[:-4]
                if isPluralWordInternal(candidate):
                    candidate = makeSingularInternal(candidate)
                fix = candidate
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/LocalContainer",
                        message=f"지역 변수 '{varName}'는 'List' 접미어 대신 '{fix}' 형태를 사용해야 합니다.",
                        snippet=trimmed,
                        suggested_fix=fix,
                    )
                )
            if (varName.startswith("list") or varName.startswith("outList")) and isPluralWordInternal(varName):
                singularFix = makeSingularInternal(varName)
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/ContainerSingular",
                        message=f"컨테이너 지역 변수 '{varName}'는 복수형 대신 단수형 명사를 사용해야 합니다 ('{singularFix}' 권장).",
                        snippet=trimmed,
                        suggested_fix=singularFix,
                    )
                )

    # 4. 로컬 연관 컨테이너 (map, unordered_map) 변수 명명 검사
    mapMatch = re.search(r'^\s*(?:(?:sw::)?(?:unordered_map|map))\s*<[^>]+>\s+([a-zA-Z0-9_]+)\s*(?:=|;|\{|\()', codeClean)
    if mapMatch:
        varName = mapMatch.group(1)
        if not varName.startswith("map") and not varName.startswith("outMap") and not varName.startswith("inoutMap"):
            candidate = "map" + varName.lstrip("_")[0].upper() + varName.lstrip("_")[1:] if varName.lstrip("_") else "map"
            if isPluralWordInternal(candidate):
                candidate = makeSingularInternal(candidate)
            fix = candidate
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/LocalContainer",
                    message=f"연관 컨테이너 지역 변수 '{varName}'는 'map' 접두어로 시작해야 합니다 ('{fix}' 권장).",
                    snippet=trimmed,
                    suggested_fix=fix,
                )
            )
        if (varName.startswith("map") or varName.startswith("outMap")) and isPluralWordInternal(varName):
            singularFix = makeSingularInternal(varName)
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/ContainerSingular",
                    message=f"맵 지역 변수 '{varName}'는 복수형 대신 단수형 명사를 사용해야 합니다 ('{singularFix}' 권장).",
                    snippet=trimmed,
                    suggested_fix=singularFix,
                )
            )

    # 5. 로컬 고유 집합 (set, unordered_set) 변수 명명 검사
    setMatch = re.search(r'^\s*(?:(?:sw::)?(?:unordered_set|set))\s*<[^>]+>\s+([a-zA-Z0-9_]+)\s*(?:=|;|\{|\()', codeClean)
    if setMatch:
        varName = setMatch.group(1)
        if not varName.startswith("unique") and not varName.startswith("outUnique") and not varName.startswith("inoutUnique"):
            fix = "unique" + varName.lstrip("_")[0].upper() + varName.lstrip("_")[1:] if varName.lstrip("_") else "unique"
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/LocalContainer",
                    message=f"고유 집합 지역 변수 '{varName}'는 'unique' 접두어로 시작해야 합니다 ('{fix}' 권장).",
                    snippet=trimmed,
                    suggested_fix=fix,
                )
            )

    # 6. 로컬 고정 배열 변수 명명 검사
    arrMatch = re.search(r'^\s*(?:float32|float64|int32|int64|uint8|uint16|uint32|uint64|char|utf8|bool)\s+([a-zA-Z0-9_]+)\s*\[[^\]]+\]\s*(?:=|;|\{)', codeClean)
    if arrMatch:
        varName = arrMatch.group(1)
        if not varName.startswith("arr") and not varName.startswith("outArr") and not varName.startswith("inoutArr"):
            candidate = "arr" + varName.lstrip("_")[0].upper() + varName.lstrip("_")[1:] if varName.lstrip("_") else "arr"
            if isPluralWordInternal(candidate):
                candidate = makeSingularInternal(candidate)
            fix = candidate
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/LocalContainer",
                    message=f"고정 배열 지역 변수 '{varName}'는 'arr' 접두어로 시작해야 합니다 ('{fix}' 권장).",
                    snippet=trimmed,
                    suggested_fix=fix,
                )
            )
        if isPluralWordInternal(varName):
            singularFix = makeSingularInternal(varName)
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/ContainerSingular",
                    message=f"배열 지역 변수 '{varName}'는 복수형 대신 단수형 명사를 사용해야 합니다 ('{singularFix}' 권장).",
                    snippet=trimmed,
                    suggested_fix=singularFix,
                )
            )

    return violations



# [연관 컨테이너(맵) 멤버 변수 명명 검사]
# 정규식 패턴: r'^\s*(?:(?:sw::)?(?:unordered_map|map))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
#   - ^\s*(?:(?:sw::)?(?:unordered_map|map)) : map 또는 unordered_map 매칭
#   - <[^>]+>                                : 템플릿 인자 (<Key, Value>) 매칭
#   - \s+(_[a-zA-Z0-9_]+)\s*;                : 멤버 변수명 캡처
# 매칭 예시 (위반): unordered_map<string, int32> _table; (위반 -> _mapTable 이어야 함)
# 올바른 예시: unordered_map<string, int32> _mapTable;, map<int32, Actor*> _mapIdToActor;
# 컨벤션 규칙: 연관 컨테이너 멤버는 '_map' 접두어로 시작해야 합니다.
_kMemberMapRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:unordered_map|map))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
)

# [고유 집합(세트) 컨테이너 멤버 변수 명명 검사]
# 정규식 패턴: r'^\s*(?:(?:sw::)?(?:unordered_set|set))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
#   - ^\s*(?:(?:sw::)?(?:unordered_set|set)) : set 또는 unordered_set 매칭
#   - <[^>]+>                                : 템플릿 인자 (<Key>) 매칭
#   - \s+(_[a-zA-Z0-9_]+)\s*;                : 멤버 변수명 캡처
# 매칭 예시 (위반): set<uint32> _ids; (위반 -> _uniqueIds 이어야 함)
# 올바른 예시: set<uint32> _uniqueIds;, unordered_set<string> _uniqueTags;
# 컨벤션 규칙: 고유 집합 멤버는 '_unique' 접두어로 시작하며, 컨테이너 중 유일하게 복수형 명사(_uniqueIds)가 허용됩니다.
_kMemberSetRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:unordered_set|set))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
)

# [리터럴/원시 타입 auto 남용 검사]
# 정규식 패턴: r'^\s*auto\s+([a-zA-Z0-9_]+)\s*=\s*(?:"[^"]*"|\'[^\']*\'|\btrue\b|\bfalse\b|\bnullptr\b)\s*;'
#   - ^\s*auto\s+([a-zA-Z0-9_]+)              : auto 변수 선언 및 변수명 캡처
#   - \s*=\s*                                 : 대입 연산자 매칭
#   - (?:"[^"]*"|\'[^\']*\'|\btrue\b|\bfalse\b|\bnullptr\b) : 명확한 리터럴 값 (문자열, 불리언, nullptr) 매칭
# 매칭 예시 (위반): auto name = "Player";, auto bReady = true;, auto pObj = nullptr;
# 올바른 예시: const char* name = "Player";, bool bReady = true;, Actor* pObj = nullptr;
# 컨벤션 규칙: auto는 복잡한 반복자(iterator)나 구조화된 바인딩(structured binding)에만 제한적으로 사용해야 합니다.
_kLiteralAutoRe = re.compile(
    r'^\s*auto\s+([a-zA-Z0-9_]+)\s*=\s*(?:"[^"]*"|\'[^\']*\'|\btrue\b|\bfalse\b|\bnullptr\b)\s*;'
)

# [불필요한 '== true' 명시 비교 검사]
# 정규식 패턴: r'\bif\s*\(\s*([a-zA-Z0-9_>.-]+\s*==\s*true|true\s*==\s*[a-zA-Z0-9_>.-]+)\s*\)'
#   - \bif\s*\(                               : if 조건문 시작 매칭
#   - (expr\s*==\s*true | true\s*==\s*expr)   : 불리언 식과 true 리터럴 간의 명시적 동등 비교 구문 캡처
# 매칭 예시 (위반): if (bValid == true), if (true == isReady)
# 올바른 예시: if (bValid), if (isReady)
# 컨벤션 규칙: 불리언 참 비교는 'if (bValid)' 와 같이 명시적 리터럴 없이 간결하게 평가합니다.
_kExplicitTrueRe = re.compile(
    r'\bif\s*\(\s*([a-zA-Z0-9_>.-]+\s*==\s*true|true\s*==\s*[a-zA-Z0-9_>.-]+)\s*\)'
)

# [암시적 부정(!expr) 조건문 통합 검사]
# 정규식 패턴: r'\bif\s*\(\s*!\s*([a-zA-Z0-9_>.:()]+(?:\.[a-zA-Z0-9_]+(?:\([^)]*\))?)?)\s*\)'
#   - \bif\s*\(\s*!\s*                        : if 문 시작 직후의 논리 부정 연산자(!) 매칭
#   - ([a-zA-Z0-9_>.:()]+...)                 : 부정되는 대상 표현식 캡처
# 매칭 예시 (위반): if (!_bValid), if (!pActor), if (!list.empty())
# 올바른 예시: if (_bValid == false), if (pActor == nullptr), if (list.empty() == false)
# 컨벤션 규칙: 암시적 '!' 부정은 엄격히 금지되며, 명시적 비교('== false', '== nullptr')를 작성해야 합니다.
_kNegatedConditionRe = re.compile(
    r'\bif\s*\(\s*!\s*([a-zA-Z0-9_>.:()]+(?:\.[a-zA-Z0-9_]+(?:\([^)]*\))?)?)\s*\)'
)

# [암시적 포인터 널 체크 검사]
# 정규식 패턴: r'\bif\s*\(\s*(get[A-Z][a-zA-Z0-9_]*\(\)(?:\s*&&\s*get[A-Z][a-zA-Z0-9_]*\(\))*)\s*\)'
#   - \bif\s*\(\s*(get[A-Z]...\(\))           : if 문 안에서 getOwner() 등 포인터 반환 게터를 직접 불리언처럼 평가하는 구문 매칭
# 매칭 예시 (위반): if ( getOwner() ), if ( getScene() && getPlayer() )
# 올바른 예시: if ( getOwner() != nullptr ), if ( getScene() != nullptr && getPlayer() != nullptr )
# 컨벤션 규칙: 포인터의 유효성 검사는 반드시 '!= nullptr' 또는 '== nullptr'를 명시해야 합니다.
_kImplicitPointerNullRe = re.compile(
    r'\bif\s*\(\s*(get[A-Z][a-zA-Z0-9_]*\(\)(?:\s*&&\s*get[A-Z][a-zA-Z0-9_]*\(\))*)\s*\)'
)

# [상수 명명 규칙 검사]
# 정규식 패턴: r'^\s*static\s+constexpr\s+(?:\w+)\s+([A-Z][a-zA-Z0-9_]*)\s*='
#   - ^\s*static\s+constexpr\s+(?:\w+)\s+     : static constexpr 상수 타입 선언 매칭
#   - ([A-Z][a-zA-Z0-9_]*)                    : 대문자로 시작하지만 'k' 접두어가 누락된 상수명 캡처
#   - \s*=                                    : 대입 연산자 매칭
# 매칭 예시 (위반): static constexpr uint32 MAX_SIZE = 100;, static constexpr float32 DefaultSpeed = 5.0f;
# 올바른 예시: static constexpr uint32 kMaxSize = 100;, static constexpr float32 kDefaultSpeed = 5.0f;
# 컨벤션 규칙: 모든 정적 상수는 'kPascalCase' 접두어 규칙을 준수해야 합니다.
_kConstantNamingRe = re.compile(
    r'^\s*static\s+constexpr\s+(?:\w+)\s+([A-Z][a-zA-Z0-9_]*)\s*='
)

# [원시 기본 자료형 사용 검사]
# 정규식 패턴: r'\b(?:unsigned\s+int|unsigned\s+short|unsigned\s+long\s+long|unsigned\s+char|long\s+long|unsigned\s+long|long|int|float|double|short|char|wchar_t)\b'
#   - 표준 C++ 원시 타입 키워드들을 단어 경계(\b)로 감지
# 매칭 예시 (위반): int count;, unsigned int size;, float weight;, double delta;
# 올바른 예시: int32 count;, uint32 size;, float32 weight;, float64 delta;, utf8 ch;
# 컨벤션 규칙: 플랫폼 독립적 크기 보장 및 일관성을 위해 Types.h에 정의된 별칭을 사용해야 합니다.
_kBasicTypesRe = re.compile(
    r'\b(?:unsigned\s+int|unsigned\s+short|unsigned\s+long\s+long|unsigned\s+char|long\s+long|unsigned\s+long|long|int|float|double|short|char|wchar_t)\b'
)

# [생성자 멤버 초기화 리스트 괄호 검사]
# 정규식 패턴: r'^[,\:]\s*([a-zA-Z0-9_]+)\s*(\([^\)]*\)|\{[^\}]*\})'
#   - ^[,\:]\s*                               : 줄 시작의 콜론(:) 또는 쉼표(,) 매칭
#   - ([a-zA-Z0-9_]+)                         : 초기화 대상 멤버 변수명 캡처 (그룹 1)
#   - (\([^\)]*\)|\{[^\}]*\})                 : 소괄호 (val) 또는 중괄호 {val} 초기화 구문 캡처 (그룹 2)
# 매칭 예시 (위반): : _member(0), , _pOwner(nullptr)
# 올바른 예시: : _member{0}, , _pOwner{nullptr}
# 컨벤션 규칙: 생성자 초기화 리스트에서는 균일 초기화 중괄호 '{}'를 사용해야 합니다.
_kConstructorInitRe = re.compile(
    r'^[,\:]\s*([a-zA-Z0-9_]+)\s*(\([^\)]*\)|\{[^\}]*\})'
)

# [클래스 / 구조체 선언 검사]
# 정규식 패턴: r'^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(?:(?:SW_\w*API|alignas\([^)]*\))\s+)*([A-Za-z0-9_]+)(?:\s*final|\s*:\s*[^{;]+)?\s*\{?'
#   - 클래스 또는 구조체의 정의부와 클래스명을 추출하여 멤버 선언 순서 및 생성자 순서 검증에 활용합니다.
_kClassDeclRe = re.compile(
    r'^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(?:(?:SW_\w*API|alignas\([^)]*\))\s+)*([A-Za-z0-9_]+)(?:\s*final|\s*:\s*[^{;]+)?\s*\{?'
)

# [클래스 멤버 변수 선언 검사]
# 정규식 패턴: r'^\s*(?:\[\[[^\]]*\]\]\s*)?(?:(?:alignas\([^)]*\)|mutable|static|inline|const|volatile|constexpr)\s+)*(?:[A-Za-z0-9_:]+(?:<[^;]+>)?\s*[\*&]?\s+)(_[a-zA-Z0-9_]+)\s*(?::\s*\d+)?\s*(?:\[[^\]]*\])?\s*(?:\{[^}]*\}|\([^)]*\))?\s*(?:=\s*[^;]+)?\s*;'
#   - 클래스 내부에서 '_'로 시작하는 모든 멤버 변수 선언의 순서를 순차 추출합니다.
_kClassMemberRe = re.compile(
    r'^\s*(?:\[\[[^\]]*\]\]\s*)?(?:(?:alignas\([^)]*\)|mutable|static|inline|const|volatile|constexpr)\s+)*(?:[A-Za-z0-9_:]+(?:<[^;]+>)?\s*[\*&]?\s+)(_[a-zA-Z0-9_]+)\s*(?::\s*\d+)?\s*(?:\[[^\]]*\])?\s*(?:\{[^}]*\}|\([^)]*\))?\s*(?:=\s*[^;]+)?\s*;'
)

# [함수 포인터 멤버 변수 선언 검사]
# 정규식 패턴: r'^\s*(?:\[\[[^\]]*\]\]\s*)?(?:[A-Za-z0-9_:]+\s+)?\(\s*\*\s*(_[a-zA-Z0-9_]+)\s*\)\s*\([^)]*\)\s*;'
#   - 반환타입 (*_pFn)(인자타입) 형태의 함수 포인터 멤버 변수를 추출합니다.
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

    classStack: list[tuple[str, int]] = []  # (className, entryBraceDepth)
    funcStack: list[int] = []               # entryBraceDepth
    braceDepth = 0
    pendingClass: str | None = None
    pendingFunc = False

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

        codeWithoutStrings = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '', line)

        # 인클루드 경로 파일명 및 대소문자 일치 검사
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

        # class / struct 선언 감지
        if classMatch := _kClassDeclRe.search(trimmed):
            if not trimmed.endswith(";"):
                pendingClass = classMatch.group(1)

        # 함수 선언 / 정의 감지
        isFnSig = False
        sigParamStr = None
        if not trimmed.startswith(("#", "//", "/*", "*", "return", "if", "while", "for", "switch", "catch")) and \
           not any(k in trimmed for k in ("SW_ASSERT", "SW_LOG", "SW_STATIC_ASSERT", "SW_EXPECT")):
            if lambdaMatch := re.search(r'\[[^\]]*\]\s*\(([^)]*)\)', codeWithoutStrings):
                sigParamStr = lambdaMatch.group(1)
                isFnSig = True
            elif fnMatch := re.search(r'^\s*(?:(?:inline|static|virtual|explicit|constexpr|friend|SW_\w*API)\s+)*(?:(?:const\s+)?[A-Za-z0-9_:]+(?:<[^;]+>)?(?:\s*[*&]+)?\s+)+([A-Za-z0-9_:]+)\s*\(([^)]*)\)', codeWithoutStrings):
                fnName = fnMatch.group(1)
                if fnName not in ("if", "while", "for", "switch", "catch", "sizeof", "decltype", "alignas", "return"):
                    sigParamStr = fnMatch.group(2)
                    isFnSig = True
            elif ctorSigMatch := re.search(r'^\s*(?:explicit\s+)?([A-Za-z0-9_]+)(?:::([A-Za-z0-9_]+))?\s*\(([^)]*)\)', codeWithoutStrings):
                c1 = ctorSigMatch.group(1)
                c2 = ctorSigMatch.group(2)
                if (c2 is None and classStack and c1 == classStack[-1][0]) or (c2 is not None and c1 == c2):
                    sigParamStr = ctorSigMatch.group(3)
                    isFnSig = True

        if isFnSig and not trimmed.endswith(";"):
            pendingFunc = True

        # 함수 매개변수 명명 검사
        if sigParamStr is not None and sigParamStr.strip():
            paramList = splitParametersInternal(sigParamStr)
            for p in paramList:
                violations.extend(checkParameterItemInternal(p, relPath, lineNum, trimmed))

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

        # 타입세이프 포매터에 printf 플래그/폭/정밀도 스펙 사용 검사 (%.2f, %016llx, %#08X 등)
        if _kFormatterCallRe.search(line):
            for strMatch in _kStringLiteralRe.finditer(line):
                badSpec = _kBadPrintfSpecRe.search(strMatch.group(1))
                if badSpec:
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Style/LogFormatSpec",
                            message=(
                                f"타입세이프 포매터가 파싱하지 못하는 printf 스펙 '{badSpec.group(0)}'이(가) 있습니다. "
                                "`%#` 플레이스홀더 + Fmt(값, Format().precision(N)) / Format(폭, Format::Padding::Zero).hex() 를 사용하세요."
                            ),
                            snippet=trimmed,
                        )
                    )
                    break

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

        # 부정(!expr) 조건문 검사
        if negatedMatch := _kNegatedConditionRe.search(line):
            expr = negatedMatch.group(1).strip()
            if re.match(r'^_?p[A-Z]', expr) or "->" in expr:
                msg = f"포인터 부정 조건 'if ( !{expr} )' 대신 명시적 'if ( {expr} == nullptr )' 비교를 사용하세요."
            elif expr.endswith(".empty()") or expr.endswith(".contains()"):
                msg = f"상태 부정 조건 'if ( !{expr} )' 대신 명시적 'if ( {expr} == false )' 비교를 사용하세요."
            elif re.match(r'^_?b[A-Z]', expr) or expr.startswith(("is", "has", "can")):
                msg = f"불리언 부정 조건 'if ( !{expr} )' 대신 명시적 'if ( {expr} == false )' 비교를 사용하세요."
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

        # 암시적 포인터 널 검사
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

        # 상수 네이밍 검사
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

        # 원시 기본 자료형 사용 검사
        if not trimmed.startswith("#") and "int main" not in trimmed and "Types.h" not in relPath:
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

        # 삼중 포인터 이상(ppp, ***) 검사
        if not trimmed.startswith("#") and "Types.h" not in relPath:
            if tripleMatch := _kTriplePointerRe.search(codeWithoutStrings):
                matchedStr = tripleMatch.group(0)
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/TriplePointer",
                        message=f"삼중 포인터 이상('{matchedStr}') 사용이 검출되었습니다. 구조적 결함이므로 데이터 구조를 재설계하세요.",
                        snippet=trimmed,
                    )
                )

        # 출력 매개변수 명명 규칙 검사
        if not trimmed.startswith("#") and "Types.h" not in relPath:
            for outMatch in _kOutParamNamingRe.finditer(codeWithoutStrings):
                paramCandidate = outMatch.group(1)
                fixResult = getSuggestedOutParamFixInternal(paramCandidate)
                if fixResult is not None:
                    suggestedFix, fixMsg = fixResult
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/OutParameter",
                            message=f"{fixMsg} ('{suggestedFix}' 권장)",
                            snippet=trimmed,
                            suggested_fix=suggestedFix,
                        )
                    )

        # --- 생성자 초기화 리스트 검사 (포맷, 중괄호, 선언 순서) ---
        if isSource:
            if ctorMatch := re.search(r'\b([A-Za-z0-9_]+)::\1\s*\([^)]*\)', trimmed):
                currentCtorClass = ctorMatch.group(1)
                ctorInitMembers = []
                ctorInitStartLine = lineNum
                inCtorInitList = False
        else:
            if classStack and (ctorMatch := re.search(rf'\b{classStack[-1][0]}\s*\([^)]*\)', trimmed)):
                if not trimmed.endswith(";"):
                    currentCtorClass = classStack[-1][0]
                    ctorInitMembers = []
                    ctorInitStartLine = lineNum
                    inCtorInitList = False

        if trimmed.startswith(":") or (inCtorInitList and trimmed.startswith(",")):
            inCtorInitList = True
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

        # 1) 함수 내부 -> 지역 변수 검사
        if isInsideFunction and not isInsideClass and not trimmed.startswith((": ", ", ")):
            violations.extend(checkLocalVariableItemInternal(line, relPath, lineNum))

        # 2) 클래스 본문 직속 -> 멤버 변수 검사
        if isInsideClass:
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

            if vecMatch := _kMemberVectorRe.match(line):
                innerType = vecMatch.group(1).strip()
                varName = vecMatch.group(2)
                isByteVec = bool(re.search(r'\b(?:uint8|int8|utf8|char|byte)\b', innerType, re.IGNORECASE))
                hasByteWord = "byte" in varName.lower()

                if isByteVec and hasByteWord:
                    if varName.startswith("_list"):
                        suggested = "_" + varName[5].lower() + varName[6:]
                        violations.append(
                            ConventionViolation(
                                file_path=relPath,
                                line_number=lineNum,
                                rule_category="Naming/DynamicContainer",
                                message=f"바이트 벡터({innerType}) 멤버 변수 '{varName}'는 'byte' 단어가 포함된 경우 '_list' 접두어를 생략해야 합니다 ('{suggested}' 권장).",
                                snippet=trimmed,
                                suggested_fix=suggested,
                            )
                        )
                else:
                    if not varName.startswith("_list") and not varName.startswith("_s_list"):
                        violations.append(
                            ConventionViolation(
                                file_path=relPath,
                                line_number=lineNum,
                                rule_category="Naming/DynamicContainer",
                                message=f"동적 배열/벡터 멤버 변수 '{varName}'는 '_list' 접두어로 시작해야 합니다 ('List' 접미어 사용 불가).",
                                snippet=trimmed,
                            )
                        )

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

            for prefix in ("_list", "_map", "_arr"):
                match = re.match(
                    rf'^\s*(?:(?:sw::)?(?:vector|list|deque|unordered_map|map))\s*<[^>]+>\s+({prefix}[A-Z][a-zA-Z0-9_]*)\s*;',
                    line,
                )
                if match:
                    vName = match.group(1)
                    if isPluralWordInternal(vName):
                        singularFix = makeSingularInternal(vName)
                        violations.append(
                            ConventionViolation(
                                file_path=relPath,
                                line_number=lineNum,
                                rule_category="Naming/ContainerSingular",
                                message=f"컨테이너 멤버 변수 '{vName}'는 복수형 대신 단수형 명사를 사용해야 합니다 ('{singularFix}' 권장).",
                                snippet=trimmed,
                                suggested_fix=singularFix,
                            )
                        )

        # 중괄호 증감 및 스택 정리
        openBraces = trimmed.count("{")
        closeBraces = trimmed.count("}")
        braceDepth += openBraces - closeBraces

        while funcStack and braceDepth <= funcStack[-1] and "}" in trimmed:
            funcStack.pop()

        while classStack and braceDepth <= classStack[-1][1] and "}" in trimmed:
            classStack.pop()

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
    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8")
        except Exception:
            pass

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
