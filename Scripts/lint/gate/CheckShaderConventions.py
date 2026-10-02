#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckShaderConventions.py

`AGENTS.md` 의 **HLSL** 명명 규칙을 검사합니다 — C++ 규칙을 셰이더에 옮긴 것이다.

**셰이더는 아무 게이트도 보지 않았다.** `CheckCodeConventions` 는 C++ 확장자만 훑는다. 그래서 셰이더는 제 나름의 모양
(PascalCase 함수 · `SW_` 로 시작하는 함수 · 한 글자 지역 변수 · `_t` 타입 · `pos`/`nrm`/`col` 필드)으로 쌓였다.
사용자 요청(2026-10-03): 셰이더 이름도 가능하면 C++ 와 같은 규칙.

검사 규칙 (`AGENTS.md` → "### HLSL"):

- 함수는 `camelCase`. 공유 헤더(`.hlsli`)의 함수는 네임스페이스 대신 `sw` 로 시작하고, 한 파일(`.hlsl`)의 함수는 접두어가 없다.
  약어는 한 단어(`RW` → `Rw`), 줄임말은 풀어 쓴다(`Tex` → `Texture`, `Cmp` → `Comparison`).
- 타입(`struct`)은 `PascalCase`, `_t` 꼬리 없음. 공유 헤더의 타입은 `Sw` 로 시작하고, 한 파일의 타입은 `Sw` 를 쓰지 않는다.
- 필드 · 지역 변수 · 매개변수는 `camelCase`. 한 글자 · `i` `j` `k` · 불투명한 줄임말 · HLSL 키워드 금지.
  `out` · `inout` 매개변수는 그 접두어로 시작한다. 고정 배열 지역 변수는 `arr` + 단수 명사, `groupshared` 는 `s_`(배열은 `s_arr`).
- 파일 수준 `static const` 는 `kPascalCase`, `#define` 은 `SW_` + 대문자, 공유 헤더의 include 가드는 `SW_<도메인>_<파일>_HLSLI`.
- 전역 리소스 · cbuffer 멤버는 `g_` + `PascalCase` — C++ 가 문자열로 묶는 이름이라 **고치지 않고**, 컨테이너 · 단수 · 줄임말 규칙에서도 뺀다.

**정규식으로 본다.** HLSL 파서를 두지 않는다 — 셰이더 문법은 작고, 이 게이트가 보는 것은 선언의 이름뿐이다. 먼저 주석 · 문자열을
지우고(예시 코드를 위반으로 읽지 않게), 중괄호 깊이와 선언은 전처리 지시문까지 지운 글로 센다 — 매크로 본문의 짝 없는 `{`
(`SW_ROOT_CONSTANTS_BEGIN`)가 파일 나머지를 "구조체 안" 으로 만들던 것이 초안의 오판이었다(함수 지역 변수가 필드로 읽혔다).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateResult, LintGate  # noqa: E402

#: 셰이더 확장자.
kShaderSuffix = (".hlsl", ".hlsli")

#: **문자열로 묶인 이름** — C++ · 파이프라인 XML 이 이 철자로 부르므로 셰이더 쪽에서만 바꿀 수 없다. 규칙 검사에서 뺀다.
#: 이유 없는 예외는 두지 않는다. 전역 · cbuffer 멤버(`g_*`)는 여기 적지 않는다 — 그 철자 규칙(`g_` + PascalCase)만 본다.
kStringBoundName = {
    "VSMain": "정점 진입점 — ShaderCompiler 기본값 · 파이프라인 XML(_vertexEntryPoint)이 문자열로 부른다",
    "PSMain": "픽셀 진입점 — ShaderCompiler 기본값 · 파이프라인 XML(_pixelEntryPoint)이 문자열로 부른다",
    "CSMain": "컴퓨트 진입점 — createComputePipelineState · ShaderBakeRequest 가 문자열로 부른다",
}

#: 불투명한 줄임말 — 이름을 camelCase 단어로 쪼갰을 때 이 단어가 하나라도 있으면 위반이다(`texColor` · `restNrm` · `instId`).
#: C++ 의 "읽히는 이름" 규칙(AGENTS.md: 루프 변수 `i` `j` `k` 금지, 줄임말은 풀어 쓴다)을 옮긴 것이다.
kOpaqueWord = {
    "pos", "wpos", "nrm", "norm", "col", "clr", "vid", "idx", "tmp", "tex", "dir", "ao", "lum", "inst", "trans", "dtid",
    "gid", "gtid", "enc", "diff", "ndot", "ndotl", "cmp", "coord", "dummy", "cnt", "num", "val", "buf", "src", "dst",
}

