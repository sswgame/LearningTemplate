"""
CheckCodeConventions — 줄 단위 규칙 구현 — 규칙 하나 = `ConventionRule` 하위 클래스 하나. import 하면 레지스트리에 오른다(`FileScan` 이 import 한다).

게이트 `Scripts/lint/gate/CheckCodeConventions.py` 의 한 묶음이다(묶음 지도는 `__init__.py`).
"""
from __future__ import annotations

import re

from .Model import ConventionRule, ConventionViolation, LineScanContext
from .NamingVocabulary import kMapNamingSubject
from .Patterns import (
    _kBadPrintfSpecRe,
    _kBasicTypesRe,
    _kBoolNamePrefixRe,
    _kBraceGroupRe,
    _kConstantNamingRe,
    _kExplicitTrueRe,
    _kFormatterCallRe,
    _kImplicitPointerNullRe,
    _kIncludePathRe,
    _kInitListTrailingMemberRe,
    _kLiteralAutoRe,
    _kLoopIndexRe,
    _kMemberFixedArrayRe,
    _kMemberMapRe,
    _kMemberRawPointerRe,
    _kMemberSetRe,
    _kMemberVectorRe,
    _kNegatedConditionRe,
    _kOperatorKeywordTailRe,
    _kOutParamNamingRe,
    _kPchIncludeRe,
    _kPointerNamePrefixRe,
    _kRawNewRe,
    _kRawPlacementNewRe,
    _kStringLiteralRe,
    _kTriplePointerRe,
    extractParenthesizedInternal,
    getExactPathMapInternal,
    isIteratorCallPairInternal,
    isIteratorPairInternal,
)
from .NamingChecks import (
    checkContainerNamingInternal,
    checkLocalVariableItemInternal,
    checkParameterItemInternal,
    checkPointerNamingInternal,
    getSuggestedOutParamFixInternal,
)


# --- 줄 단위 규칙 구현 ---------------------------------------------------------

# 줄 규칙의 `"글자" in ctx.line and 정규식` 앞부분은 그 정규식이 반드시 품는 글자다 — 없는 줄에는 정규식을 부르지 않는다(결과는 같다).
# 정규식을 바꿀 때 그 글자가 여전히 필수인지 함께 볼 것.


class IncludePathCasingRule( ConventionRule ):
    """Include/PathCasing"""
    category = "Include/PathCasing"

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 인클루드 경로 파일명 및 대소문자 일치 검사
        if includeMatch := _kIncludePathRe.match(ctx.trimmed):
            includeType = includeMatch.group(1)
            includePath = includeMatch.group(2).replace("\\", "/")
            if includeType == '"' and includePath != "pch.h":
                exactMap = getExactPathMapInternal(ctx.rootDir)
                includeLower = includePath.lower()
                if includeLower in exactMap:
                    exactPath = exactMap[includeLower]
                    if includePath != exactPath:
                        violations.append(
                            ConventionViolation(
                                file_path=ctx.relPath,
                                line_number=ctx.lineNum,
                                rule_category="Include/PathCasing",
                                message=f"인클루드 경로 '{includePath}'의 대소문자가 실제 파일 시스템 경로 '{exactPath}'와 일치하지 않습니다.",
                                snippet=ctx.trimmed,
                                suggested_fix=f'#include "{exactPath}"',
                            )
                        )
        return violations


class LoopVariableNameRule( ConventionRule ):
    """Naming/LoopVariable"""
    category = "Naming/LoopVariable"
    badSampleFile = "Source/Probe/LoopVariable.cpp"
    badSample = '#include "pch.h"\n\nvoid probe()\n{\n    for ( int32 i = 0; i < 4; ++i )\n    {\n    }\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 단일 문자 루프 카운터(i, j, k) 검사
        if "for" in ctx.line and (loopMatch := _kLoopIndexRe.search(ctx.line)):
            violations.append(
                ConventionViolation(
                    file_path=ctx.relPath,
                    line_number=ctx.lineNum,
                    rule_category="Naming/LoopVariable",
                    message=f"단일 문자 루프 변수 '{loopMatch.group(1)}' 사용이 검출되었습니다. 의미 있는 이름(예: index, childIndex)을 사용하세요.",
                    snippet=ctx.trimmed,
                )
            )
        return violations


