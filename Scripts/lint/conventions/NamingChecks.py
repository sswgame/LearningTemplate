"""
CheckCodeConventions — 명명 판정 — 어휘 표를 읽는 단 한 벌의 검사(멤버 컨테이너 · 포인터 · 매개변수 · 지역 변수 · 출력 매개변수).

게이트 `Scripts/lint/gate/CheckCodeConventions.py` 의 한 묶음이다(묶음 지도는 `__init__.py`).
"""
from __future__ import annotations

import re

from .Model import ConventionViolation
from .NamingVocabulary import (
    _kAnyMapRe,
    _kAnySetRe,
    _kAnyVectorRe,
    _kLocalFixedArrayRe,
    _kNonDeclarationKeyword,
    ContainerVocabulary,
    isByteBufferNameInternal,
    kMapContainerVocabulary,
    kMapNamingSubject,
    listAllowedPrefixInternal,
    NamingSubject,
)
from .Patterns import (
    _kCallLikeNameRe,
    _kFunctionSignatureTailRe,
    _kLocalUnderscoreDeclRe,
    _kParameterTypeRe,
    _kStringLiteralStripRe,
    _kTemplateArgumentRe,
    _kTrailingArraySuffixRe,
    _kTrailingIdentifierRe,
    isPluralWordInternal,
    makeSingularInternal,
)


# --- 5. 명명 판정 — 어휘 표를 읽는 단 한 벌의 검사 ---------------------------
#
# 위 `kMapContainerVocabulary` · `kMapNamingSubject` 를 읽어 실제 판정을 내린다.
# 주체 셋(`checkParameterItemInternal` · `checkLocalVariableItemInternal` ·
# `ClassMemberNamingRule`)이 **선언을 찾아낸 뒤** 이름을 여기로 넘긴다.


def buildContainerFixInternal(subject: NamingSubject, vocabulary: ContainerVocabulary, varName: str) -> str:
    """
    접두어가 없는 이름에 붙여 줄 수정안. 방향 접두어(`out`/`inout`)는 보존한다.

    `outActor` → `outListActor`, `count` → `listCount`, 멤버 `_actor` → `_listActor`.
    """
    bare = varName.lstrip("_")
    prefix = vocabulary.prefix
    capitalized = prefix[0].upper() + prefix[1:]

    if subject.bMember:
        candidate = f"_{prefix}{bare[0].upper()}{bare[1:]}" if bare else f"_{prefix}"
    elif bare in ("out", "_out"):
        candidate = f"out{capitalized}"
    else:
        for direction in subject.listDirection:
            if bare.startswith(direction) and len(bare) > len(direction):
                candidate = f"{direction}{capitalized}{bare[len(direction):]}"
                break
        else:
            candidate = f"{prefix}{bare[0].upper()}{bare[1:]}" if bare else prefix

    if vocabulary.bSingular and isPluralWordInternal(candidate):
        candidate = makeSingularInternal(candidate)

    return candidate


def buildListSuffixFixInternal(subject: NamingSubject, varName: str) -> str:
    """`actorList` → `listActor`, `outActorList` → `outListActor`."""
    bare = varName.lstrip("_")[:-4]
    mark = "_" if subject.bMember else ""

    if bare.startswith("list"):
        # 이미 접두어가 있다 — 접미어만 떼면 된다 (`_listLiveCmdList` → `_listLiveCmd`).
        candidate = f"{mark}{bare}"
    else:
        for direction in subject.listDirection:
            if bare.startswith(direction) and len(bare) > len(direction):
                candidate = f"{direction}List{bare[len(direction):]}"
                break
        else:
            candidate = f"{mark}list{bare}"

    if isPluralWordInternal(candidate):
        candidate = makeSingularInternal(candidate)

    return candidate