#: 이름으로 쓰면 안 되는 HLSL 키워드 · 예약어(컴파일러가 받아 주는 자리도 있지만 읽는 사람이 헷갈린다).
kHlslKeyword = {
    "sample", "point", "line", "lineadj", "triangle", "triangleadj", "linear", "centroid", "texture", "sampler", "vector",
    "matrix", "pass", "shared", "string", "export", "interface", "precise", "unorm", "snorm", "uniform", "technique",
    "compile", "register", "packoffset", "groupshared", "nointerpolation", "noperspective", "half", "dword",
}

#: 매크로로 선언하는 리소스 — (매크로, 이름 인자 위치들). 모르는 `SW_DECLARE_*` 는 위반으로 알린다(검사가 눈을 감지 않게).
kDeclareMacroNameArgument = {
    "SW_DECLARE_CBUFFER": (0,),
    "SW_DECLARE_STRUCTURED_BUFFER": (1,),
    "SW_DECLARE_RW_STRUCTURED_BUFFER": (1,),
    "SW_DECLARE_BYTE_ADDRESS_BUFFER": (0,),
    "SW_DECLARE_RW_BYTE_ADDRESS_BUFFER": (0,),
    "SW_DECLARE_TEXTURE2D_SAMPLER": (0, 1),
    "SW_DECLARE_TEXTURE2D": (0,),
    "SW_DECLARE_SAMPLER": (0,),
    "SW_DECLARE_RW_TEXTURE2D": (0,),
}

kCamelCaseRe = re.compile(r"^[a-z][a-zA-Z0-9]*$")
kPascalCaseRe = re.compile(r"^[A-Z][a-zA-Z0-9]*$")
kConstantRe = re.compile(r"^k[A-Z][a-zA-Z0-9]*$")
kMacroRe = re.compile(r"^SW_[A-Z0-9_]*$")
kGlobalRe = re.compile(r"^g_[A-Z][a-zA-Z0-9]*$")
kAcronymRunRe = re.compile(r"[A-Z]{2,}")
kWordRe = re.compile(r"[A-Z]?[a-z0-9]+|[A-Z]+(?![a-z])")
kSharedPrefixRe = re.compile(r"^sw[A-Z]")
kSharedTypePrefixRe = re.compile(r"^Sw[A-Z]")

#: 선언 앞에 올 수 있는 한정자.
kQualifierWord = ("const", "static", "precise", "nointerpolation", "noperspective", "centroid", "linear", "sample", "uniform",
                  "groupshared", "in", "out", "inout", "row_major", "column_major", "volatile", "extern")
#: HLSL 의 스칼라 · 벡터 · 행렬 타입. 대문자로 시작하는 이름(구조체 · 리소스 · `Texture2D` 등)은 따로 받는다.
kBuiltinTypeRe = r"(?:bool|int|uint|dword|half|float|double|min16float|min16int|min16uint)(?:[1-4](?:x[1-4])?)?"
kTypeRe = r"(?:" + kBuiltinTypeRe + r"|matrix|vector|[A-Z]\w*)(?:\s*<[^<>;{}]*>)?"

#: 선언 하나의 시작 — 문장 경계(`;` `{` `}` `(` `,` `]` 뒤 또는 줄 머리) + 한정자 + 타입 + 첫 이름.
kDeclarationRe = re.compile(
    r"(?:(?<=[;{}(,\]])|^)\s*"
    r"(?P<qualifier>(?:(?:" + "|".join(kQualifierWord) + r")\s+)*)"
    r"(?P<type>" + kTypeRe + r")\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*(?=[\[=;,:)])",
    re.MULTILINE)
#: 함수 정의 — 줄 머리나 속성(`[numthreads(…)]`) 뒤에서 시작한다.
kFunctionRe = re.compile(
    r"(?:^|(?<=\]))[ \t]*(?:(?:inline|static|precise|export)\s+)*"
    r"(?P<ret>[A-Za-z_]\w*(?:\s*<[^<>;{}]*>)?)\s+(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^()]*)\)\s*(?::\s*\w+\s*)?\{",
    re.MULTILINE)
