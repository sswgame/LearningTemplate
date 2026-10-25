"""
Scripts/common/CppConstants.py

C++ 헤더의 숫자 상수(`constexpr` · `static constexpr` · enum 값)를 include 를 따라가며 풉니다 — 설정 참조 문서의 `Min` · `Max` 가 다른 헤더의 상수 이름일 때 쓴다.

- 상수는 네임스페이스 · 구조체 · enum 으로 한정한 이름(`ChatLimit::kMaxHistoryPage`)으로 색인한다. 부르는 쪽이 적은 이름은 그 이름 끝과 같은 상수에 맞는다.
- 찾는 순서는 시작 헤더 → include 로 닿는 헤더(따옴표 include 만, `IncludeResolver`). 순환 include 는 한 번만 본다.
- 같은 순서에서 서로 다른 값이 둘 이상 맞으면 어느 쪽인지 추측하지 않고 멈춘다(네임스페이스로 한정해 적는다).
- 식은 정수 · 실수 리터럴, 이름, 괄호, `+ - * / % << >> & | ^ ~`, `static_cast<T>(…)` 만 푼다. 그 밖의 모양은 오류로 멈춘다(폴백 없음).
- `float32` 상수는 값을 32 비트 실수로 맞춘다(`kHalfPi = kPi * 0.5f` 는 잘린 π 가 아니라 float 로 계산한 값). `Min` · `Max` 는 그 값을 생성기처럼 double 로 찍고(`formatDoubleInternal`),
  기본값 칸은 같은 값을 되읽으면 같은 float32 인 가장 짧은 글로 찍는다(`formatFloat32Internal`).
"""

from __future__ import annotations

import ast
import re
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from .CodeText import IncludeResolver, blankComments, blankCommentsAndLiterals, iterIncludes


class ConstantError(ValueError):
    """상수를 풀지 못했습니다 — 어느 헤더에서 무엇을 못 찾았는지가 글에 들어 있다."""


_kScopeRe = re.compile(r"\b(?P<kind>namespace|struct|class|union)\s+(?:SW_\w+\s+)?(?P<name>\w+(?:::\w+)*)?\s*(?:final\s*)?(?::[^{;]*)?$")
_kEnumHeadRe = re.compile(r"\benum\s+(?P<scoped>class\s+|struct\s+)?(?P<name>\w+)\s*(?::[^{;]*)?$")
_kConstexprRe = re.compile(r"(?:\b(?:static|inline|const)\s+)*\bconstexpr\s+(?:(?:static|inline|const)\s+)*(?P<type>[\w:]+(?:\s*<[^>]*>)?)\s+(?P<name>\w+)\s*=\s*(?P<expr>.+)$",
                           re.DOTALL)
_kTokenRe = re.compile(r"\s*(?:(?P<num>(?:0[xX][0-9a-fA-F']+|(?:\d[\d']*\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)[uUlLfF]*)|(?P<name>[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)"
                       r"|(?P<op><<|>>|[-+*/%&|^~()]))")
_kNumberRe = re.compile(r"(?P<body>0[xX][0-9a-fA-F']+|(?:\d[\d']*\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)(?P<suffix>[uUlLfF]*)$")
_kArithmeticTypeRe = re.compile(r"(?:(?:std::)?u?int\d*(?:_t)?|float\d*|double|(?:std::)?size_t|(?:std::)?ptrdiff_t|auto)")
_kCastRe = re.compile(r"static_cast\s*<[^<>]*>\s*\(")


@dataclass
class _Symbol:
    """상수 하나 — 한정한 이름 · 정의한 헤더 · 식 글입니다."""

    qualified: str
    header: str
    expr: str
    bEnum: bool = False   # enum 값 — 문서 기본값 칸에서는 이름이 뜻이라 풀지 않는다
    bNonArithmetic: bool = False   # 산술형이 아닌 `constexpr`(enum 형의 센티널 등) — 기본값 칸에서는 이름을 남긴다
    bFloat32: bool = False   # `float32` 상수 — 값을 32 비트 실수로 맞춘다(clang 이 평가한 값을 double 로 읽는 생성기와 같은 수)


@dataclass
class _HeaderInfo:
    """헤더 하나에서 읽은 것 — 상수와 include 한 헤더입니다."""

    listSymbol: list[_Symbol] = field(default_factory=list)
    listInclude: list[str] = field(default_factory=list)
    listUnresolvedInclude: list[str] = field(default_factory=list)