def checkContainerNamingInternal(
    subject: NamingSubject,
    vocabularyKey: str,
    varName: str,
    innerType: str,
    relPath: str,
    lineNum: int,
    snippet: str,
) -> list[ConventionViolation]:
    """
    컨테이너 이름 하나를 판정합니다 — **세 주체가 모두 이 함수를 부른다.**

    접두어 · 바이트 버퍼 예외 · `List` 접미어 · 단수형까지 여기서 전부 본다. 주체가 하는 일은
    선언을 찾아 `varName` 과 원소 타입을 넘기는 것뿐이다.
    """
    violations: list[ConventionViolation] = []
    vocabulary = kMapContainerVocabulary[vocabularyKey]
    listAllowed = listAllowedPrefixInternal(subject, vocabulary.prefix)
    category = subject.categoryFor(vocabularyKey)

    # 1) 바이트 버퍼는 접두어를 **생략**한다 — 규칙이 뒤집히는 유일한 자리다.
    if vocabulary.bByteException and isByteBufferNameInternal(innerType, varName):
        if varName.lstrip("_").startswith(vocabulary.prefix):
            bare = varName.lstrip("_")[len(vocabulary.prefix):]
            mark = "_" if subject.bMember else ""
            fix = f"{mark}{bare[0].lower()}{bare[1:]}" if bare else f"{mark}bytes"
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category=category,
                    message=f"바이트 벡터 {subject.noun} '{varName}'는 'byte/buffer' 단어가 포함된 경우 "
                            f"'{vocabulary.prefix}' 접두어를 생략해야 합니다 ('{fix}' 권장).",
                    snippet=snippet,
                    suggested_fix=fix,
                )
            )
        return violations

    # 2) 접두어가 있어야 한다.
    if not varName.startswith(listAllowed):
        fix = buildContainerFixInternal(subject, vocabulary, varName)
        violations.append(
            ConventionViolation(
                file_path=relPath,
                line_number=lineNum,
                rule_category=category,
                message=f"{vocabulary.noun} {subject.noun} '{varName}'는 "
                        f"'{listAllowed[0]}' 접두어로 시작해야 합니다 ('{fix}' 권장).",
                snippet=snippet,
                suggested_fix=fix,
            )
        )

    # 3) `List` 접미어 금지 (`actorList` → `listActor`).
    if vocabulary.bBanListSuffix and varName.endswith("List") and varName.lstrip("_") not in listAllowed:
        fix = buildListSuffixFixInternal(subject, varName)
        violations.append(
            ConventionViolation(
                file_path=relPath,
                line_number=lineNum,
                rule_category=category,
                message=f"{subject.noun} '{varName}'는 'List' 접미어 대신 '{fix}' 형태를 사용해야 합니다.",
                snippet=snippet,
                suggested_fix=fix,
            )
        )

    # 4) 단수형. `unique` 만 복수형을 허용한다 (`AGENTS.md`).
    if vocabulary.bSingular and varName.startswith(listAllowed) and isPluralWordInternal(varName):
        singularFix = makeSingularInternal(varName)
        violations.append(
            ConventionViolation(
                file_path=relPath,
                line_number=lineNum,
                rule_category="Naming/ContainerSingular",
                message=f"{vocabulary.noun} {subject.noun} '{varName}'는 복수형 대신 단수형 명사를 "
                        f"사용해야 합니다 ('{singularFix}' 권장).",
                snippet=snippet,
                suggested_fix=singularFix,
            )
        )

    return violations


