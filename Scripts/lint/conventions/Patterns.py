"""
CheckCodeConventions — 규칙들이 나눠 쓰는 정규식과 작은 글 도우미(복수형 · 괄호 나누기 · 반복자 짝 · include 경로 대소문자 표).

게이트 `Scripts/lint/gate/CheckCodeConventions.py` 의 한 묶음이다(묶음 지도는 `__init__.py`).
"""
from __future__ import annotations

import re
from pathlib import Path

from common import collectRepositoryFiles, kCppAllExtensions, kLintTargetRelDirs


# --- 4. 정규표현식 패턴 ------------------------------------------------------

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

# [익명 네임스페이스 헬퍼 이름 중복 검사]
# 정규식 패턴: r'^\s*struct\s+(\w+Internal)\s*$'
#   - ^\s*struct\s+   : 줄 시작의 struct 선언
#   - (\w+Internal)    : 컨벤션상 TU 전용 헬퍼 이름(AGENTS.md "Helpers: Util vs Internal") 캡처
#   - \s*$             : 같은 줄에 다른 토큰이 없는 순수 선언만 (전방 선언/한 줄 정의 제외)
# 용도: 유니티 빌드(SW_ENABLE_UNITY_BUILD, CI-Debug/CI-Shipping)는 .cpp 여러 개를 한 TU 로 묶는다.
#       익명 네임스페이스라도 같은 TU 안에서는 같은 이름이 재정의로 충돌한다 — 같은 클래스를 여러 .cpp 로
#       나눠 구현할 때 헬퍼를 클래스 이름으로 지으면 부딪힌다 — 헬퍼는 파일 이름으로 짓는다
#       (`VulkanRHIResourceFactoryPipeline.cpp` → `VulkanRHIResourceFactoryPipelineInternal`).
_kAnonHelperStructRe = re.compile(r'^\s*struct\s+(\w+Internal)\s*$', re.MULTILINE)
# 익명 네임스페이스 바로 안(구조체 · 함수 안이 아닌 곳)의 상수 선언 — `constexpr int32 kLimit = 4;` · `const string s_empty{};` · 배열.
_kBareConstantDeclRe = re.compile(r'^\s*(?:static\s+)?(?:inline\s+)?(?:constexpr|const)\b[^;(]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)?(?:=|\{)')

_s_exactPathMap: dict[str, str] = {}


def getExactPathMapInternal(projectRoot: Path) -> dict[str, str]:
    """
    저장소 내 모든 소스/헤더 파일의 실제 대소문자 경로 맵을 생성합니다(Source · Test · Tools 기준 상대 경로와 저장소 기준 경로 둘을 열쇠로).

    내려받은 외부 도구(`Tools/vcpkg` · `Tools/LLVM` …)로는 내려가지 않습니다(`collectRepositoryFiles`) — 거기까지 걸으면 폴더가 수천 개라
    파일 셋만 검사해도 1 초를 넘깁니다. 이 맵은 따옴표 include 에만 쓰이고 외부 헤더는 꺾쇠로 include 하므로 결과는 같습니다.
    """
    global _s_exactPathMap
    if not _s_exactPathMap:
        newMap = {}
        for fullPath in collectRepositoryFiles(projectRoot, kLintTargetRelDirs, suffixes=set(kCppAllExtensions) | {".inl"}):
            relRoot = fullPath.relative_to(projectRoot).as_posix()
            rel = relRoot.split("/", 1)[1]
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
    r'^\s*(?:const\s+)?[A-Za-z0-9_:]+(?:<[^>]+>)?\s*(\*{1,2})\s*(?:const\s+)?(_[a-zA-Z0-9_]*)\s*;'
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
    r'^\s*(?:float32|float64|int32|int64|uint8|uint16|uint32|uint64|char|utf8|bool)'
    r'\s+(_[a-zA-Z0-9_]*)\s*\[[^\]]+\]\s*;'
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
# 이 포매터가 파싱하지 못하는 printf 스펙.
#
# 포매터는 `% [flags] [width] [.precision] [length] conversion` 을 그대로 읽으므로 `%.3f` · `%05d` 는 허용한다.
# 막는 것은 정말로 못 읽는 둘이다:
#  - `*` (인자로 주는 동적 폭/정밀도) — 인자 개수가 서식에 따라 달라져 타입세이프 경로와 맞지 않는다.
#  - `%a` / `%A` (16진 부동소수) — 변환 자체가 구현되어 있지 않다.
# `%n` 은 값을 쓰는 스펙이라 애초에 지원 대상이 아니고 보안상으로도 막는다.
# 플래그 자리에 공백과 `#` 은 넣지 않는다 — `%#` 은 이 엔진의 플레이스홀더라, 그걸 플래그로 보면
# "%# not found" 의 n 까지 %n 으로 읽어 오탐이 난다.
_kBadPrintfSpecRe = re.compile(
    r'%[-+0]*(?:\*[.\d]*|\d*\.\*)[-+]*(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGaAcsp]'
    r'|%[-+0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t|L)?[aAn]'
)
_kStringLiteralRe = re.compile(r'"((?:\\.|[^"\\])*)"')