def _joinScopeInternal(listScope: list[str | None], name: str) -> str:
    return "::".join([scope for scope in listScope if scope] + [name])


def _splitEnumBodyInternal(body: str) -> list[tuple[str, str]]:
    """enum 본문 → (값 이름, 식 글). 식이 없으면 앞 값 + 1(첫 값은 0)로 쓴다."""
    listValue: list[tuple[str, str]] = []
    previous = ""
    for part in body.split(","):
        if not part.strip():
            continue
        name, _, expr = part.partition("=")
        name = name.strip()
        expr = expr.strip() or (f"({previous}) + 1" if previous else "0")
        listValue.append((name, expr))
        previous = name
    return listValue


def _closeIndexInternal(blanked: str, openIndex: int) -> int:
    depth = 0
    for index in range(openIndex, len(blanked)):
        if blanked[index] == "{":
            depth += 1
        elif blanked[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ConstantError("중괄호 짝이 맞지 않는 헤더입니다")


def collectSymbolsInternal(header: str, text: str) -> list[_Symbol]:
    """헤더 글에서 이름 있는 범위(namespace · struct · class · union · enum) 안의 상수를 모읍니다. 함수 본문 안의 상수는 세지 않는다."""
    blanked = blankCommentsAndLiterals(text)
    listSymbol: list[_Symbol] = []
    listScope: list[str | None] = []   # None = 이름 없는 블록(함수 본문 등) — 안의 선언은 세지 않는다
    bodyStart = 0
    index = 0
    while index < len(blanked):
        character = blanked[index]
        if character == "{":
            head = " ".join(blanked[bodyStart:index].split())
            enumMatch = _kEnumHeadRe.search(head)
            scopeMatch = _kScopeRe.search(head)
            if enumMatch is not None and None not in listScope:
                closeIndex = _closeIndexInternal(blanked, index)
                bScoped = enumMatch.group("scoped") is not None
                for name, expr in _splitEnumBodyInternal(blanked[index + 1:closeIndex]):
                    listSymbol.append(_Symbol(_joinScopeInternal(listScope, f"{enumMatch.group('name')}::{name}"), header, expr, True))
                    if not bScoped:
                        listSymbol.append(_Symbol(_joinScopeInternal(listScope, name), header, expr, True))
                index = closeIndex
            elif scopeMatch is not None and ("(" not in head.split(scopeMatch.group("kind"))[-1]):
                listScope.append(scopeMatch.group("name") or "")
            else:
                listScope.append(None)
            bodyStart = index + 1
        elif character == "}":
            if listScope:
                listScope.pop()
            bodyStart = index + 1
        elif character == ";":
            if None not in listScope:
                declMatch = _kConstexprRe.search(" ".join(blanked[bodyStart:index].split()))
                if declMatch is not None:
                    listSymbol.append(_Symbol(_joinScopeInternal(listScope, declMatch.group("name")), header, declMatch.group("expr").strip(),
                                              bNonArithmetic=_kArithmeticTypeRe.fullmatch(declMatch.group("type")) is None,
                                              bFloat32=declMatch.group("type") in ("float", "float32")))
            bodyStart = index + 1
        index += 1
    return listSymbol


def _stripCastsInternal(expr: str) -> str:
    """`static_cast<T>( … )` 를 안쪽 식의 괄호로 바꿉니다(값은 그대로 — 숫자 상수끼리의 산식에서 형 변환은 값을 바꾸지 않는 모양만 다룬다)."""
    return _kCastRe.sub("(", expr)


def _parseNumberInternal(token: str) -> int | float:
    match = _kNumberRe.match(token)
    if match is None:
        raise ConstantError(f"숫자 리터럴 '{token}' 을 읽지 못했습니다")
    body = match.group("body").replace("'", "")
    bFloat = "f" in match.group("suffix").lower()
    if body.lower().startswith("0x"):
        return int(body, 16)
    if bFloat or any(mark in body for mark in ".eE"):
        return float(body)
    return int(body, 8) if len(body) > 1 and body.startswith("0") else int(body)


def _evaluateNodeInternal(node: ast.AST) -> int | float:
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)) and not isinstance(node.value, bool):
        return node.value
    if isinstance(node, ast.UnaryOp):
        operand = _evaluateNodeInternal(node.operand)
        if isinstance(node.op, ast.USub):
            return -operand
        if isinstance(node.op, ast.UAdd):
            return operand
        if isinstance(node.op, ast.Invert) and isinstance(operand, int):
            return ~operand
    if isinstance(node, ast.BinOp):
        left, right = _evaluateNodeInternal(node.left), _evaluateNodeInternal(node.right)
        return _applyBinaryInternal(node.op, left, right)
    raise ConstantError("지원하지 않는 식 모양입니다")


