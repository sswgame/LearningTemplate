"""
Scripts/lint/CppScope.py

C++ 여는 중괄호의 머리를 읽어 그 중괄호가 무엇을 여는지 가른다 — 선언 자리(`namespace` · `class` · `struct` · `union`), 건너뛸 것(`enum` · 초기화),
함수 본문(이름과 함께), 람다 본문. 보고서 `RunFunctionSize` 가 함수 크기를 잴 때 쓰던 것을 게이트 `CheckStaticLocals` 가 같이 읽도록 옮겼다.

둘 다 층 2 라 서로의 모듈을 import 할 수 없어(층 게이트 `CheckScriptLayers`) 린트 틀 층(층 1)에 둔다.
입력은 `common.CodeText.blankCommentsAndLiterals` 로 주석 · 문자열을 지우고 `blankPreprocessor` 로 전처리 줄을 지운 글이다(길이와 줄바꿈은 그대로).
"""

from __future__ import annotations

import re

from common.CodeText import blankMatch

#: 전처리 줄 — `\` 로 이어진 줄까지 한 덩어리.
_kPreprocessorRe = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.MULTILINE)
#: 머리 끝에서 떼어 낼 한정어(함수 본문 앞에 올 수 있는 것).
_kFunctionQualifiers: tuple[str, ...] = ("const", "volatile", "noexcept", "override", "final", "mutable", "&&", "&")
_kLambdaQualifiers: tuple[str, ...] = ("mutable", "constexpr", "noexcept")
#: 접근 지정 레이블(`public:`) — 머리 앞에 남아 있을 수 있다.
_kAccessLabelRe = re.compile(r"^\s*(?:public|protected|private)\s*:(?!:)")
_kIdentifierEndRe = re.compile(r"([A-Za-z_~][A-Za-z_0-9]*)\s*$")
_kOperatorEndRe = re.compile(r"\boperator\s*(\(\)|\[\]|[^\sA-Za-z_0-9(]+|\s+[A-Za-z_][A-Za-z_0-9:<>*&\s]*?)\s*$")
_kTemplateHeadRe = re.compile(r"^\s*template\s*<")
_kParenRe = re.compile(r"[()]")
#: 끝의 이름 앞에 붙은 한정 이름(`Foo::Bar<T>::`) — 이름마다 정규식을 새로 만들면 컴파일이 머리 읽기의 절반을 차지해 미리 만든 것 하나로 본다.
_kScopePrefixRe = re.compile(r"((?:[A-Za-z_~][A-Za-z_0-9]*\s*(?:<[^<>;{}]*>)?\s*::\s*)+)[A-Za-z_~][A-Za-z_0-9]*\s*$")
#: 람다인지 볼 때 여는 중괄호 앞에서 읽는 글자 수(캡처 목록 · 인자 목록 · 뒤 반환 형식이 들어갈 만큼).
_kLambdaLookBehind = 4000


def blankPreprocessor(text: str) -> str:
    """전처리 줄을 같은 길이의 공백으로 바꿉니다(줄바꿈은 남긴다)."""
    return _kPreprocessorRe.sub(blankMatch, text)


def stripTrailingWordInternal(text: str, listWord: tuple[str, ...]) -> str:
    """끝의 낱말 하나(@p listWord 가운데)를 떼어 냅니다. 끝에 `$` 를 건 정규식은 긴 글에서 느려 글 끝만 본다."""
    for word in listWord:
        if not text.endswith(word):
            continue
        rest = text[:-len(word)]
        if word[0].isalpha() and rest and (rest[-1].isalnum() or rest[-1] == "_"):
            continue   # `myconst` 처럼 낱말의 일부다
        return rest.rstrip()
    return text


def findMatchingOpenInternal(text: str, closeIndex: int) -> int:
    """`text[closeIndex]` 의 `)` 와 짝인 `(` 의 위치입니다. 없으면 -1."""
    depth = 0
    for index in range(closeIndex, -1, -1):
        character = text[index]
        if character == ")":
            depth += 1
        elif character == "(":
            depth -= 1
            if depth == 0:
                return index
    return -1


def topLevelOutsideParensInternal(head: str) -> str:
    """괄호 · 꺾쇠 바깥 글만 남깁니다(안은 공백). `=` · `:` 를 최상위에서 찾을 때 쓴다."""
    if "(" not in head and ")" not in head:
        return head   # 대부분의 머리(네임스페이스 · 클래스)는 괄호가 없다 — 글자마다 도는 비용을 아낀다
    listPart: list[str] = []
    parenDepth = 0
    last = 0
    for match in _kParenRe.finditer(head):
        index = match.start()
        listPart.append(head[last:index] if parenDepth == 0 else " " * (index - last))   # 괄호 사이의 글은 깊이가 0 일 때만 남긴다
        if match.group(0) == "(":
            parenDepth += 1
            listPart.append("(" if parenDepth == 0 else " ")
        else:
            parenDepth -= 1
            listPart.append(" ")
        last = index + 1
    listPart.append(head[last:] if parenDepth == 0 else " " * (len(head) - last))
    return "".join(listPart)


def stripTemplateHeadInternal(head: str) -> str:
    """머리 앞의 `template < … >` 를 떼어 냅니다(여러 겹이어도)."""
    while _kTemplateHeadRe.match(head):
        start = head.index("<")
        depth = 0
        for index in range(start, len(head)):
            if head[index] == "<":
                depth += 1
            elif head[index] == ">":
                depth -= 1
                if depth == 0:
                    head = head[index + 1:]
                    break
        else:
            return head
    return head