class AutoOnLiteralRule( ConventionRule ):
    """Style/AutoUsage"""
    category = "Style/AutoUsage"
    badSampleFile = "Source/Probe/AutoUsage.cpp"
    badSample = '#include "pch.h"\n\nvoid probe()\n{\n    auto name = "Probe";\n    (void)name;\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 리터럴/원시 타입 직접 대입 시 auto 사용 검사
        if "auto" in ctx.line and (autoMatch := _kLiteralAutoRe.search(ctx.line)):
            violations.append(
                ConventionViolation(
                    file_path=ctx.relPath,
                    line_number=ctx.lineNum,
                    rule_category="Style/AutoUsage",
                    message=f"명시적 리터럴/원시 타입 대입 변수 '{autoMatch.group(1)}'에 auto를 사용하지 마세요.",
                    snippet=ctx.trimmed,
                )
            )
        return violations


class LogFormatSpecRule( ConventionRule ):
    """Style/LogFormatSpec"""
    category = "Style/LogFormatSpec"
    badSampleFile = "Source/Probe/LogFormat.cpp"
    badSample = '#include "pch.h"\n\nvoid probe( int32 width, int32 count )\n{\n    SW_LOG_INFO( "count=%*d", width, count );\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 타입세이프 포매터가 못 읽는 스펙 검사 (동적 폭 `%*d`, 16진 부동소수 `%a`, `%n`)
        if ("SW_" in ctx.line or "ormat" in ctx.line) and _kFormatterCallRe.search(ctx.line):
            for strMatch in _kStringLiteralRe.finditer(ctx.line):
                badSpec = _kBadPrintfSpecRe.search(strMatch.group(1))
                if badSpec:
                    violations.append(
                        ConventionViolation(
                            file_path=ctx.relPath,
                            line_number=ctx.lineNum,
                            rule_category="Style/LogFormatSpec",
                            message=(
                                f"타입세이프 포매터가 파싱하지 못하는 printf 스펙 '{badSpec.group(0)}'이(가) 있습니다. "
                                "동적 폭/정밀도(*)와 %a/%n 은 지원하지 않습니다 — "
                                "`%#` 플레이스홀더 + Fmt(값, Format()...) 로 쓰세요. "
                                "폭·정밀도·플래그(%.3f, %05d, %-8s)는 이제 그대로 쓸 수 있습니다."
                            ),
                            snippet=ctx.trimmed,
                        )
                    )
                    break
        return violations


class ExplicitTrueCompareRule( ConventionRule ):
    """Style/ExplicitTrueCheck"""
    category = "Style/ExplicitTrueCheck"
    badSampleFile = "Source/Probe/ExplicitTrue.cpp"
    badSample = '#include "pch.h"\n\nvoid probe( bool bValid )\n{\n    if ( bValid == true )\n    {\n    }\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 불필요한 '== true' 명시 검사
        if "true" in ctx.line and _kExplicitTrueRe.search(ctx.line):
            violations.append(
                ConventionViolation(
                    file_path=ctx.relPath,
                    line_number=ctx.lineNum,
                    rule_category="Style/ExplicitTrueCheck",
                    message="불리언을 '== true'와 명시적으로 비교하지 마세요. 'if (bValid)' 형태를 사용하세요.",
                    snippet=ctx.trimmed,
                )
            )
        return violations