kStructRe = re.compile(r"\bstruct\s+(?P<name>[A-Za-z_]\w*)")
kCbufferRe = re.compile(r"\bcbuffer\s+(?P<name>[A-Za-z_]\w*)")
kDefineRe = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(?P<name>[A-Za-z_]\w*)", re.MULTILINE)
kDirectiveRe = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.MULTILINE)
kDeclareMacroRe = re.compile(r"\b(?P<macro>SW_DECLARE_\w+)\s*\((?P<args>[^()]*(?:\([^()]*\)[^()]*)*)\)")
kGuardRe = re.compile(r"^[ \t]*#[ \t]*ifndef[ \t]+(?P<name>\w+)[ \t]*\n[ \t]*#[ \t]*define[ \t]+(?P=name)\b", re.MULTILINE)
kStructBodyRe = re.compile(r"\bstruct\s+\w+\s*\{|\bSW_MATERIAL_BEGIN\s*\{")
kCbufferBodyRe = re.compile(r"\bcbuffer\s+\w+[^{;]*\{|\bSW_DECLARE_CBUFFER\s*\([^()]*\)\s*\{")


def blankInternal(match: re.Match) -> str:
    """일치한 글을 같은 길이의 공백으로 바꾼다(줄바꿈은 남겨 줄 번호 · 오프셋을 지킨다)."""
    return re.sub(r"[^\n]", " ", match.group(0))