def cutInitializerListInternal(head: str) -> str:
    """최상위 `)` 뒤의 첫 단독 `:`(생성자 초기화 목록)이나 `->`(뒤 반환 형식)에서 머리를 자릅니다."""
    outside = topLevelOutsideParensInternal(head)
    firstClose = head.find(")")
    if firstClose < 0:
        return head
    for index in range(firstClose, len(outside)):
        if outside.startswith("->", index):
            return head[:index]
        if outside[index] == ":" and outside[index - 1:index] != ":" and outside[index + 1:index + 2] != ":":
            return head[:index]
    return head


def readFunctionNameInternal(head: str) -> str | None:
    """함수 본문 머리면 함수 이름을, 아니면 None 을 돌려줍니다."""
    head = _kAccessLabelRe.sub("", head)
    cut = cutInitializerListInternal(head)
    initializerList = head[len(cut):].strip()
    if initializerList.startswith(":") and re.search(r"[A-Za-z_0-9>]$", initializerList):
        return None   # 초기화 목록 안의 중괄호 초기화(`: _a( a ), _b{`) — 본문은 그 뒤다
    head = cut.rstrip()
    while True:
        stripped = stripTrailingWordInternal(head.rstrip(), _kFunctionQualifiers)
        if stripped.endswith(")"):
            # `noexcept( … )` 는 한정어다 — 그 앞이 또 `)` 면 떼어 낸다.
            openIndex = findMatchingOpenInternal(stripped, len(stripped) - 1)
            before = stripped[:openIndex].rstrip() if openIndex >= 0 else ""
            if before.endswith("noexcept") and before[:-len("noexcept")].rstrip().endswith(")"):
                head = before[:-len("noexcept")].rstrip()
                continue
        if stripped == head:
            break
        head = stripped
    if not head.endswith(")"):
        return None
    openIndex = findMatchingOpenInternal(head, len(head) - 1)
    if openIndex <= 0:
        return None
    before = head[:openIndex].rstrip()
    if before.endswith("]"):
        return None   # 선언 자리의 람다 — 초기화의 일부다
    operatorMatch = _kOperatorEndRe.search(before)
    if operatorMatch:
        scopeMatch = re.search(r"((?:[A-Za-z_][A-Za-z_0-9]*\s*::\s*)*)operator", before[:operatorMatch.end()])
        prefix = re.sub(r"\s+", "", scopeMatch.group(1)) if scopeMatch else ""
        return prefix + "operator" + re.sub(r"\s+", " ", operatorMatch.group(1)).strip()
    nameMatch = _kIdentifierEndRe.search(before)
    if not nameMatch:
        return None
    name = nameMatch.group(1)
    if name in {"if", "for", "while", "switch", "catch", "return", "sizeof", "alignas", "decltype"}:
        return None
    # 이름 앞의 한정 이름(`Foo::bar`)을 붙인다.
    scopeMatch = _kScopePrefixRe.search(before)
    if scopeMatch:
        name = re.sub(r"\s+", "", scopeMatch.group(1)) + name
    if name.isupper() or re.fullmatch(r"[A-Z][A-Z0-9_]+", name):
        arguments = re.sub(r"\s+", " ", head[openIndex + 1:-1]).strip()
        name = f"{name}({arguments})"
    return name


def classifyHead(head: str) -> str:
    """선언 자리의 여는 중괄호 머리 → `scope`(네임스페이스 · 클래스), `skip`(enum · 초기화), 아니면 함수 이름."""
    stripped = stripTemplateHeadInternal(head).strip()
    outside = topLevelOutsideParensInternal(stripped)
    if re.search(r"\bnamespace\b", outside) or re.fullmatch(r"extern\s*", outside.strip() + " "):
        return "scope"
    if re.search(r"\benum\b", outside):
        return "skip"
    if re.search(r"(?<![=!<>])=(?!=)", re.sub(r"\boperator\s*\S*=", "", outside)) or re.search(r"\breturn\b", outside):
        return "skip"
    name = readFunctionNameInternal(stripped)
    if re.search(r"\b(?:class|struct|union)\b", outside) and name is None:
        return "scope"
    if name is None:
        return "skip"
    return "function:" + name


def isLambdaOpen(text: str, braceIndex: int) -> bool:
    """`text[braceIndex]` 의 `{` 가 람다 본문을 여는가. 앞의 `_kLambdaLookBehind` 글자만 본다(파일 앞부분을 통째로 자르면 느리다)."""
    before = text[max(0, braceIndex - _kLambdaLookBehind):braceIndex].rstrip()
    # `-> 타입` 은 떼어 낸다(타입 안에 `{` 는 없다).
    arrowIndex = before.rfind("->")
    if arrowIndex >= 0 and re.fullmatch(r"->\s*[A-Za-z_0-9:<>,\s*&]*", before[arrowIndex:]):
        before = before[:arrowIndex].rstrip()
    while True:
        stripped = stripTrailingWordInternal(before, _kLambdaQualifiers)
        if stripped == before:
            break
        before = stripped
    if before.endswith("]"):
        return True
    if before.endswith(")"):
        openIndex = findMatchingOpenInternal(before, len(before) - 1)
        return openIndex > 0 and before[:openIndex].rstrip().endswith("]")
    return False