class SingleAnonymousNamespaceRule( ConventionRule ):
    """Structure/AnonymousNamespaceCount"""
    category = "Structure/AnonymousNamespaceCount"
    badSampleFile = "Source/Probe/TwoAnonymous.cpp"
    badSample = (
        '#include "pch.h"\n'
        "\n"
        "namespace sw\n"
        "{\n"
        "    namespace\n"
        "    {\n"
        "        int32 first = 0;\n"
        "    } // namespace\n"
        "\n"
        "    namespace\n"
        "    {\n"
        "        int32 second = 0;\n"
        "    } // namespace\n"
        "} // namespace sw\n"
    )

    def __init__(self) -> None:
        # **파일별로 나눠 둔다.** 규칙 객체는 하나인데 게이트는 파일을 **동시에** 훑는다 — 상태를
        # 객체에 그냥 두면 파일 사이에 섞여 같은 트리에서 결과가 매번 달라진다. 한 파일의 줄은
        # 한 일꾼이 순서대로 보므로, 경로로 칸을 나누면 그 안은 안전하다.
        self._stateByFile: dict[str, list[int]] = {}

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        # AGENTS.md "One anonymous namespace per file" — `.cpp` 하나에 익명 네임스페이스는 하나다.
        # 여럿이면 번역 단위 지역 헬퍼가 흩어지고, 유니티 빌드에서 이름이 겹칠 자리가 는다.
        if ctx.isSource is False:
            return []

        state = self._stateByFile.setdefault(ctx.relPath, [0, 0])  # [블록 수, 전처리 분기 깊이]

        if ctx.trimmed.startswith("#if"):
            state[1] += 1
        elif ctx.trimmed.startswith("#endif") and state[1] > 0:
            state[1] -= 1

        if ctx.trimmed != "namespace":
            return []

        # 서로 배타적인 전처리 분기 안의 블록들은 번역 단위마다 하나씩이라 정당한 예외다
        # (AGENTS.md 가 그렇게 적고 있다). 그래서 분기 밖의 것만 센다.
        if state[1] > 0:
            return []

        state[0] += 1
        if state[0] <= 1:
            return []

        return [
            ConventionViolation(
                file_path=ctx.relPath,
                line_number=ctx.lineNum,
                rule_category=self.category,
                message="익명 네임스페이스는 파일당 하나입니다. 번역 단위 지역 헬퍼는 맨 위 한 블록에 모으세요.",
                snippet=ctx.trimmed,
            )
        ]


class NegatedConditionRule( ConventionRule ):
    """Style/NegatedComparison"""
    category = "Style/NegatedComparison"
    badSampleFile = "Source/Probe/Negated.cpp"
    badSample = '#include "pch.h"\n\nvoid probe( int32* pActor )\n{\n    if ( !pActor )\n    {\n    }\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 부정(!expr) 조건문 검사
        if "!" in ctx.line and (negatedMatch := _kNegatedConditionRe.search(ctx.line)):
            expr = negatedMatch.group(1).strip()
            if _kPointerNamePrefixRe.match(expr) or "->" in expr:
                msg = f"포인터 부정 조건 'if ( !{expr} )' 대신 명시적 'if ( {expr} == nullptr )' 비교를 사용하세요."
            elif expr.endswith(".empty()") or expr.endswith(".contains()"):
                msg = f"상태 부정 조건 'if ( !{expr} )' 대신 명시적 'if ( {expr} == false )' 비교를 사용하세요."
            elif _kBoolNamePrefixRe.match(expr) or expr.startswith(("is", "has", "can")):
                msg = f"불리언 부정 조건 'if ( !{expr} )' 대신 명시적 'if ( {expr} == false )' 비교를 사용하세요."
            else:
                msg = f"부정 연산자 'if ( !{expr} )' 대신 명시적 비교('== false' 또는 '== nullptr')를 사용하세요."

            violations.append(
                ConventionViolation(
                    file_path=ctx.relPath,
                    line_number=ctx.lineNum,
                    rule_category="Style/NegatedComparison",
                    message=msg,
                    snippet=ctx.trimmed,
                )
            )
        return violations


class ImplicitPointerNullRule( ConventionRule ):
    """Style/ImplicitPointerNullCheck"""
    category = "Style/ImplicitPointerNullCheck"
    badSampleFile = "Source/Probe/ImplicitNull.cpp"
    badSample = '#include "pch.h"\n\nvoid probe()\n{\n    if ( getOwner() )\n    {\n    }\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 암시적 포인터 널 검사
        if "get" in ctx.line and (ptrMatch := _kImplicitPointerNullRe.search(ctx.line)):
            violations.append(
                ConventionViolation(
                    file_path=ctx.relPath,
                    line_number=ctx.lineNum,
                    rule_category="Style/ImplicitPointerNullCheck",
                    message=f"암시적 포인터 검사 '{ptrMatch.group(1)}'가 검출되었습니다. 명시적 '!= nullptr' 비교를 사용하세요.",
                    snippet=ctx.trimmed,
                )
            )
        return violations