# [비복수형 예외 단어 목록]
kNonPluralWordEndings = (
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
    if any(word.endswith(exc) for exc in kNonPluralWordEndings):
        return False
    if word.endswith(("ies", "es", "s")) and not word.endswith("ss"):
        return True
    return False


# [줄마다 부르는 정규식 — 미리 컴파일한다]
# 함수 안에서 `re.search( r"...", x )` 로 문자열을 넘기면 부를 때마다 정규식 모듈의 캐시를 찾는다 — 전체 스캔 한 번에
# 백만 번이 넘어 초 단위가 된다. 줄마다 부르는 패턴은 여기서 미리 컴파일한다.
_kTrailingArraySuffixRe = re.compile(r'\[[^\]]*\]$')
_kTrailingIdentifierRe = re.compile(r'([A-Za-z0-9_]+)$')
# 식별자 뒤의 `(?![A-Za-z0-9_:])` 는 식별자를 끝까지 먹게 한다 — 없으면 반복 안의 `[...]+` 와 빈 `\s*` 가 식별자를 몇 조각으로든
# 나눌 수 있어 맞지 않는 입력에서 역추적이 지수로 는다(`const Name<T> ( &` 한 줄에 7 초). 받는 문자열 집합은 같다.
_kParameterTypeRe = re.compile(r'^(?:(?:const|volatile|register)\s+)?(?:[A-Za-z0-9_:]+(?![A-Za-z0-9_:])(?:<[^>]+>)?(?:\s*[*&]+)?\s*)+$')
_kTemplateArgumentRe = re.compile(r'<[^>]*>')
_kStringLiteralStripRe = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
_kFunctionSignatureTailRe = re.compile(r'\([a-zA-Z0-9_,\s*&:<>=./"-]*\)\s*(?:const|override|final|noexcept|SW_\w*API)*\s*[{;=]')
_kLocalUnderscoreDeclRe = re.compile(r'^\s*(?:const\s+|static\s+|constexpr\s+|auto\s+)?(?:[A-Za-z0-9_:]+(?:<[^;]+>)?\s*[*&]*\s+)(_[a-zA-Z0-9]+)\s*(?:=|;|,|\{|\()')
_kCallLikeNameRe = re.compile(r'\b[A-Za-z0-9_:]+\s*\(')
_kCommentStripRe = re.compile(r"//.*$|/\*.*?\*/")
_kLambdaParameterRe = re.compile(r'\[[^\]]*\]\s*\(([^)]*)\)')
_kFunctionSignatureRe = re.compile(r'^\s*(?:(?:inline|static|virtual|explicit|constexpr|friend|SW_\w*API)\s+)*(?:(?:const\s+)?[A-Za-z0-9_:]+(?:<[^;]+>)?(?:\s*[*&]+)?\s+)+([A-Za-z0-9_:]+)\s*\(([^)]*)\)')
_kConstructorSignatureRe = re.compile(r'^\s*(?:explicit\s+)?([A-Za-z0-9_]+)(?:::([A-Za-z0-9_]+))?\s*\(([^)]*)\)')
_kConstructorDefinitionRe = re.compile(r'\b([A-Za-z0-9_]+)::\1\s*\([^)]*\)')
_kInitListTrailingMemberRe = re.compile(r'[\}\)]\s*,\s*_[a-zA-Z0-9_]+')
_kConstructorDefinitionLineRe = re.compile(r"^\s*([A-Z]\w*)::\1\s*\(")
_kPointerNamePrefixRe = re.compile(r'^_?p[A-Z]')
_kBoolNamePrefixRe = re.compile(r'^_?b[A-Z]')

# [반복자 쌍 인자]
# `x.begin(), x.end()` · `x->cbegin(), x->cend()` · `std::begin( x ), std::end( x )` — 같은 대상의 시작 · 끝 호출.
# `first, last` · `pBegin, pEnd` 처럼 이름 끝 단어가 Begin/First 와 End/Last 인 식별자 둘도 반복자 쌍으로 본다(소괄호 예외에만).
_kIteratorBeginMemberCallRe = re.compile(r'^(?P<object>[A-Za-z_][\w.:\->\[\]]*?)\s*(?:\.|->)\s*c?r?begin\s*\(\s*\)$')
_kIteratorEndMemberCallRe = re.compile(r'^(?P<object>[A-Za-z_][\w.:\->\[\]]*?)\s*(?:\.|->)\s*c?r?end\s*\(\s*\)$')
_kIteratorBeginFreeCallRe = re.compile(r'^(?:std::)?c?r?begin\s*\(\s*(?P<object>[^()]+?)\s*\)$')
_kIteratorEndFreeCallRe = re.compile(r'^(?:std::)?c?r?end\s*\(\s*(?P<object>[^()]+?)\s*\)$')
_kIteratorBeginNameRe = re.compile(r'^(?:[a-z]\w*(?:Begin|First)|begin|first)$')
_kIteratorEndNameRe = re.compile(r'^(?:[a-z]\w*(?:End|Last)|end|last)$')
_kBraceGroupRe = re.compile(r'\{([^{}]*)\}')


def splitTopLevelCommasInternal(text: str) -> list[str]:
    """괄호 · 대괄호 · 중괄호 밖의 쉼표로 나눕니다(꺾쇠는 `->` 와 섞여 세지 않는다)."""
    listPart: list[str] = []
    depth = 0
    start = 0
    for index, ch in enumerate(text):
        if ch in "([{":
            depth += 1
        elif ch in ")]}" and depth > 0:
            depth -= 1
        elif ch == "," and depth == 0:
            listPart.append(text[start:index].strip())
            start = index + 1
    listPart.append(text[start:].strip())
    return listPart


def isIteratorCallPairInternal(argumentText: str) -> bool:
    """인자가 같은 대상의 begin/end 호출 두 개인지 봅니다(`x.begin(), x.end()`)."""
    listPart = splitTopLevelCommasInternal(argumentText)
    if len(listPart) != 2:
        return False
    for beginRe, endRe in ((_kIteratorBeginMemberCallRe, _kIteratorEndMemberCallRe),
                           (_kIteratorBeginFreeCallRe, _kIteratorEndFreeCallRe)):
        beginMatch = beginRe.match(listPart[0])
        endMatch = endRe.match(listPart[1])
        if beginMatch is not None and endMatch is not None and beginMatch.group("object") == endMatch.group("object"):
            return True
    return False


def isIteratorPairInternal(argumentText: str) -> bool:
    """인자가 반복자 쌍인지 봅니다 — begin/end 호출 쌍이거나 `first, last` 같은 이름 쌍."""
    if isIteratorCallPairInternal(argumentText):
        return True
    listPart = splitTopLevelCommasInternal(argumentText)
    return (len(listPart) == 2
            and _kIteratorBeginNameRe.match(listPart[0]) is not None
            and _kIteratorEndNameRe.match(listPart[1]) is not None)


def extractParenthesizedInternal(text: str) -> str | None:
    """`( ... )` 로 시작하는 텍스트에서 짝이 맞는 괄호 안쪽을 돌려줍니다. 짝이 없으면 None."""
    if text.startswith("(") is False:
        return None
    depth = 0
    for index, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return text[1:index]
    return None



# [연관 컨테이너(맵) 멤버 변수 명명 검사]
# 정규식 패턴: r'^\s*(?:(?:sw::)?(?:unordered_map|map))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
#   - ^\s*(?:(?:sw::)?(?:unordered_map|map)) : map 또는 unordered_map 매칭
#   - <[^>]+>                                : 템플릿 인자 (<Key, Value>) 매칭
#   - \s+(_[a-zA-Z0-9_]+)\s*;                : 멤버 변수명 캡처
# 매칭 예시 (위반): unordered_map<string, int32> _table; (위반 -> _mapTable 이어야 함)
# 올바른 예시: unordered_map<string, int32> _mapTable;, map<int32, Actor*> _mapIDToActor;
# 컨벤션 규칙: 연관 컨테이너 멤버는 '_map' 접두어로 시작해야 합니다.
_kMemberMapRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:unordered_map|map))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
)