def _applyBinaryInternal(operator: ast.operator, left: int | float, right: int | float) -> int | float:
    bInteger = isinstance(left, int) and isinstance(right, int)
    if isinstance(operator, ast.Add):
        return left + right
    if isinstance(operator, ast.Sub):
        return left - right
    if isinstance(operator, ast.Mult):
        return left * right
    if isinstance(operator, ast.Div):
        if right == 0:
            raise ConstantError("0 으로 나눕니다")
        return int(left / right) if bInteger else left / right   # C++ 정수 나눗셈은 0 쪽으로 자른다
    if bInteger and isinstance(operator, (ast.Mod, ast.LShift, ast.RShift, ast.BitAnd, ast.BitOr, ast.BitXor)):
        if isinstance(operator, ast.Mod):
            return left - int(left / right) * right
        mapOperation: dict[type, Callable[[int, int], int]] = {ast.LShift: lambda a, b: a << b, ast.RShift: lambda a, b: a >> b,
                                                              ast.BitAnd: lambda a, b: a & b, ast.BitOr: lambda a, b: a | b, ast.BitXor: lambda a, b: a ^ b}
        return mapOperation[type(operator)](left, right)
    raise ConstantError("지원하지 않는 연산입니다")


def _toFloat32Internal(value: float) -> float:
    """double 을 가장 가까운 32 비트 실수로 맞춘 값(다시 double). 범위를 넘으면 ConstantError."""
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError as error:
        raise ConstantError(f"float32 범위를 넘는 값 {value}") from error


def formatDoubleInternal(value: int | float) -> str:
    """`std::to_chars(double)` 가 찍는 글 — 되돌려 읽으면 같은 double 인 가장 짧은 글. 정수 값은 `.0` 없이 쓴다.
    shortcut: 아주 크거나 작은 값(1e16 이상 · 1e-5 미만)은 to_chars 가 고르는 고정 · 지수 표기와 달라질 수 있다 — 범위 상수에 그런 값은 없다."""
    number = float(value)
    return str(int(number)) if number.is_integer() and abs(number) < 1e16 else repr(number)


def formatFloat32Internal(value: int | float) -> str:
    """32 비트 실수 값을 되돌려 읽으면 같은 값인 가장 짧은 글(`9.810000419616699` → `9.81`). 정수 값은 `9.0` 으로 적는다.
    32 비트로 딱 맞지 않는 값(`float64` 상수)은 그대로 `repr` 이다."""
    number = float(value)
    if number.is_integer() or _toFloat32Internal(number) != number:
        return formatNumberInternal(value)
    for digits in range(1, 10):
        text = f"{number:.{digits}g}"
        if _toFloat32Internal(float(text)) == number:
            return text if ("." in text or "e" in text) else text + ".0"
    return repr(number)


def formatNumberInternal(value: int | float) -> str:
    """문서 칸 글 — 정수는 그대로, 실수는 `repr`(정수로 떨어져도 `.0` 을 남긴다)."""
    return str(value) if isinstance(value, int) else repr(value)


def hasConstantName(text: str) -> bool:
    """식에 상수 이름이 있는가 — 없으면(숫자 리터럴뿐) 문서에 적힌 그대로 둔다."""
    position = 0
    while position < len(text):
        match = _kTokenRe.match(text, position)
        if match is None:
            return False
        if match.group("name"):
            return True
        position = match.end()
    return False


class _NotConstexprError(ConstantError):
    """기본값 칸의 최상위 이름이 `constexpr` 상수가 아니다(enum 값 · 타입 · 함수 이름 등) — 이름이 곧 뜻이라 그대로 둔다."""