class ConstantNameRule( ConventionRule ):
    """Naming/Constant"""
    category = "Naming/Constant"
    badSampleFile = "Source/Probe/Constant.cpp"
    badSample = '#include "pch.h"\n\nstatic constexpr int32 MAX_COUNT = 4;\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 상수 네이밍 검사
        if "constexpr" in ctx.line and (constMatch := _kConstantNamingRe.search(ctx.line)):
            varName = constMatch.group(1)
            if "Math" not in ctx.relPath:
                if not varName.startswith("k") or (len(varName) > 1 and not varName[1].isupper()):
                    violations.append(
                        ConventionViolation(
                            file_path=ctx.relPath,
                            line_number=ctx.lineNum,
                            rule_category="Naming/Constant",
                            message=f"상수 '{varName}'는 kPascalCase 명명 규칙을 따라야 합니다.",
                            snippet=ctx.trimmed,
                        )
                    )
        return violations


class BasicTypeAliasRule( ConventionRule ):
    """Style/BasicTypeAlias"""
    category = "Style/BasicTypeAlias"
    badSampleFile = "Source/Probe/BasicType.cpp"
    badSample = '#include "pch.h"\n\nvoid probe()\n{\n    unsigned int count = 0u;\n    (void)count;\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 원시 기본 자료형 사용 검사
        if not ctx.trimmed.startswith("#") and "int main" not in ctx.trimmed and "Types.h" not in ctx.relPath:
            if basicTypeMatch := _kBasicTypesRe.search(ctx.codeWithoutStrings):
                typeName = basicTypeMatch.group(0)
                violations.append(
                    ConventionViolation(
                        file_path=ctx.relPath,
                        line_number=ctx.lineNum,
                        rule_category="Style/BasicTypeAlias",
                        message=f"기본 자료형 '{typeName}' 대신 Types.h의 별칭(int32, float32 등)을 사용하세요.",
                        snippet=ctx.trimmed,
                    )
                )
        return violations


class PlacementNewRule( ConventionRule ):
    """Style/PlacementNew"""
    category = "Style/PlacementNew"
    badSampleFile = "Source/Probe/PlacementNew.cpp"
    badSample = '#include "pch.h"\n\nvoid probe( void* pMemory )\n{\n    new ( pMemory ) int32( 0 );\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 매크로 정의(`sw_placement_new` · `sw_new` 자신)는 맨 new 를 쓸 수밖에 없다.
        if ctx.trimmed.startswith("#") or "new" not in ctx.codeWithoutStrings:
            return violations
        for newMatch in _kRawPlacementNewRe.finditer(ctx.codeWithoutStrings):
            # `operator new( size_t, ... )` 는 할당 함수 선언이지 객체 생성이 아니다.
            if _kOperatorKeywordTailRe.search(ctx.codeWithoutStrings[:newMatch.start()]):
                continue
            violations.append(
                ConventionViolation(
                    file_path=ctx.relPath,
                    line_number=ctx.lineNum,
                    rule_category="Style/PlacementNew",
                    message=(
                        "placement new 는 `sw_placement_new( p ) T( ... )` 로 쓰세요(Core/Memory/Memory.h). "
                        "매크로는 주소를 `void*` 로 바꾸는 캐스트를 드러내고, 표기가 하나여야 한 곳만 고쳐 전체에 반영됩니다."
                    ),
                    snippet=ctx.trimmed,
                )
            )
            break
        return violations


class RawNewRule( ConventionRule ):
    """Style/RawNew"""
    category = "Style/RawNew"
    badSampleFile = "Source/Probe/RawNew.cpp"
    badSample = '#include "pch.h"\n\nvoid probe()\n{\n    int32* pArray = new int32[4]{};\n    (void)pArray;\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 매크로 정의(`sw_new` 자신)는 맨 new 를 쓸 수밖에 없다.
        if ctx.trimmed.startswith("#") or "new" not in ctx.codeWithoutStrings:
            return violations
        for newMatch in _kRawNewRe.finditer(ctx.codeWithoutStrings):
            # `operator new[]( size_t, ... )` 는 할당 함수 선언이지 객체 생성이 아니다.
            if _kOperatorKeywordTailRe.search(ctx.codeWithoutStrings[:newMatch.start()]):
                continue
            violations.append(
                ConventionViolation(
                    file_path=ctx.relPath,
                    line_number=ctx.lineNum,
                    rule_category="Style/RawNew",
                    message=(
                        "맨 `new` 대신 sw 할당자를 쓰세요 — 객체는 `sw_new T( ... )` · `make_unique<T>`, 배열은 `sw_new_array<T>( n )` · "
                        "`make_unique<T[]>( n )` · `vector<T>`(Core/Memory/Memory.h). CRT new 는 메모리 태그 · 누수 검사에 보이지 않습니다."
                    ),
                    snippet=ctx.trimmed,
                )
            )
            break
        return violations