def checkPointerNamingInternal(
    subject: NamingSubject,
    varName: str,
    numPointer: int,
    relPath: str,
    lineNum: int,
    snippet: str,
) -> list[ConventionViolation]:
    """
    원시 포인터 이름 하나를 판정합니다 — **세 주체가 모두 이 함수를 부른다.**

    `p` / `pp` 접두어와 그 정적(`s_p` · `_s_p`) · 출력(`pOut` · `ppOut`) 변형을 함께 본다.
    """
    if numPointer < 1 or numPointer > 2:
        return []

    prefix = "p" * numPointer
    noun = "원시 포인터" if numPointer == 1 else "이중 포인터"

    if subject.bMember:
        listAllowed = (f"_{prefix}", f"_s_{prefix}")
    else:
        listAllowed = (prefix, f"s_{prefix}", f"_s_{prefix}")

    for allowed in listAllowed:
        if varName.startswith(allowed):
            rest = varName[len(allowed):]
            # `pOut` · `pInOut` 처럼 뒤가 대문자로 이어지면 올바른 이름이다.
            if rest and rest[0].isupper():
                return []
            # 이름이 접두어 그 자체인 경우(`T* p`)는 둔다 — `Core/Container/vector.h` 처럼
            # STL 시그니처를 그대로 흉내 내는 자리가 있다.
            if not rest and not subject.bMember:
                return []

    stripped = varName.lstrip("_")
    fix = f"{listAllowed[0]}{stripped[0].upper()}{stripped[1:]}" if stripped else f"{listAllowed[0]}Ptr"
    return [
        ConventionViolation(
            file_path=relPath,
            line_number=lineNum,
            rule_category=subject.pointerCategory,
            message=f"{noun} {subject.noun} '{varName}'는 '{listAllowed[0]}' "
                    f"(정적 변수는 '{listAllowed[-1]}') 접두어로 시작해야 합니다 ('{fix}' 권장).",
            snippet=snippet,
            suggested_fix=fix,
        )
    ]


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
    arrayMatch = _kTrailingArraySuffixRe.search(paramDecl)
    isArray = bool(arrayMatch)
    if isArray and arrayMatch:
        paramDecl = paramDecl[:arrayMatch.start()].strip()

    # 식별자(변수명) 분리
    match = _kTrailingIdentifierRe.search(paramDecl)
    if not match:
        return violations

    paramName = match.group(1)
    typePart = paramDecl[:match.start()].strip()

    # 타입만 명시되고 변수명이 생략된 선언 또는 네임스페이스 한정 식 (e.g. Scope::_s_var)
    if not typePart or typePart.endswith("::"):
        return violations

    # typePart가 유효한 C++ 타입 패턴인지 검증 (표현식 제외)
    if not _kParameterTypeRe.match(typePart):
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

    subject = kMapNamingSubject["parameter"]

    # 2. 원시 포인터 (템플릿 인자 <...> 내부의 *는 제외)
    typeWithoutTemplate = _kTemplateArgumentRe.sub('', typePart)
    numPointer = typeWithoutTemplate.count("*")
    if numPointer >= 1:
        if paramName in ("argv", "argc", "env", "this"):
            return violations

        violations.extend(
            checkPointerNamingInternal(subject, paramName, numPointer, relPath, lineNum, snippet)
        )

    # 3. 컨테이너 — 판정은 `checkContainerNamingInternal` 한 곳이 든다.
    elif vectorMatch := _kAnyVectorRe.search(typePart):
        violations.extend(
            checkContainerNamingInternal(
                subject, "list", paramName, vectorMatch.group(1), relPath, lineNum, snippet
            )
        )
    elif _kAnyMapRe.search(typePart):
        violations.extend(
            checkContainerNamingInternal(subject, "map", paramName, "", relPath, lineNum, snippet)
        )
    elif _kAnySetRe.search(typePart):
        violations.extend(
            checkContainerNamingInternal(subject, "unique", paramName, "", relPath, lineNum, snippet)
        )
    elif isArray:
        violations.extend(
            checkContainerNamingInternal(subject, "arr", paramName, "", relPath, lineNum, snippet)
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

    codeClean = _kStringLiteralStripRe.sub('', line)

    # 함수 선언/정의 시그니처인 경우 변수 선언이 아님
    if _kFunctionSignatureTailRe.search(trimmed):
        return violations

    # 1. 지역 변수 '_' 접두어 검사 (예: int32 _val = 0;, auto _pPtr = ...)
    localUnderscoreMatch = _kLocalUnderscoreDeclRe.search(codeClean)
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

    subject = kMapNamingSubject["local"]

    # 2. 원시 포인터 — 함수 정의/선언(뒤에 괄호가 오는 이름)은 변수가 아니므로 제외한다.
    if not _kCallLikeNameRe.search(codeClean):
        ptrDeclMatch = re.search(
            r'^\s*(?:const\s+|static\s+|constexpr\s+)?([A-Za-z0-9_:]+)\s*(\*{1,2})\s*(?:const\s+)?([a-zA-Z0-9_]+)\s*(?:=|;|,|{)',
            codeClean,
        )
        if ptrDeclMatch:
            typeName = ptrDeclMatch.group(1)
            numPointer = len(ptrDeclMatch.group(2))
            varName = ptrDeclMatch.group(3)
            if typeName not in _kNonDeclarationKeyword and varName not in ("argv", "this"):
                violations.extend(
                    checkPointerNamingInternal(subject, varName, numPointer, relPath, lineNum, trimmed)
                )

    # 3. 컨테이너 — 판정은 `checkContainerNamingInternal` 한 곳이 든다.
    if vectorMatch := re.search(
        r'^\s*(?:(?:sw::)?(?:vector|list|deque))\s*<([^>]+)>\s+([a-zA-Z0-9_]+)\s*(?:=|;|{|\()',
        codeClean,
    ):
        violations.extend(
            checkContainerNamingInternal(
                subject, "list", vectorMatch.group(2), vectorMatch.group(1), relPath, lineNum, trimmed
            )
        )

    if mapMatch := re.search(
        r'^\s*(?:(?:sw::)?(?:unordered_map|map))\s*<[^>]+>\s+([a-zA-Z0-9_]+)\s*(?:=|;|{|\()',
        codeClean,
    ):
        violations.extend(
            checkContainerNamingInternal(subject, "map", mapMatch.group(1), "", relPath, lineNum, trimmed)
        )

    if setMatch := re.search(
        r'^\s*(?:(?:sw::)?(?:unordered_set|set))\s*<[^>]+>\s+([a-zA-Z0-9_]+)\s*(?:=|;|{|\()',
        codeClean,
    ):
        violations.extend(
            checkContainerNamingInternal(subject, "unique", setMatch.group(1), "", relPath, lineNum, trimmed)
        )

    if arrMatch := _kLocalFixedArrayRe.search(codeClean):
        violations.extend(
            checkContainerNamingInternal(subject, "arr", arrMatch.group(1), "", relPath, lineNum, trimmed)
        )

    return violations