def _isPlainExpressionInternal(text: str) -> bool:
    """식 전체가 `_evaluateInternal` 이 읽는 모양(숫자 · 이름 · 연산자 · 괄호)이고 상수 이름이 하나 이상 있는가."""
    remaining = _stripCastsInternal(text.strip())
    position = 0
    bName = False
    while position < len(remaining):
        match = _kTokenRe.match(remaining, position)
        if match is None:
            if remaining[position:].strip():
                return False
            break
        bName = bName or bool(match.group("name"))
        position = match.end()
    return bName


class ConstantResolver:
    """
    헤더의 숫자 상수를 include 를 따라가며 풉니다. 헤더 글 읽기는 부르는 쪽이 넘긴 함수로 한다(문서 한 벌 동안 파일을 한 번만 읽는다).
    """

    def __init__(self, repositoryRoot: Path, readText: Callable[[str], str]) -> None:
        self._includeResolver = IncludeResolver(repositoryRoot)
        self._readText = readText
        self._mapInfo: dict[str, _HeaderInfo] = {}

    def _infoOfInternal(self, header: str) -> _HeaderInfo:
        info = self._mapInfo.get(header)
        if info is not None:
            return info
        text = self._readText(header)
        info = _HeaderInfo(listSymbol=collectSymbolsInternal(header, text))
        for _, includePath in iterIncludes(blankComments(text), bQuotedOnly=True):
            resolved = self._includeResolver.resolveInclude(header, includePath)
            (info.listInclude if resolved is not None else info.listUnresolvedInclude).append(resolved or includePath)
        self._mapInfo[header] = info
        return info

    def _reachableInternal(self, header: str) -> list[str]:
        """`header` 와 include 로 닿는 헤더(너비 우선, 순환은 한 번만)."""
        listOrder = [header]
        setSeen = {header}
        for current in listOrder:
            for included in self._infoOfInternal(current).listInclude:
                if included not in setSeen:
                    setSeen.add(included)
                    listOrder.append(included)
        return listOrder

    def _findSymbolsInternal(self, header: str, name: str) -> list[_Symbol]:
        """시작 헤더 → 닿는 헤더 순서로, 정확히 같은 한정 이름 → 이름 끝이 같은 상수 순서로 첫 무리를 냅니다."""
        listReachable = self._reachableInternal(header)
        for listGroup in ([header], listReachable[1:]):
            listSymbol = [symbol for candidate in listGroup for symbol in self._infoOfInternal(candidate).listSymbol]
            listExact = [symbol for symbol in listSymbol if symbol.qualified == name]
            listSuffix = [symbol for symbol in listSymbol if symbol.qualified.endswith("::" + name)]
            # 정확히 같은 이름과 이름 끝이 같은 상수를 함께 낸다 — 다르면 호출이 멈춘다. 전역 `kMax` 가 있어도 필드가 든 구조체의 `S::kMax` 를 C++ 이 쓰는 경우가 있어
            # (안쪽 스코프가 이긴다) 정확히 같은 쪽만 고르면 틀린 숫자를 조용히 쓴다.
            listFound = listExact + [symbol for symbol in listSuffix if symbol not in listExact]
            if listFound:
                return listFound
        return []

    def _describeSearchInternal(self, header: str, name: str) -> str:
        listReachable = self._reachableInternal(header)
        listUnresolved = [f"{candidate}:{path}" for candidate in listReachable for path in self._infoOfInternal(candidate).listUnresolvedInclude]
        shown = ", ".join(listReachable[:8]) + (f" 외 {len(listReachable) - 8}" if len(listReachable) > 8 else "")
        text = f"{header} 와 include 로 닿는 헤더 {len(listReachable)} 개({shown})에서 상수 '{name}' 을 찾지 못했습니다"
        if listUnresolved:
            text += f" — 풀지 못한 include: {', '.join(listUnresolved[:8])}"
        return text

    def _lookupInternal(self, header: str, name: str, listStack: list[str], bTopConstexprOnly: bool = False) -> int | float:
        listSymbol = self._findSymbolsInternal(header, name)
        if bTopConstexprOnly:
            listSymbol = [symbol for symbol in listSymbol if not symbol.bEnum and not symbol.bNonArithmetic]
        if not listSymbol:
            if bTopConstexprOnly:
                raise _NotConstexprError(self._describeSearchInternal(header, name))
            raise ConstantError(self._describeSearchInternal(header, name))
        setValue: dict[str, int | float] = {}
        for symbol in listSymbol:
            key = f"{symbol.header}::{symbol.qualified}"
            if key in listStack:
                raise ConstantError(f"상수 '{name}' 이 자기 자신을 거쳐 정의됩니다: {' -> '.join(listStack + [key])}")
            value = self._evaluateInternal(symbol.header, symbol.expr, listStack + [key])
            setValue[key] = _toFloat32Internal(value) if symbol.bFloat32 and isinstance(value, float) else value
        if len({repr(value) for value in setValue.values()}) > 1:
            raise ConstantError(f"{header}: 상수 '{name}' 에 값이 다른 정의가 여럿 맞습니다({', '.join(f'{key}={value}' for key, value in setValue.items())}) — "
                                "네임스페이스나 구조체로 한정해 적습니다")
        return next(iter(setValue.values()))

    def _evaluateInternal(self, header: str, expr: str, listStack: list[str], bTopConstexprOnly: bool = False) -> int | float:
        listPiece: list[str] = []
        remaining = _stripCastsInternal(expr.strip())
        position = 0
        while position < len(remaining):
            match = _kTokenRe.match(remaining, position)
            if match is None:
                if not remaining[position:].strip():
                    break
                raise ConstantError(f"{header}: 식 '{expr}' 의 '{remaining[position:].strip()[:20]}' 부터 읽지 못했습니다(지원하는 모양: 숫자, 상수 이름, 괄호, 사칙 · 비트 · 시프트)")
            position = match.end()
            if match.group("name"):
                listPiece.append(f"({formatNumberInternal(self._lookupInternal(header, match.group('name'), listStack, bTopConstexprOnly))})")
            elif match.group("num"):
                listPiece.append(formatNumberInternal(_parseNumberInternal(match.group("num"))))
            else:
                listPiece.append(match.group("op"))
        try:
            return _evaluateNodeInternal(ast.parse(" ".join(listPiece), mode="eval").body)
        except (SyntaxError, ConstantError) as error:
            raise ConstantError(f"{header}: 식 '{expr}' 을 풀지 못했습니다({error})") from error

    def resolveExpression(self, header: str, expr: str) -> str:
        """`header` 에서 본 식 `expr`(`kMax`, `-Limit::kMax`, `Limit::kA * 2`)을 숫자 글로 풉니다. 못 풀면 ConstantError."""
        return formatNumberInternal(self._evaluateInternal(header, expr, []))

    def resolveDefaultExpression(self, header: str, expr: str) -> str | None:
        """기본값 칸의 항목 `expr` 이 `constexpr` 상수(들)의 산식이면 숫자 글로 풉니다. 식이 아니거나(문자열 · 호출 · 쉼표) 최상위 이름이
        enum 값 · 타입 · 함수라 상수가 아니면 None 으로 돌려 이름을 그대로 두게 한다. 상수인데 값이 모호하거나 못 풀면 ConstantError 로 멈춘다."""
        if not _isPlainExpressionInternal(expr):
            return None
        try:
            return formatFloat32Internal(self._evaluateInternal(header, expr, [], True))
        except _NotConstexprError:
            return None

    def resolveRangeConstantValue(self, header: str, name: str) -> float:
        """`Min` · `Max` 항의 상수 이름 하나를 생성기(`AstVisitor::resolveRangeConstants`)가 읽는 값(double)으로 풉니다. 산술형 변수 상수만 맞고 enum 값 ·
        못 찾은 이름은 ConstantError 입니다.
        shortcut: 이름 찾기 규칙이 생성기와 다르다 — 생성기는 필드가 든 타입 → 바깥 타입 → 바깥 네임스페이스 순으로 clang 스코프를 따르고(기반 클래스 · using 제외),
        여기는 시작 헤더 → include 로 닿는 헤더에서 정확히 같은 이름 → 이름 끝이 같은 상수 순서로 찾는다. 같은 이름이 둘 이상이고 값이 다르면 여기서는 멈추지만
        생성기는 안쪽 스코프 것을 쓴다(문서가 멈추는 쪽이라 틀린 숫자를 약속하지는 않는다). 반대로 헤더 사이 이름이 하나뿐이면 생성기는 스코프 밖이라 못 찾는 상수도 여기서는 찾는다."""
        return float(self._lookupInternal(header, name, [], True))


__all__ = ["ConstantError", "ConstantResolver", "formatDoubleInternal", "hasConstantName"]