class PchIncludeRule( ConventionRule ):
    """Include/PCH"""
    category = "Include/PCH"
    scope = "file"
    badSampleFile = "Source/Probe/NoPch.cpp"
    badSample = '#include "Engine/EngineMinimal.h"\n\nvoid probe() {}\n'

    def onFile(self, ctx: LineScanContext, listLine: list[str]) -> list[ConventionViolation]:
        # .cpp 의 첫 코드 줄은 `#include "pch.h"` 다.
        if ctx.isSource is False:
            return []
        for lineNum, line in enumerate(listLine, start=1):
            trimmed = line.strip()
            if not trimmed or trimmed.startswith(("//", "/*", "*")):
                continue
            if _kPchIncludeRe.match(trimmed):
                return []
            return [
                ConventionViolation(
                    file_path=ctx.relPath,
                    line_number=lineNum,
                    rule_category=self.category,
                    message='.cpp 소스 파일의 첫 번째 인클루드는 반드시 #include "pch.h" 이어야 합니다.',
                    snippet=trimmed,
                )
            ]
        return []


class ParameterNamingRule( ConventionRule ):
    """Naming/ParameterNoUnderscore · Naming/ParameterPointer · Naming/ParameterContainer"""
    categories = ("Naming/ParameterNoUnderscore", "Naming/ParameterPointer", "Naming/ParameterContainer")
    scope = "parameter"
    badSampleFile = "Source/Probe/ParamUnderscore.cpp"
    badSample = '#include "pch.h"\n\nvoid probe( int32 _count )\n{\n    (void)_count;\n}\n'
    extraSamples = (
        ("Source/Probe/ParamPointer.cpp", '#include "pch.h"\n\nvoid probe( int32* value )\n{\n    (void)value;\n}\n'),
        ("Source/Probe/ParamContainer.cpp", '#include "pch.h"\n\nvoid probe( const vector<int32>& items )\n{\n    (void)items;\n}\n'),
    )

    def onParameter(self, ctx: LineScanContext, parameterText: str) -> list[ConventionViolation]:
        # 판정은 주체 셋이 함께 쓰는 어휘 표(`kMapNamingSubject["parameter"]`)를 읽는다.
        return checkParameterItemInternal(parameterText, ctx.relPath, ctx.lineNum, ctx.trimmed)


class LocalVariableNamingRule( ConventionRule ):
    """Naming/LocalNoUnderscore · Naming/LocalPointer · Naming/LocalContainer"""
    categories = ("Naming/LocalNoUnderscore", "Naming/LocalPointer", "Naming/LocalContainer")
    scope = "functionLocal"
    badSampleFile = "Source/Probe/LocalUnderscore.cpp"
    badSample = '#include "pch.h"\n\nvoid probe()\n{\n    int32 _count = 0;\n    (void)_count;\n}\n'
    extraSamples = (
        ("Source/Probe/LocalPointer.cpp", '#include "pch.h"\n\nvoid probe()\n{\n    int32* value = nullptr;\n    (void)value;\n}\n'),
        ("Source/Probe/LocalContainer.cpp", '#include "pch.h"\n\nvoid probe()\n{\n    vector<int32> items;\n    (void)items;\n}\n'),
    )

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        return checkLocalVariableItemInternal(ctx.line, ctx.relPath, ctx.lineNum)


class ConstructorBracesRule( ConventionRule ):
    """Style/ConstructorBraces"""
    category = "Style/ConstructorBraces"
    scope = "constructorInitializer"
    badSampleFile = "Source/Probe/CtorBraces.cpp"
    badSample = '#include "pch.h"\n\nProbe::Probe()\n    : _count( 0 )\n{\n}\n'

    def onInitializerLine(self, ctx: LineScanContext, initMatch: re.Match | None) -> list[ConventionViolation]:
        if initMatch is None:
            return []
        initVar = initMatch.group(1)
        initVal = initMatch.group(2)
        if initVal.startswith("(") is False or initVar.startswith("super") or initVar.endswith("Base") or "_" not in initVar:
            return []
        # 반복자 쌍은 소괄호여야 한다 — 중괄호면 initializer_list 생성자가 이겨 반복자 자체를 값으로 담는다.
        initArgument = extractParenthesizedInternal(ctx.trimmed[initMatch.start(2):])
        if initArgument is not None and isIteratorPairInternal(initArgument):
            return []
        return [
            ConventionViolation(
                file_path=ctx.relPath,
                line_number=ctx.lineNum,
                rule_category=self.category,
                message=f"생성자 멤버 초기화 '{initVar}'는 소괄호 '()' 대신 중괄호 '{{}}'를 사용해야 합니다.",
                snippet=ctx.trimmed,
            )
        ]


