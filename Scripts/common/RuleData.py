"""
린트 규칙 데이터 읽기 — `Scripts/lint/rules/<이름>.toml` 하나를 읽고 모양을 검사하는 한 자리입니다.

게이트의 예외 표(`[exemption]` — 경로 · 이름 → 이유)와 등록부 · 게이트의 목록(약어 · 제품 이름 · 티어 · 금지 동사 …)은 코드가 아니라
데이터다. 파이썬 사전에 두면 고칠 때마다 게이트 코드를 열어야 하고, 이유 칸 · 낡은 줄 검사가 표마다 따로 붙는다. 그래서 데이터는
`rules/` 의 TOML 에 두고, 읽기와 모양 검사는 여기 한 번만 적는다.

읽는 쪽이 넘기는 것은 **스키마**(키 → 종류)다. 종류는 넷이다.

- `kKindReason`  : 표. 키 → 이유(빈 글 · 공백만 있는 글은 오류 — 이유 없는 예외는 없다)
- `kKindText`    : 표. 키 → 글(빈 글 허용)
- `kKindInteger` : 표. 키 → 정수
- `kKindTextList`: 배열. 빈 글과 같은 값 두 번은 오류

스키마에 없는 키, 종류가 다른 값, `requiredKeys` 에 든 키가 없는 것, 읽을 수 없는 파일은 `RuleDataError` 다. 게이트(`LintGate`)는 이것을
"검사가 서지 않는다"(종료 2)로 바꾼다. 결과의 표는 `dict`(파일 순서), 배열은 `tuple` 이고 부를 때마다 새 사본이다 — 읽은 원문은 파일마다
한 번만 읽어 둔다(`lru_cache`).

**파이썬 3.10 에서도 읽는다.** CI 린트 잡(ubuntu-22.04 의 `python3`)은 `tomllib`(3.11+)이 없다. 그래서 `tomllib` 이 없으면 이 저장소가
쓰는 TOML 의 부분 집합만 아는 읽기(`readTomlSubset`)로 읽는다 — `[표]`, `키 = 값`(맨 키 · 따옴표 키), 값은 기본 · 리터럴 문자열, 정수,
`true`/`false`, 그 값들의 배열(여러 줄 · 끝 쉼표 · 주석 허용). 점 키 · 인라인 표 · `[[표 배열]]` · 여러 줄 문자열 · 날짜는 오류다.
`selftest/CheckExemptionTables` 가 `rules/` 의 모든 파일을 두 읽기로 읽어 결과가 같은지 본다 — 부분 집합 밖의 문법이 들면 CI 보다 먼저 거기서 진다.
"""

from __future__ import annotations

import re
from functools import lru_cache
from pathlib import Path
from typing import Any, Iterable, Mapping

#: 규칙 데이터 폴더(저장소 기준). 검사 대상 트리(`--root`)가 아니라 **이 스크립트 트리**의 것을 읽는다 — 자기 시험의 임시 트리에도 같은 규칙이 든다.
kRuleDataRelDir = "Scripts/lint/rules"
kRuleDataDir = Path(__file__).resolve().parents[1] / "lint" / "rules"

kKindReason = "reason"
kKindText = "text"
kKindInteger = "integer"
kKindTextList = "textList"
_kTableKinds = (kKindReason, kKindText, kKindInteger)

#: 게이트 예외 표의 키(`LintGate.mapExemption` 이 이것을 읽는다).
kExemptionKey = "exemption"


class RuleDataError(Exception):
    """규칙 데이터 파일을 읽을 수 없거나 모양이 스키마와 다르다."""


def getRuleFilePath(name: str) -> Path:
    """`rules/<name>.toml` 의 경로."""
    return kRuleDataDir / f"{name}.toml"


def listRuleFiles() -> list[Path]:
    """`rules/` 의 TOML 파일 전부(이름 순)."""
    return sorted(kRuleDataDir.glob("*.toml"))


def readRuleFile(name: str, schema: Mapping[str, str], *, requiredKeys: Iterable[str] = (),
                 bMissingFileIsEmpty: bool = False) -> dict[str, Any]:
    """
    `rules/<name>.toml` 을 읽어 `schema` 로 검사한 결과를 돌려줍니다. 스키마의 키는 모두 결과에 있다(파일에 없으면 빈 표 · 빈 튜플).

    `bMissingFileIsEmpty` 면 파일이 없는 것을 오류가 아니라 빈 데이터로 친다 — 예외가 없는 게이트는 파일을 두지 않는다.
    """
    requiredKeys = tuple(requiredKeys)
    path = getRuleFilePath(name)
    if not path.is_file():
        if bMissingFileIsEmpty and not requiredKeys:
            return {key: makeEmptyValueInternal(kind) for key, kind in schema.items()}
        raise RuleDataError(f"{kRuleDataRelDir}/{name}.toml 이 없습니다")
    return validateRuleDataInternal(f"{kRuleDataRelDir}/{name}.toml", readRawRuleFileInternal(str(path)), schema, requiredKeys)


