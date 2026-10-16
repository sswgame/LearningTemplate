"""
CheckCodeConventions — `.cpp` 의 번역 단위 전용 도우미(`XxxInternal` 구조체 · 익명 네임스페이스 자유 함수)를 읽는 도구.

게이트 `Scripts/lint/gate/CheckCodeConventions.py` 의 한 묶음이다(묶음 지도는 `__init__.py`). 규칙 셋이 이것을 나눠 쓴다.

  - `Naming/MultipleInternalHelper`      — `.cpp` 에 `XxxInternal` 구조체가 둘 이상(파일 규칙, `LineRules.py`)
  - `Naming/LoneFunctionInternalHelper`  — 함수 하나뿐인 `XxxInternal` 구조체(파일 규칙, `LineRules.py`)
  - `Naming/DuplicateAnonymousFunction`  — 여러 .cpp 가 같은 이름의 익명 네임스페이스 자유 함수를 둔다(전체 스캔, `CrossFile.py`)

C++ 를 파서 없이 읽으므로 **주석 · 문자열 · 전처리 줄을 먼저 지우고**(줄 번호는 남긴다) 중괄호 깊이로 멤버를 문장 단위로 가른다. 모르는 모양은
"함수가 하나"로 세지 않는 쪽으로 기운다 — 틀려서 구조체를 고치라고 시키는 것보다 놓치는 편이 낫다(위반은 커밋을 막는다).
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from functools import lru_cache

from common.RuleData import kExemptionKey, kKindReason, kKindTextList, readRuleFile

#: 도우미 구조체 정의 — 이름이 `Internal` 로 끝나고 `struct` 와 같은 줄에 `{` 가 없다(Allman). 상속 · `final` 이 붙은 것은 형식이라 세지 않는다.
kInternalStructRe = re.compile(r"^[ \t]*struct[ \t]+(\w+Internal)[ \t]*$", re.MULTILINE)

#: 주석 · 문자열 · 문자 · 전처리 줄 — 한 번에 지운다(`R"(...)"` 원시 문자열 포함). 숫자 구분자 `1'000` 은 문자 리터럴이 아니다.
_kBlankRe = re.compile(
    r"//[^\n]*"
    r"|/\*.*?\*/"
    r'|R"(?P<delim>[^(\s]*)\(.*?\)(?P=delim)"'
    r'|"(?:\\.|[^"\\\n])*"'
    r"|(?<![\w])'(?:\\.|[^'\\\n])*'"
    r"|^[ \t]*\#(?:[^\n\\]|\\\n)*",
    re.DOTALL | re.MULTILINE)
_kConditionalDirectiveRe = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b", re.MULTILINE)
_kAccessSpecifierRe = re.compile(r"\b(?:public|private|protected)\s*:(?!:)")
_kOperatorRe = re.compile(r"\boperator\s*(?:\(\s*\)|[^\s(\w]+|\w+)")
_kTypeHeadRe = re.compile(r"(?:alignas\s*\([^)]*\)\s*)?\b(?:struct|class|union|enum)\b")
_kNameBeforeParenRe = re.compile(r"([A-Za-z_~][\w:]*)\s*$")
_kStaticRe = re.compile(r"\bstatic\b")
_kAnonymousNamespaceRe = re.compile(r"\bnamespace\s*\{")


@dataclass
class Statement:
    """구조체 · 네임스페이스 본문의 최상위 문장 하나."""
    kind: str       # function · type · data · decl · namespace · other
    head: str       # 첫 `{` 또는 `;` 앞의 글(접근 지정자 · 앞붙은 `template<...>` 를 뗀 것)
    lineNum: int    # 문장이 시작하는 줄(1 부터, 파일 기준)
    bTemplate: bool = False
    bStatic: bool = False
    bOperator: bool = False
    name: str = ""  # 함수 이름(함수일 때만)


def blankNonCodeInternal(content: str) -> str:
    """주석 · 문자열 · 전처리 줄을 공백으로 바꿉니다. 줄바꿈과 글자 수는 그대로라 위치가 원문과 맞는다."""
    return _kBlankRe.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), content)


def findClosingBraceInternal(text: str, openIndex: int) -> int:
    """`text[openIndex]` 의 `{` 와 짝인 `}` 의 위치. 짝이 없으면 -1."""
    depth = 0
    for index in range(openIndex, len(text)):
        ch = text[index]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def stripTemplatePrefixInternal(head: str) -> tuple[str, bool]:
    """머리의 앞 `template< ... >` 를 떼고 있었는지를 함께 돌려줍니다(`<` `>` 짝을 센다)."""
    stripped = head.lstrip()
    if not stripped.startswith("template"):
        return head, False
    open_ = stripped.find("<")
    if open_ < 0:
        return head, True
    depth = 0
    for index in range(open_, len(stripped)):
        if stripped[index] == "<":
            depth += 1
        elif stripped[index] == ">":
            depth -= 1
            if depth == 0:
                return stripped[index + 1:].strip(), True
    return head, True


def classifyHeadInternal(rawHead: str, bBlock: bool, lineNum: int) -> Statement:
    """문장 머리를 읽어 종류를 정합니다. `bBlock` 은 `{` 로 끝났다는 뜻(`;` 로 끝나면 False)."""
    head = _kAccessSpecifierRe.sub(" ", rawHead).strip()
    head, bTemplate = stripTemplatePrefixInternal(head)
    # 속성(`[[nodiscard]]`)은 종류 판정에 방해라 뗀다.
    head = re.sub(r"\[\[[^\]]*\]\]", " ", head).strip()
    if re.match(r"namespace\b", head):
        return Statement("namespace", head, lineNum)
    if _kTypeHeadRe.match(head):
        return Statement("type", head, lineNum)
    if re.match(r"(?:typedef|using|static_assert|friend)\b", head):
        return Statement("other", head, lineNum)
    bOperator = _kOperatorRe.search(head) is not None
    flat = _kOperatorRe.sub("operator_", head)
    parenIndex = flat.find("(")
    if parenIndex < 0 or "=" in flat[:parenIndex]:
        return Statement("data", head, lineNum, bTemplate=bTemplate)
    if not bBlock:
        return Statement("decl", head, lineNum, bTemplate=bTemplate, bOperator=bOperator)
    nameMatch = _kNameBeforeParenRe.search(flat[:parenIndex])
    return Statement("function", head, lineNum, bTemplate=bTemplate, bStatic=_kStaticRe.search(head) is not None,
                     bOperator=bOperator, name=nameMatch.group(1) if nameMatch else "")


def readStatementsInternal(text: str, begin: int, end: int) -> list[Statement]:
    """`text[begin:end]`(블록의 안쪽) 의 최상위 문장들. `text` 는 `blankNonCodeInternal` 을 거친 글이다."""
    listStatement: list[Statement] = []
    headStart = begin
    current: Statement | None = None
    index = begin
    while index < end:
        ch = text[index]
        if ch == "{":
            closeIndex = findClosingBraceInternal(text, index)
            if closeIndex < 0 or closeIndex >= end:
                break
            if current is None:
                lead = text[headStart:index]
                lineNum = text.count("\n", 0, headStart + len(lead) - len(lead.lstrip())) + 1
                current = classifyHeadInternal(lead, True, lineNum)
            index = closeIndex + 1
            # `}` 뒤가 `,` `{` 면 생성자 초기화 목록의 중괄호다 — 같은 문장이 이어진다. 그 밖에는 `;` 를 기다리거나(변수 · 형식) 문장이 끝난 것(함수 · 네임스페이스).
            probe = index
            while probe < end and text[probe].isspace():
                probe += 1
            nextChar = text[probe] if probe < end else ""
            if nextChar in (",", "{", ";"):
                continue
            listStatement.append(current)
            current = None
            headStart = index
        elif ch == ";":
            if current is not None:
                listStatement.append(current)
            else:
                lead = text[headStart:index]
                lineNum = text.count("\n", 0, headStart + len(lead) - len(lead.lstrip())) + 1
                if lead.strip():
                    listStatement.append(classifyHeadInternal(lead, False, lineNum))
            current = None
            index += 1
            headStart = index
        else:
            index += 1
    return listStatement


def lineOfInternal(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


@dataclass
class InternalStruct:
    name: str
    lineNum: int
    listStatement: list[Statement]
    bConditional: bool      # 본문에 `#if` 계열이 있다 — 분기마다 모양이 달라 세지 않는다


def readInternalStructsInternal(content: str) -> list[InternalStruct]:
    """`.cpp` 의 `XxxInternal` 구조체 정의를 (이름, 줄, 본문 문장) 으로 읽습니다."""
    if "Internal" not in content:
        return []
    text = blankNonCodeInternal(content)
    listFound: list[InternalStruct] = []
    for match in kInternalStructRe.finditer(text):
        braceIndex = text.find("{", match.end())
        # 이름 줄 다음 첫 비공백이 `{` 가 아니면 정의가 아니다(`struct FooInternal` 만 있는 전방 선언 · 매크로 따위).
        if braceIndex < 0 or text[match.end():braceIndex].strip():
            continue
        closeIndex = findClosingBraceInternal(text, braceIndex)
        if closeIndex < 0:
            continue
        # 전처리 줄은 `text` 에서 지워졌으니 원문에서 본다.
        bConditional = _kConditionalDirectiveRe.search(content, braceIndex, closeIndex) is not None
        listFound.append(InternalStruct(match.group(1), lineOfInternal(text, match.start(1)),
                                        readStatementsInternal(text, braceIndex + 1, closeIndex), bConditional))
    return listFound


def isLoneFunctionStructInternal(found: InternalStruct) -> bool:
    """멤버가 `static` 함수 정의 하나뿐인가 — 상수 · 데이터 · 중첩 타입 · 선언 · 템플릿 · 연산자 · 다른 함수가 하나라도 있으면 아니다."""
    if found.bConditional or len(found.listStatement) != 1:
        return False
    only = found.listStatement[0]
    return only.kind == "function" and only.bStatic and not only.bTemplate and not only.bOperator


def findConditionalGroupsInternal(content: str) -> list[tuple[int, ...]]:
    """각 줄이 속한 `#if` 분기 경로 — 같은 `#if` 의 다른 분기끼리는 동시에 존재하지 않는다. 줄 번호(1 부터) → 경로."""
    listPath: list[tuple[int, ...]] = [()]
    stack: list[list[int]] = []   # [ifId, branchIndex]
    nextId = 0
    for line in content.splitlines():
        directive = _kConditionalDirectiveRe.match(line)
        if directive:
            word = directive.group(1)
            if word in ("if", "ifdef", "ifndef"):
                stack.append([nextId, 0])
                nextId += 1
            elif word in ("elif", "else") and stack:
                stack[-1][1] += 1
            elif word == "endif" and stack:
                stack.pop()
        listPath.append(tuple(ifId * 1000 + branch for ifId, branch in stack))
    return listPath


def countCoexistingStructsInternal(content: str, listFound: list[InternalStruct]) -> list[InternalStruct]:
    """`#if` 의 서로 다른 분기에 있는 정의는 한 번에 하나뿐이다 — 그런 짝은 하나로 쳐서, 함께 존재하는 정의만 돌려줍니다."""
    listPath = findConditionalGroupsInternal(content)
    listKept: list[InternalStruct] = []
    for found in listFound:
        path = listPath[found.lineNum] if found.lineNum < len(listPath) else ()
        bExclusive = False
        for kept in listKept:
            keptPath = listPath[kept.lineNum] if kept.lineNum < len(listPath) else ()
            bExclusive = bExclusive or any(a != b and a // 1000 == b // 1000 for a in path for b in keptPath)
        if not bExclusive:
            listKept.append(found)
    return listKept


def findBareAnonymousFunctionsInternal(content: str) -> list[tuple[str, int]]:
    """
    익명 네임스페이스 **바로 안**에 정의한 자유 함수의 (이름, 줄 번호). 파일 안의 이름은 한 번만(첫 정의) 낸다.

    `XxxInternal` 구조체 안 함수와 함수 지역 람다는 중괄호가 한 단 더 깊어 잡히지 않는다. 연산자 · 소멸자 모양은 뺀다.
    """
    if "namespace" not in content:
        return []
    text = blankNonCodeInternal(content)
    mapNameToLine: dict[str, int] = {}
    for match in _kAnonymousNamespaceRe.finditer(text):
        braceIndex = match.end() - 1
        closeIndex = findClosingBraceInternal(text, braceIndex)
        if closeIndex < 0:
            continue
        for statement in readStatementsInternal(text, braceIndex + 1, closeIndex):
            if statement.kind == "function" and statement.name and not statement.bOperator and "::" not in statement.name:
                mapNameToLine.setdefault(statement.name, statement.lineNum)
    return list(mapNameToLine.items())


#: `rules/CheckCodeConventions.toml` 의 기준선 키 둘 — 게이트(`ruleSchema`)와 여기서 같은 표를 읽는다.
kBaselineSchema = {"internalHelperBaseline": kKindTextList, "anonymousFunctionBaseline": kKindTextList}


@lru_cache(maxsize=1)
def readBaselineInternal() -> dict[str, frozenset[str]]:
    """
    규칙이 생길 때 이미 어기고 있던 파일의 목록(저장소 기준 경로) — 이 파일들의 소거는 따로 하고, 그 전까지는 **목록에 없는 파일의 새 위반만** 막는다.
    파일을 고치면 목록에서 지운다(`Scripts/lint/rules/CheckCodeConventions.toml`). 목록은 줄기만 하고 늘지 않는다.
    """
    data = readRuleFile("CheckCodeConventions", {kExemptionKey: kKindReason, **kBaselineSchema}, bMissingFileIsEmpty=True)
    return {key: frozenset(data[key]) for key in kBaselineSchema}


def readInternalHelperBaselineInternal() -> frozenset[str]:
    """`Naming/MultipleInternalHelper` · `Naming/LoneFunctionInternalHelper` 가 보지 않는 파일."""
    return readBaselineInternal()["internalHelperBaseline"]


def readAnonymousFunctionBaselineInternal() -> frozenset[str]:
    """`Naming/DuplicateAnonymousFunction` 이 위반으로 치지 않는 파일(플랫폼별 짝처럼 한 TU 에 묶이지 않는 것 · 아직 안 고친 것)."""
    return readBaselineInternal()["anonymousFunctionBaseline"]