class ConstructorOnePerLineRule( ConventionRule ):
    """Style/ConstructorOnePerLine"""
    category = "Style/ConstructorOnePerLine"
    scope = "constructorInitializer"
    badSampleFile = "Source/Probe/CtorOnePerLine.cpp"
    badSample = '#include "pch.h"\n\nProbe::Probe()\n    : _count{ 0 }, _other{ 1 }\n{\n}\n'

    def onInitializerLine(self, ctx: LineScanContext, initMatch: re.Match | None) -> list[ConventionViolation]:
        trimmed = ctx.trimmed
        if trimmed.startswith((":", ",")) is False or "," not in trimmed[1:] or _kInitListTrailingMemberRe.search(trimmed) is None:
            return []
        return [
            ConventionViolation(
                file_path=ctx.relPath,
                line_number=ctx.lineNum,
                rule_category=self.category,
                message="생성자 멤버 초기화는 한 줄에 하나의 변수만 와야 하며, 다음 줄에 ','로 시작해야 합니다.",
                snippet=trimmed,
            )
        ]


class IteratorPairBracesRule( ConventionRule ):
    """Style/IteratorPairBraces"""
    category = "Style/IteratorPairBraces"
    badSampleFile = "Source/Probe/IteratorPairBraces.cpp"
    badSample = (
        '#include "pch.h"\n\nProbe::Probe( std::initializer_list<int32> listValue )\n'
        '    : _listValue{ listValue.begin(), listValue.end() }\n{\n}\n'
    )

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        # 반복자 쌍을 중괄호로 넘기면 받는 쪽에 initializer_list 생성자가 있을 때 그것이 이긴다 — 반복자 둘이 원소 둘로
        # 담긴다(값 타입이 반복자로부터 만들어질 수 있으면 컴파일도 된다). 반복자 쌍은 소괄호로 넘긴다.
        if ctx.trimmed.startswith("#"):
            return []
        for braceMatch in _kBraceGroupRe.finditer(ctx.codeWithoutStrings):
            if isIteratorCallPairInternal(braceMatch.group(1)):
                return [
                    ConventionViolation(
                        file_path=ctx.relPath,
                        line_number=ctx.lineNum,
                        rule_category=self.category,
                        message=(
                            "반복자 쌍은 소괄호로 넘기세요: `( x.begin(), x.end() )`. 중괄호면 initializer_list 생성자가 "
                            "골라져 반복자 둘이 원소로 담길 수 있습니다."
                        ),
                        snippet=ctx.trimmed,
                    )
                ]
        return []


class TriplePointerRule( ConventionRule ):
    """Naming/TriplePointer"""
    category = "Naming/TriplePointer"
    badSampleFile = "Source/Probe/TriplePointer.cpp"
    badSample = '#include "pch.h"\n\nvoid probe( int32*** pppValue )\n{\n    (void)pppValue;\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 삼중 포인터 이상(ppp, ***) 검사
        if not ctx.trimmed.startswith("#") and "Types.h" not in ctx.relPath:
            code = ctx.codeWithoutStrings
            if ("ppp" in code or "*" in code) and (tripleMatch := _kTriplePointerRe.search(code)):
                matchedStr = tripleMatch.group(0)
                violations.append(
                    ConventionViolation(
                        file_path=ctx.relPath,
                        line_number=ctx.lineNum,
                        rule_category="Naming/TriplePointer",
                        message=f"삼중 포인터 이상('{matchedStr}') 사용이 검출되었습니다. 구조적 결함이므로 데이터 구조를 재설계하세요.",
                        snippet=ctx.trimmed,
                    )
                )
        return violations