# [고유 집합(세트) 컨테이너 멤버 변수 명명 검사]
# 정규식 패턴: r'^\s*(?:(?:sw::)?(?:unordered_set|set))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
#   - ^\s*(?:(?:sw::)?(?:unordered_set|set)) : set 또는 unordered_set 매칭
#   - <[^>]+>                                : 템플릿 인자 (<Key>) 매칭
#   - \s+(_[a-zA-Z0-9_]+)\s*;                : 멤버 변수명 캡처
# 매칭 예시 (위반): set<uint32> _ids; (위반 -> _uniqueIDs 이어야 함)
# 올바른 예시: set<uint32> _uniqueIDs;, unordered_set<string> _uniqueTags;
# 컨벤션 규칙: 고유 집합 멤버는 '_unique' 접두어로 시작하며, 컨테이너 중 유일하게 복수형 명사(_uniqueIDs)가 허용됩니다.
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

# [placement new 표기 검사]
# 정규식 패턴: r'(?<!\w)new\s*\('
#   - (?<!\w)  : 앞이 식별자 글자가 아니어야 한다 — `sw_new` · `sw_placement_new(` 는 여기서 빠진다
#   - new\s*\( : `new` 바로 뒤에 괄호가 오는 형태, 즉 placement 구문
# `operator new(` 선언과 `#define` 줄은 규칙 쪽에서 거른다.
# 매칭 예시 (위반): new ( pMemory ) T();, ::new ( &storage ) T( value );
# 올바른 예시: sw_placement_new( pMemory ) T();, sw_new T();
# 컨벤션 규칙: 이미 잡아 둔 메모리에 객체를 만들 때는 Memory.h 의 sw_placement_new 를 씁니다.
_kRawPlacementNewRe = re.compile(r'(?<!\w)new\s*\(')