def readTomlText(text: str, sourceName: str) -> dict[str, Any]:
    """TOML 글을 읽습니다 — `tomllib` 이 있으면 그것으로, 없으면 부분 집합 읽기로."""
    try:
        import tomllib
    except ModuleNotFoundError:
        return readTomlSubset(text, sourceName)
    try:
        return tomllib.loads(text)
    except tomllib.TOMLDecodeError as exception:
        raise RuleDataError(f"{sourceName}: TOML 을 읽을 수 없습니다 — {exception}") from exception


def readTomlSubset(text: str, sourceName: str) -> dict[str, Any]:
    """이 저장소가 쓰는 TOML 부분 집합을 읽습니다(머리말). 모르는 문법은 `RuleDataError`."""
    return _TomlSubsetReader(text, sourceName).read()


# --- 내부 ----------------------------------------------------------------------


@lru_cache(maxsize=None)
def readRawRuleFileInternal(pathText: str) -> dict[str, Any]:
    path = Path(pathText)
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exception:
        raise RuleDataError(f"{kRuleDataRelDir}/{path.name}: 읽을 수 없습니다 — {exception}") from exception
    return readTomlText(text, f"{kRuleDataRelDir}/{path.name}")


def makeEmptyValueInternal(kind: str) -> Any:
    return () if kind == kKindTextList else {}


def validateRuleDataInternal(sourceName: str, data: dict[str, Any], schema: Mapping[str, str],
                             requiredKeys: tuple[str, ...]) -> dict[str, Any]:
    listUnknown = [key for key in data if key not in schema]
    if listUnknown:
        raise RuleDataError(f"{sourceName}: 알 수 없는 키 {', '.join(repr(key) for key in listUnknown)} — 아는 키: {', '.join(schema)}")
    result: dict[str, Any] = {}
    for key, kind in schema.items():
        if key not in data:
            if key in requiredKeys:
                raise RuleDataError(f"{sourceName}: '{key}' 가 없습니다")
            result[key] = makeEmptyValueInternal(kind)
            continue
        result[key] = validateValueInternal(f"{sourceName} [{key}]", data[key], kind)
    return result


def validateValueInternal(where: str, value: Any, kind: str) -> Any:
    if kind == kKindTextList:
        if not isinstance(value, list):
            raise RuleDataError(f"{where}: 글 배열이어야 합니다")
        listBad = [item for item in value if not isinstance(item, str) or not item.strip()]
        if listBad:
            raise RuleDataError(f"{where}: 빈 글이나 글이 아닌 값 {listBad!r}")
        listDuplicate = sorted({item for item in value if value.count(item) > 1})
        if listDuplicate:
            raise RuleDataError(f"{where}: 같은 값이 두 번 있습니다 — {', '.join(listDuplicate)}")
        return tuple(value)
    if kind not in _kTableKinds:
        raise RuleDataError(f"{where}: 스키마의 종류 '{kind}' 를 모릅니다(읽는 코드의 결함)")
    if not isinstance(value, dict):
        raise RuleDataError(f"{where}: 표([{where.rsplit('[', 1)[-1]})여야 합니다")
    for key, item in value.items():
        if kind == kKindInteger:
            if not isinstance(item, int) or isinstance(item, bool):
                raise RuleDataError(f"{where}: '{key}' 의 값은 정수여야 합니다")
        elif not isinstance(item, str):
            raise RuleDataError(f"{where}: '{key}' 의 값은 글이어야 합니다")
        elif kind == kKindReason and not item.strip():
            raise RuleDataError(f"{where}: '{key}' 의 이유가 비어 있습니다 — 이유 없는 예외는 없다")
    return dict(value)


_kBareKeyRe = re.compile(r"[A-Za-z0-9_-]+")
_kIntegerRe = re.compile(r"[+-]?(?:0|[1-9][0-9]*)(?![0-9A-Za-z_.:-])")
_kBooleanRe = re.compile(r"(true|false)(?![0-9A-Za-z_])")