def stripCommentsAndStringsInternal(text: str) -> str:
    """주석 · 문자열을 같은 길이의 공백으로 바꾼다."""
    text = re.sub(r"/\*.*?\*/", blankInternal, text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", blankInternal, text)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', blankInternal, text)


def lineOfInternal(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def computeDepthMapInternal(code: str) -> list[int]:
    """오프셋마다 그 자리의 중괄호 깊이(0 = 파일 수준). 전처리 지시문을 지운 글로 센다."""
    listDepth = [0] * (len(code) + 1)
    depth = 0
    for index, character in enumerate(code):
        listDepth[index] = depth
        if character == "{":
            depth += 1
        elif character == "}":
            depth = max(0, depth - 1)
    listDepth[len(code)] = depth
    return listDepth


def computeBodySpanInternal(code: str, bodyRe: re.Pattern) -> list[tuple[int, int]]:
    """`bodyRe` 가 여는 `{ … }` 본문의 (시작, 끝) 오프셋."""
    listSpan: list[tuple[int, int]] = []
    for match in bodyRe.finditer(code):
        start = match.end()
        depth = 1
        index = start
        while index < len(code) and depth > 0:
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
            index += 1
        listSpan.append((start, index))
    return listSpan


def isInsideSpanInternal(offset: int, listSpan: list[tuple[int, int]]) -> bool:
    return any(start <= offset < end for start, end in listSpan)


def splitTopLevelInternal(text: str) -> list[tuple[int, str]]:
    """괄호 · 대괄호 · 중괄호 밖의 쉼표로 나눈다 — (조각 시작 오프셋, 조각)."""
    listPiece: list[tuple[int, str]] = []
    depth = 0
    start = 0
    for index, character in enumerate(text):
        if character in "([{":
            depth += 1
        elif character in ")]}":
            depth -= 1
        elif character == "," and depth == 0:
            listPiece.append((start, text[start:index]))
            start = index + 1
    listPiece.append((start, text[start:]))
    return listPiece


def findStatementEndInternal(code: str, offset: int) -> int:
    """`offset` 부터 괄호 밖의 첫 `;`(또는 for 초기화를 닫는 `)`)까지. 선언 하나의 끝이다."""
    depth = 0
    for index in range(offset, len(code)):
        character = code[index]
        if character in "([{":
            depth += 1
        elif character in ")]}":
            if depth == 0:
                return index
            depth -= 1
        elif character == ";" and depth == 0:
            return index
    return len(code)


def findNameProblemInternal(name: str) -> str | None:
    """필드 · 지역 변수 · 매개변수 · 함수 이름 하나에 공통으로 쓰는 규칙(없으면 None)."""
    if len(name) == 1:
        return "한 글자 이름"
    if name in kHlslKeyword:
        return "HLSL 키워드를 이름으로 썼음"
    if not kCamelCaseRe.match(name):
        return "camelCase 가 아님"
    if kAcronymRunRe.search(name):
        return "약어는 한 단어로 씁니다(RW → Rw, ID → Id)"
    listOpaque = [word for word in kWordRe.findall(name) if word.lower() in kOpaqueWord]
    if listOpaque:
        return f"불투명한 줄임말 '{listOpaque[0]}' — 풀어 씁니다"
    return None


def findArrayProblemInternal(name: str) -> str | None:
    """고정 배열 지역 변수의 접두어 · 단수 규칙(C++ 의 `arr` 컨테이너 접두어와 같다)."""
    if not re.match(r"^arr[A-Z]", name):
        return "고정 배열은 arr 접두어"
    if re.search(r"[^su]s$", name):
        return "arr 뒤는 단수 명사"
    return None


def findDeclaratorInternal(code: str, match: re.Match) -> list[tuple[int, str]]:
    """선언 하나의 이름들 — `float sine, cosine;` 처럼 쉼표로 이어진 것까지. (이름 오프셋, 이름부터 그 선언자 끝까지)."""
    nameEnd = match.end("name")
    statementEnd = findStatementEndInternal(code, nameEnd)
    listDeclarator = [(match.start("name"), match.group("name") + code[nameEnd:statementEnd])]
    for pieceOffset, piece in splitTopLevelInternal(code[nameEnd:statementEnd])[1:]:
        nameMatch = re.match(r"\s*([A-Za-z_]\w*)", piece)
        if nameMatch:
            listDeclarator.append((nameEnd + pieceOffset + nameMatch.start(1), piece[nameMatch.start(1):]))
    return listDeclarator


def checkShaderTextInternal(text: str, relPath: str) -> list[str]:
    """셰이더 하나의 글에 규칙을 적용한다."""
    listViolation: list[str] = []
    bSharedHeader = relPath.endswith(".hlsli")
    code = stripCommentsAndStringsInternal(text)
    codeBody = kDirectiveRe.sub(blankInternal, code)   # 지시문까지 지운 글 — 깊이 · 함수 · 선언은 이것으로 본다
    listDepth = computeDepthMapInternal(codeBody)
    listStructSpan = computeBodySpanInternal(codeBody, kStructBodyRe)
    listCbufferSpan = computeBodySpanInternal(codeBody, kCbufferBodyRe)
    listParamSpan: list[tuple[int, int]] = []

    def report(offset: int, message: str) -> None:
        listViolation.append(f"{relPath}:{lineOfInternal(code, offset)} {message} — AGENTS.md '### HLSL'")

    # 함수와 매개변수
    for match in kFunctionRe.finditer(codeBody):
        offset = match.start("name")
        if listDepth[offset] != 0 or match.group("ret") in ("struct", "cbuffer", "return", "else"):
            continue
        listParamSpan.append((match.start("params"), match.end("params")))
        name = match.group("name")
        if name not in kStringBoundName:
            problem = findNameProblemInternal(name)
            if problem is None and bSharedHeader and not kSharedPrefixRe.match(name):
                problem = "공유 헤더의 함수는 sw 로 시작합니다(네임스페이스 대신)"
            elif problem is None and not bSharedHeader and kSharedPrefixRe.match(name):
                problem = "한 파일의 함수에는 sw 접두어를 쓰지 않습니다(공유 헤더 함수와 구별)"
            if problem:
                report(offset, f"함수 '{name}': {problem}")

        for pieceOffset, rawParam in splitTopLevelInternal(match.group("params")):
            param = re.sub(r":\s*\w+\s*$", "", rawParam.strip())   # 시맨틱
            param = re.sub(r"=.*$", "", param, flags=re.DOTALL)      # 기본값
            param = re.sub(r"\[[^\]]*\]\s*$", "", param).strip()     # 배열 크기
            listToken = param.split()
            if len(listToken) < 2:
                continue
            paramName = listToken[-1]
            setQualifier = set(listToken[:-1])
            problem = findNameProblemInternal(paramName)
            if problem is None and "inout" in setQualifier and not re.match(r"^inout[A-Z]", paramName):
                problem = "inout 매개변수는 inout 접두어"
            elif problem is None and "out" in setQualifier and not re.match(r"^out[A-Z]", paramName):
                problem = "out 매개변수는 out 접두어"
            if problem:
                report(match.start("params") + pieceOffset, f"함수 '{name}' 의 매개변수 '{paramName}': {problem}")

    # 타입 — 매크로 본문(`#define SW_MATERIAL_BEGIN struct …`)의 구조체도 본다. 그것은 공유 헤더가 내놓는 타입이다.
    for match in kStructRe.finditer(code):
        name = match.group("name")
        bInDirective = codeBody[match.start()] == " "
        if not kPascalCaseRe.match(name):
            report(match.start("name"), f"타입 '{name}' 는 PascalCase 여야 합니다(_t 꼬리 없음)")
        elif bSharedHeader and not kSharedTypePrefixRe.match(name):
            report(match.start("name"), f"공유 헤더의 타입 '{name}' 는 Sw 로 시작합니다(네임스페이스 대신)")
        elif not bSharedHeader and not bInDirective and kSharedTypePrefixRe.match(name):
            report(match.start("name"), f"한 파일의 타입 '{name}' 에는 Sw 접두어를 쓰지 않습니다(공유 헤더 타입과 구별)")
    for match in kCbufferRe.finditer(codeBody):
        name = match.group("name")
        if not kPascalCaseRe.match(name):
            report(match.start("name"), f"cbuffer '{name}' 는 PascalCase 여야 합니다")

    # 매크로로 선언한 리소스 · cbuffer
    for match in kDeclareMacroRe.finditer(codeBody):
        macro = match.group("macro")
        if macro not in kDeclareMacroNameArgument:
            report(match.start("macro"), f"모르는 선언 매크로 '{macro}' — 이름 인자 위치를 이 게이트의 표(kDeclareMacroNameArgument)에 더하십시오")
            continue
        listArgument = [piece.strip() for _, piece in splitTopLevelInternal(match.group("args"))]
        for argumentIndex in kDeclareMacroNameArgument[macro]:
            if argumentIndex >= len(listArgument):
                continue
            name = listArgument[argumentIndex]
            if macro == "SW_DECLARE_CBUFFER":
                if not kPascalCaseRe.match(name):
                    report(match.start("args"), f"cbuffer '{name}' 는 PascalCase 여야 합니다")
            elif not kGlobalRe.match(name):
                report(match.start("args"), f"전역 리소스 '{name}' 는 g_ + PascalCase 여야 합니다")

    # 선언 — 자리로 가른다: 파일 수준(전역 · 상수 · groupshared), cbuffer 멤버, 구조체 필드, 함수 지역
    for match in kDeclarationRe.finditer(codeBody):
        if isInsideSpanInternal(match.start("name"), listParamSpan):
            continue   # 매개변수는 위에서 봤다
        listQualifier = match.group("qualifier").split()
        for declaratorOffset, declarator in findDeclaratorInternal(codeBody, match):
            name = re.match(r"[A-Za-z_]\w*", declarator).group(0)
            bArray = re.match(r"[A-Za-z_]\w*\s*\[", declarator) is not None
            if name in kStringBoundName:
                continue
            if listDepth[declaratorOffset] == 0:
                if "groupshared" in listQualifier:
                    bare = name[2:] if name.startswith("s_") else ""
                    if not name.startswith("s_") or not kCamelCaseRe.match(bare):
                        report(declaratorOffset, f"groupshared '{name}' 는 s_ + camelCase 입니다")
                    elif bArray and not re.match(r"^arr[A-Z]", bare):
                        report(declaratorOffset, f"groupshared 배열 '{name}' 는 s_arr 접두어입니다")
                elif "static" in listQualifier and "const" in listQualifier:
                    if not kConstantRe.match(name):
                        report(declaratorOffset, f"파일 수준 상수 '{name}' 는 kPascalCase 여야 합니다")
                elif "static" in listQualifier:
                    if not re.match(r"^s_[a-z][a-zA-Z0-9]*$", name):
                        report(declaratorOffset, f"파일 수준 static '{name}' 는 s_ + camelCase 입니다")
                elif not kGlobalRe.match(name):
                    report(declaratorOffset, f"전역 · 루트 상수 '{name}' 는 g_ + PascalCase 여야 합니다")
                continue
            if isInsideSpanInternal(declaratorOffset, listCbufferSpan):
                if not kGlobalRe.match(name):
                    report(declaratorOffset, f"cbuffer 멤버 '{name}' 는 g_ + PascalCase 여야 합니다")
                continue
            if isInsideSpanInternal(declaratorOffset, listStructSpan):
                problem = findNameProblemInternal(name)   # 필드는 C++ 멤버를 비춘다 — 배열 접두어는 강제하지 않는다
                if problem:
                    report(declaratorOffset, f"필드 '{name}': {problem}")
                continue
            bLocalConstant = "const" in listQualifier and kConstantRe.match(name) is not None   # 지역 상수는 kPascalCase 도 받는다(C++ 와 같다)
            problem = None if bLocalConstant else findNameProblemInternal(name)
            if problem is None and bArray:
                problem = findArrayProblemInternal(name)
            if problem:
                report(declaratorOffset, f"지역 변수 '{name}': {problem}")

    # 매크로 · include 가드
    for match in kDefineRe.finditer(code):
        name = match.group("name")
        if not kMacroRe.match(name):
            report(match.start("name"), f"매크로 '{name}' 는 SW_ + 대문자여야 합니다")
    if bSharedHeader:
        listPart = list(Path(relPath).parts)
        listDomain = listPart[listPart.index("Resource") + 1:-1] if "Resource" in listPart else listPart[:-1]
        if listDomain and listDomain[-1] == "shaders":
            listDomain = listDomain[:-1]
        expected = "_".join(["SW"] + [part.upper() for part in listDomain] + [Path(relPath).stem.upper(), "HLSLI"])
        guardMatch = kGuardRe.search(code)
        if guardMatch is None:
            report(0, f"include 가드가 없습니다 — '#ifndef {expected}' / '#define {expected}'")
        elif guardMatch.group("name") != expected:
            report(guardMatch.start("name"), f"include 가드 '{guardMatch.group('name')}' 는 '{expected}' 여야 합니다(SW_<도메인>_<파일>_HLSLI)")

    return listViolation


def wrapHeaderInternal(body: str) -> str:
    """자가 시험용 `.hlsli` 조각 — 맞는 include 가드로 감싸 그 조각이 노리는 규칙 하나만 걸리게 한다."""
    return f"#ifndef SW_ENGINE_PROBE_HLSLI\n#define SW_ENGINE_PROBE_HLSLI\n{body}#endif\n"


kProbeSource = "Resource/engine/shaders/probe.hlsl"
kProbeHeader = "Resource/engine/shaders/probe.hlsli"


class CheckShaderConventionsGate(LintGate):
    """`AGENTS.md` 의 HLSL 규칙 — C++ 규칙을 셰이더에 옮긴 것이다."""

    description = "Resource/ 셰이더(HLSL) 명명 규칙 검사 (AGENTS.md '### HLSL')"
    buildComment = "Checking HLSL naming conventions (AGENTS.md)..."
    timeoutSeconds = 30
    preCommitPattern = ("*.hlsl", "*.hlsli")
    preCommitFileArgument = "--files"
    violationHeader = "HLSL 명명 규칙 위반"
    hint = ("  AGENTS.md '### HLSL': 함수 camelCase(공유 헤더는 sw…, 한 파일은 접두어 없음, 진입점 VSMain/PSMain/CSMain 예외), 타입 PascalCase\n"
            "  (공유 헤더는 Sw…), 필드 · 지역 · 매개변수 camelCase(한 글자 · 줄임말 · 키워드 금지, out/inout 접두어, 배열 arr), static const\n"
            "  kPascalCase, #define SW_, groupshared s_, 전역 · cbuffer 멤버 g_Pascal(문자열로 묶인 이름이라 바꾸지 않는다).")
    # 조각마다 그 규칙 하나만 걸리게 짓는다 — 다른 위반이 섞이면 노리는 규칙이 죽어도 이 시험은 통과한다.
    selfTestCases = [
        {"name": "PascalCase 함수", "files": {kProbeSource: "float4 SampleThing( float2 uv ) { return 0; }\n"}},
        {"name": "공유 헤더 함수에 sw 없음", "files": {kProbeHeader: wrapHeaderInternal("float loadValue( uint slotIndex ) { return 0; }\n")}},
        {"name": "한 파일 함수에 sw", "files": {kProbeSource: "float swLoadValue( uint slotIndex ) { return 0; }\n"}},
        {"name": "약어 연속 대문자", "files": {kProbeHeader: wrapHeaderInternal("float swLoadRWTexture( uint slotIndex ) { return 0; }\n")}},
        {"name": "함수 이름의 줄임말", "files": {kProbeHeader: wrapHeaderInternal("float swSampleShadowCmp( float depth ) { return depth; }\n")}},
        {"name": "속성 뒤 진입점의 매개변수", "files": {kProbeSource: "[numthreads( 1, 1, 1 )] void CSMain( uint3 dtid : SV_DispatchThreadID ) { }\n"}},
        {"name": "_t 타입", "files": {kProbeSource: "struct Material_t { float4 color; };\n"}},
        {"name": "매크로 본문의 _t 타입", "files": {kProbeHeader: wrapHeaderInternal("#define SW_PROBE_BEGIN struct SwProbe_t\n")}},
        {"name": "공유 헤더 타입에 Sw 없음", "files": {kProbeHeader: wrapHeaderInternal("struct MaterialData { float4 color; };\n")}},
        {"name": "한 파일 타입에 Sw", "files": {kProbeSource: "struct SwLocalData { float4 color; };\n"}},
        {"name": "줄임말 필드", "files": {kProbeSource: "struct PSInput\n{\n    float4 pos : SV_Position;\n};\n"}},
        {"name": "한 글자 지역 변수", "files": {kProbeSource: "float4 shadeIt( float4 color ) {\n    float3 n = color.xyz;\n    return color;\n}\n"}},
        {"name": "여럿 선언의 두 번째 이름", "files": {kProbeSource: "float spinIt( float angle ) {\n    float sine, c;\n    sincos( angle, sine, c );\n    return sine;\n}\n"}},
        {"name": "지역 변수 이름의 줄임말", "files": {kProbeSource: "float4 shadeIt( float4 color ) {\n    float4 texColor = color;\n    return texColor;\n}\n"}},
        {"name": "키워드 이름", "files": {kProbeSource: "float4 shadeIt( float4 color ) {\n    float4 texture = color;\n    return texture;\n}\n"}},
        {"name": "루프 변수 i", "files": {kProbeSource: "float sumIt( float value ) {\n    for ( uint i = 0; i < 4; ++i ) value += 1;\n    return value;\n}\n"}},
        {"name": "out 접두어 없는 out 매개변수", "files": {kProbeSource: "void splitIt( float4 value, out float3 normal ) { normal = value.xyz; }\n"}},
        {"name": "arr 없는 고정 배열", "files": {kProbeSource: "float sumIt( float value ) {\n    float2 offset[4] = { 0, 0, 0, 0 };\n    return value;\n}\n"}},
        {"name": "복수형 고정 배열", "files": {kProbeSource: "float sumIt( float value ) {\n    float2 arrOffsets[4] = { 0, 0, 0, 0 };\n    return value;\n}\n"}},
        {"name": "groupshared 배열 접두어", "files": {kProbeSource: "groupshared uint s_sortKey[64];\n"}},
        {"name": "대문자 static const", "files": {kProbeHeader: wrapHeaderInternal("static const uint SW_INVALID = 0xffffffff;\n")}},
        {"name": "SW_ 아닌 매크로", "files": {kProbeSource: "#define GET_BUFFER( x ) x\n"}},
        {"name": "g_ 아닌 전역", "files": {kProbeSource: "StructuredBuffer<uint> instanceIds : register( t0 );\n"}},
        {"name": "g_ 아닌 매크로 선언 리소스", "files": {kProbeSource: "SW_DECLARE_STRUCTURED_BUFFER( uint, instanceIds, 0 );\n"}},
        {"name": "g_ 아닌 cbuffer 멤버", "files": {kProbeSource: "SW_DECLARE_CBUFFER( ProbeParams, 0 )\n{\n    float4 tintColor;\n};\n"}},
        {"name": "모르는 선언 매크로", "files": {kProbeSource: "SW_DECLARE_APPEND_BUFFER( uint, g_ProbeList, 0 );\n"}},
        {"name": "include 가드 이름", "files": {kProbeHeader: "#ifndef SW_PROBE_HLSLI\n#define SW_PROBE_HLSLI\n#endif\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = self.selectTargetFiles(repositoryRoot, args.files, suffixes=kShaderSuffix)
        listViolation: list[str] = []
        for path in listPath:
            relPath = path.relative_to(repositoryRoot).as_posix()
            try:
                text = path.read_text(encoding="utf-8")
            except OSError as exception:
                listViolation.append(f"{relPath}:1 읽을 수 없습니다: {exception}")
                continue
            listViolation.extend(checkShaderTextInternal(text, relPath))
        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} shader files scanned")


main = CheckShaderConventionsGate.run


if __name__ == "__main__":
    sys.exit(main())