class OutParameterNameRule( ConventionRule ):
    """Naming/OutParameter"""
    category = "Naming/OutParameter"
    badSampleFile = "Source/Probe/OutParameter.cpp"
    badSample = '#include "pch.h"\n\nvoid probe( int32* outPValue )\n{\n    *outPValue = 1;\n}\n'

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        # 출력 매개변수 명명 규칙 검사
        # 이름 후보(`_kOutParamNamingRe`)는 전부 `out` 이나 `Out` 을 품는다 — 없는 줄에는 식별자마다 역추적하는 정규식을 돌리지 않는다.
        code = ctx.codeWithoutStrings
        if not ctx.trimmed.startswith("#") and "Types.h" not in ctx.relPath and ("out" in code or "Out" in code):
            for outMatch in _kOutParamNamingRe.finditer(code):
                paramCandidate = outMatch.group(1)
                fixResult = getSuggestedOutParamFixInternal(paramCandidate)
                if fixResult is not None:
                    suggestedFix, fixMsg = fixResult
                    violations.append(
                        ConventionViolation(
                            file_path=ctx.relPath,
                            line_number=ctx.lineNum,
                            rule_category="Naming/OutParameter",
                            message=f"{fixMsg} ('{suggestedFix}' 권장)",
                            snippet=ctx.trimmed,
                            suggested_fix=suggestedFix,
                        )
                    )
        return violations

class ClassMemberNamingRule( ConventionRule ):
    """Naming/ContainerSingular · Naming/DynamicContainer · Naming/FixedArray · Naming/MapContainer · Naming/RawPointer · Naming/SetContainer"""
    categories = (
        "Naming/ContainerSingular",
        "Naming/DynamicContainer",
        "Naming/FixedArray",
        "Naming/MapContainer",
        "Naming/RawPointer",
        "Naming/SetContainer",
    )
    scope = "classMember"
    # 이 규칙은 카테고리를 여섯 개 낸다 — 하나씩 증명한다.
    badSampleFile = "Source/Probe/DynamicContainer.h"
    badSample = "#pragma once\n\nclass Probe\n{\nprivate:\n    vector<int32> _items;\n};\n"
    extraSamples = (
        ("Source/Probe/RawPointer.h",
         "#pragma once\n\nclass Probe\n{\nprivate:\n    int32* _value;\n};\n"),
        ("Source/Probe/FixedArray.h",
         "#pragma once\n\nclass Probe\n{\nprivate:\n    float32 _matrix[16];\n};\n"),
        ("Source/Probe/MapContainer.h",
         "#pragma once\n\nclass Probe\n{\nprivate:\n    map<int32, int32> _items;\n};\n"),
        ("Source/Probe/SetContainer.h",
         "#pragma once\n\nclass Probe\n{\nprivate:\n    set<int32> _items;\n};\n"),
        ("Source/Probe/MemberPlural.h",
         "#pragma once\n\nclass Probe\n{\nprivate:\n    vector<int32> _listItems;\n};\n"),
    )

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        violations: list[ConventionViolation] = []
        subject = kMapNamingSubject["member"]

        # 선언을 찾는 일만 여기서 한다 — 이름을 어떻게 볼지는 어휘 표가 정한다.
        if pointerMatch := _kMemberRawPointerRe.match(ctx.line):
            violations.extend(
                checkPointerNamingInternal(
                    subject, pointerMatch.group(2), len(pointerMatch.group(1)),
                    ctx.relPath, ctx.lineNum, ctx.trimmed,
                )
            )

        if arrayMatch := _kMemberFixedArrayRe.match(ctx.line):
            violations.extend(
                checkContainerNamingInternal(
                    subject, "arr", arrayMatch.group(1), "", ctx.relPath, ctx.lineNum, ctx.trimmed
                )
            )

        if vectorMatch := _kMemberVectorRe.match(ctx.line):
            violations.extend(
                checkContainerNamingInternal(
                    subject, "list", vectorMatch.group(2), vectorMatch.group(1),
                    ctx.relPath, ctx.lineNum, ctx.trimmed,
                )
            )

        if mapMatch := _kMemberMapRe.match(ctx.line):
            violations.extend(
                checkContainerNamingInternal(
                    subject, "map", mapMatch.group(1), "", ctx.relPath, ctx.lineNum, ctx.trimmed
                )
            )

        if setMatch := _kMemberSetRe.match(ctx.line):
            violations.extend(
                checkContainerNamingInternal(
                    subject, "unique", setMatch.group(1), "", ctx.relPath, ctx.lineNum, ctx.trimmed
                )
            )

        return violations