class _TomlSubsetReader:
    """`readTomlSubset` 의 몸통 — 글 전체를 한 번 훑는 재귀 하강 읽기."""

    def __init__(self, text: str, sourceName: str) -> None:
        self._text = text.replace("\r\n", "\n")
        self._sourceName = sourceName
        self._position = 0

    def read(self) -> dict[str, Any]:
        root: dict[str, Any] = {}
        current = root
        while True:
            self.skipBlankAndCommentInternal()
            if self._position >= len(self._text):
                return root
            if self._text[self._position] == "[":
                if self._text.startswith("[[", self._position):
                    self.failInternal("표 배열([[…]])은 이 읽기가 모른다")
                self._position += 1
                self.skipSpaceInternal()
                name = self.readKeyInternal()
                self.skipSpaceInternal()
                self.expectInternal("]")
                self.expectLineEndInternal()
                if name in root:
                    self.failInternal(f"표 '{name}' 가 두 번 있다")
                current = root[name] = {}
                continue
            key = self.readKeyInternal()
            self.skipSpaceInternal()
            self.expectInternal("=")
            self.skipSpaceInternal()
            value = self.readValueInternal()
            self.expectLineEndInternal()
            if key in current:
                self.failInternal(f"키 '{key}' 가 두 번 있다")
            current[key] = value

    def failInternal(self, message: str) -> None:
        lineNumber = self._text.count("\n", 0, self._position) + 1
        raise RuleDataError(f"{self._sourceName}:{lineNumber}: {message}")

    def peekInternal(self) -> str:
        return self._text[self._position] if self._position < len(self._text) else ""

    def skipSpaceInternal(self) -> None:
        while self.peekInternal() in (" ", "\t"):
            self._position += 1

    def skipCommentInternal(self) -> None:
        if self.peekInternal() == "#":
            end = self._text.find("\n", self._position)
            self._position = len(self._text) if end < 0 else end

    def skipBlankAndCommentInternal(self) -> None:
        while True:
            self.skipSpaceInternal()
            self.skipCommentInternal()
            if self.peekInternal() == "\n":
                self._position += 1
                continue
            return

    def expectInternal(self, character: str) -> None:
        if self.peekInternal() != character:
            self.failInternal(f"'{character}' 가 와야 한다")
        self._position += 1

    def expectLineEndInternal(self) -> None:
        self.skipSpaceInternal()
        self.skipCommentInternal()
        if self.peekInternal() not in ("", "\n"):
            self.failInternal("줄 끝에 남은 글이 있다")

    def readKeyInternal(self) -> str:
        character = self.peekInternal()
        if character == '"':
            return self.readBasicStringInternal()
        if character == "'":
            return self.readLiteralStringInternal()
        match = _kBareKeyRe.match(self._text, self._position)
        if match is None:
            self.failInternal("키가 와야 한다(맨 키 [A-Za-z0-9_-] 또는 따옴표 키 — 점 키는 이 읽기가 모른다)")
        self._position = match.end()
        return match.group(0)

    def readValueInternal(self) -> Any:
        character = self.peekInternal()
        if character == '"':
            if self._text.startswith('"""', self._position):
                self.failInternal("여러 줄 문자열은 이 읽기가 모른다")
            return self.readBasicStringInternal()
        if character == "'":
            if self._text.startswith("'''", self._position):
                self.failInternal("여러 줄 문자열은 이 읽기가 모른다")
            return self.readLiteralStringInternal()
        if character == "[":
            return self.readArrayInternal()
        for pattern, convert in ((_kIntegerRe, int), (_kBooleanRe, lambda text: text == "true")):
            match = pattern.match(self._text, self._position)
            if match is not None:
                self._position = match.end()
                return convert(match.group(0))
        self.failInternal("이 읽기가 모르는 값(문자열 · 정수 · true/false · 배열만)")

    def readArrayInternal(self) -> list[Any]:
        self._position += 1
        listItem: list[Any] = []
        while True:
            self.skipBlankAndCommentInternal()
            if self.peekInternal() == "]":
                self._position += 1
                return listItem
            listItem.append(self.readValueInternal())
            self.skipBlankAndCommentInternal()
            if self.peekInternal() == ",":
                self._position += 1
                continue
            if self.peekInternal() == "]":
                self._position += 1
                return listItem
            self.failInternal("배열 안에는 ',' 나 ']' 가 와야 한다")

    def readBasicStringInternal(self) -> str:
        import json
        start = self._position
        self._position += 1
        while True:
            character = self.peekInternal()
            if character in ("", "\n"):
                self.failInternal("문자열이 줄 안에서 닫히지 않는다")
            if character == "\\":
                self._position += 2
                continue
            self._position += 1
            if character == '"':
                break
        raw = self._text[start:self._position]
        if "\\U" in raw or "\\/" in raw:
            self.failInternal("이 읽기가 모르는 이스케이프(\\U · \\/)")
        try:
            return json.JSONDecoder(strict=False).decode(raw)
        except ValueError:
            self.failInternal("문자열의 이스케이프를 읽을 수 없다")
        return ""

    def readLiteralStringInternal(self) -> str:
        start = self._position + 1
        end = self._text.find("'", start)
        newline = self._text.find("\n", start)
        if end < 0 or (0 <= newline < end):
            self.failInternal("문자열이 줄 안에서 닫히지 않는다")
        self._position = end + 1
        return self._text[start:end]


__all__ = [
    "RuleDataError", "getRuleFilePath", "kExemptionKey", "kKindInteger", "kKindReason", "kKindText", "kKindTextList", "kRuleDataDir",
    "kRuleDataRelDir", "listRuleFiles", "readRuleFile", "readTomlSubset", "readTomlText",
]