# [맨 new 검사]
# 정규식 패턴: r'(?<!\w)new\s+(?=[A-Za-z_:])'
#   - (?<!\w)               : 앞이 식별자 글자가 아니어야 한다 — `sw_new T` 는 여기서 빠진다(`::new T` 는 잡힌다)
#   - new\s+(?=[A-Za-z_:])  : `new` 뒤에 타입 이름이 오는 형태(`new T` · `new T[n]` · `new T( ... )` · `new ::ns::T`)
# placement 구문(`new (`)은 Style/PlacementNew 가 본다. `operator new` 선언과 `#define` 줄은 규칙 쪽에서 거른다.
# 매칭 예시 (위반): pChunk = new T[count]{};, pPage = new Page;
# 올바른 예시: sw_new Page, sw_new_array<T>( count ), make_unique<T>( ... )
# 컨벤션 규칙: 힙 객체는 sw 할당자(Memory.h)로 만든다. CRT new 는 메모리 태그 · 누수 검사에 보이지 않는다.
_kRawNewRe = re.compile(r'(?<!\w)new\s+(?=[A-Za-z_:])')
_kOperatorKeywordTailRe = re.compile(r'\boperator\s*$')

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
# 정규식 패턴: r'^\s*(?:\[\[[^\]]*\]\]\s*)?(?:(?:alignas\([^)]*\)|mutable|static|inline|const|volatile|constexpr)\s+)*(?:[A-Za-z0-9_:]+(?:<[^;]+>)?(?:\s*(?:[\*&]|const\b))*\s+)(_[a-zA-Z0-9_]+)\s*(?::\s*\d+)?\s*(?:\[[^\]]*\])?\s*(?:\{[^}]*\}|\([^)]*\))?\s*(?:=\s*[^;]+)?\s*;'
#   - 클래스 내부에서 '_'로 시작하는 모든 멤버 변수 선언의 순서를 순차 추출합니다.
#   - 타입 뒤의 `*` · `&` · `const` 는 몇 번이든 온다(`Widget* const* _ppWidget`). 못 읽은 멤버가 생성자 목록에 있으면 순서 위반으로 잘못 걸린다.
_kClassMemberRe = re.compile(
    r'^\s*(?:\[\[[^\]]*\]\]\s*)?(?:(?:alignas\([^)]*\)|mutable|static|inline|const|volatile|constexpr)\s+)*(?:[A-Za-z0-9_:]+(?:<[^;]+>)?(?:\s*(?:[\*&]|const\b))*\s+)(_[a-zA-Z0-9_]+)\s*(?::\s*\d+)?\s*(?:\[[^\]]*\])?\s*(?:\{[^}]*\}|\([^)]*\))?\s*(?:=\s*[^;]+)?\s*;'
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
