#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
약어 등록부 — 무엇이 약어이고 어떻게 쓰는지를 정하는 한 자리입니다(docs/plans/AcronymSpelling.md).

게이트(`gate/CheckAcronymSpelling.py`) · 코드모드(`fixer/FormatAcronymSpelling.py`) · 사전 실행 보고가 모두 이 모듈을 읽는다.
목록과 규칙을 한 파일에 두는 이유는 하나다 — 게이트가 막는 철자와 코드모드가 고치는 철자가 갈리면 고친 코드를 게이트가 다시 막는다.

규칙(계획 2절):
- 약어는 대문자로 쓴다. `Ui` 처럼 Pascal 낱말로 쓴 약어는 이름 어디서나 `UI` 가 된다(`UiSystem` → `UISystem`, `updateUi` → `updateUI`,
  `pUiSystem` → `pUISystem`). 이름 맨 앞의 소문자 약어(`uiSystem` · `_gpuScene`)는 camelCase 의 첫 낱말이라 그대로다.
- 바꾸지 않는 것: 문자열 리터럴 · 주석 · `#include` 줄(파일 이름은 `--apply-files` 가 따로 옮긴다), `gv_` 전역 변수, 대문자 매크로(`SW_*`),
  서드파티 이름(`kExternalPrefixRe` · `kExternalNamespace` · `kExternalName`), 제품 이름(`kProductName` — `ImGui` 의 `Gui` 는 약어가 아니다).
- 줄임말(`kNotAcronym`)은 약어가 아니다 — 등록부에 오르면 시작할 때 멈춘다.

`kEnforced` 는 게이트가 막는 약어다. 약어 하나를 트리 전체에서 바꾼 커밋이 그 약어를 여기에 올린다 — 그 전까지 게이트는 보고만 한다.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Iterable

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

from common.CodeText import blankCommentsAndLiterals  # noqa: E402

#: 등록된 약어(대문자 철자). 계획 1절의 목록 + 폴더 표(2-2)의 `RPG` · `JRPG`.
kAcronym: tuple[str, ...] = (
    "UI", "GPU", "AI", "HTTP", "XML", "JSON", "SQL", "IO", "LOD", "RTS", "SRPG", "HUD", "URL", "UUID", "DDS", "TLS", "UDP", "IK",
    "API", "RPC", "PSO", "CPU", "RHI", "AABB", "ID", "QA", "DSP", "MMO", "ACL", "GUI", "RPG", "JRPG",
)

#: 게이트가 막는 약어. 그 약어를 트리 전체에서 바꾼 커밋이 여기에 올린다(계획 2절 순서).
kEnforced: frozenset[str] = frozenset({"HUD", "IK", "DDS", "GPU", "CPU", "RHI"})

#: 복수 `s` 를 붙여 쓰지 않는 약어 — `Ios` 는 `IO` 의 복수가 아니라 플랫폼 이름(iOS)이다.
kNoPlural: frozenset[str] = frozenset({"IO"})

#: 약어처럼 보이지만 줄임말인 낱말 → 이유. 등록부(`kAcronym`)에 오르면 `checkRegistry` 가 멈춘다.
kNotAcronym: dict[str, str] = {
    "Nav": "navigation 의 줄임말",
    "Anim": "animation 의 줄임말",
    "Gimmick": "낱말 그대로",
    "Net": "network 의 줄임말",
    "Dev": "development 의 줄임말",
    "Resp": "response 의 줄임말",
    "Std": "C++ 표준 라이브러리 이름(std)",
    "Idx": "index 의 줄임말 — `Id` 와 다른 낱말이다",
    "Info": "information 의 줄임말",
}


@dataclass(frozen=True)
class ProductName:
    """제품 이름 — 제품 철자가 정본이다. `variants` 는 정본으로 고쳐 쓰는 옛 철자, `acronym` 은 `--acronym` 으로 고르는 이름표."""

    canonical: str
    variants: tuple[str, ...] = ()
    acronym: str = ""


#: 제품 이름. 정본 철자는 약어 규칙이 건드리지 않는다(`ImGui` → `ImGUI` 가 되지 않는다).
kProductName: tuple[ProductName, ...] = (
    ProductName("ImGui"),
    ProductName("OpenSSL", ("OpenSsl",), "SSL"),
    ProductName("SQLite", ("Sqlite",), "SQL"),
    ProductName("Box2D"),
    ProductName("Jolt"),
    ProductName("FreeType"),
    ProductName("HarfBuzz"),
    ProductName("OpenGL"),
    ProductName("DirectX"),
)

#: 고를 수 있는 이름표 전체 — 약어 + 제품 이름표(`SSL` — `OpenSsl` → `OpenSSL`). `--acronym` 을 생략하면 이것이다.
kSelectable: tuple[str, ...] = kAcronym + tuple(dict.fromkeys(product.acronym for product in kProductName
                                                             if product.acronym and product.acronym not in kAcronym))

#: 서드파티 API 의 이름 앞머리 — 이 모양으로 시작하는 식별자는 남의 이름이다(Vulkan · GL · D3D · DXGI · ImGui · SDL · FreeType · …).
kExternalPrefixRe = re.compile(
    r"^(?:Vk|vk|VK_|PFN_|gl[A-Z]|GL[A-Z_]|D3D|ID3D|DXGI|IDXGI|Im[A-Z]|SDL|FT_|hb_|JPH|b2[A-Z]|ma_|stbi|XR_|Xr[A-Z]|xr[A-Z]|"
    r"b2_|cgltf|ozz|rtm|WSA|SSL_|EVP_|BIO_|PQ|redis|sqlite3|curl|CURL)")

#: 이 이름공간 뒤(`ns::`)의 식별자는 남의 이름이다.
kExternalNamespace: frozenset[str] = frozenset({
    "std", "ImGui", "JPH", "ax", "ed", "NodeEditor", "nlohmann", "pugi", "acl", "rtm", "fmt", "Microsoft", "WRL", "DirectX", "tinyxml2", "moodycamel",
})

#: 앞머리로 가려지지 않는 남의 이름(Win32 함수 · DXGI · ImGui 구조체 멤버). 사전 실행(`FormatAcronymSpelling --report`)의
#: "외부 헤더에도 있는 이름" 이 후보를 내고, 사람이 남의 이름인지 보고 여기에 올린다 — 코드모드는 외부 헤더를 직접 읽지 않는다
#: (기계마다 깔린 SDK 가 달라 같은 트리를 다르게 고치면 안 된다).
kExternalName: frozenset[str] = frozenset({
    "GetCurrentProcessId", "GetCurrentThreadId", "GetProcessId", "GetThreadId", "GetWindowThreadProcessId", "CancelIoEx",
    "CreateIoCompletionPort", "UuidCreate", "UuidToStringA", "UuidFromStringA", "dwProcessId", "dwThreadId",
    "VendorId", "DeviceId", "SubSysId",
    "ActiveId", "ActiveIdHasBeenEditedThisFrame", "HoveredId", "DockId",
})

#: 코드모드 · 게이트가 보는 C++ 파일의 확장자 — X-매크로 목록(`.xxx`)도 C++ 로 include 된다.
kCodeSuffix: tuple[str, ...] = (".h", ".hpp", ".inl", ".c", ".cpp", ".cc", ".cxx", ".xxx")

#: 리플렉션 타입 · 속성 이름이 문자열로 든 데이터 파일의 확장자(`--apply-files` 가 같은 치환을 한다).
kDataSuffix: tuple[str, ...] = (".xml", ".json", ".material", ".meta", ".prefab", ".scene")

#: 데이터 파일을 찾는 뿌리.
kDataRelDir: tuple[str, ...] = ("Resource", "Config", "Test")

#: 이름이 이 모양이면 바꾸지 않는다 — `gv_` 전역 변수, `SW_` · 대문자 매크로.
_kSkipNameRe = re.compile(r"^(?:gv_|SW_)|^[A-Z0-9_]+$")

#: `#include` · `#error` · `#warning` · `#pragma message` 줄 — 이름이 아니라 경로 · 글이다.
_kDirectiveTextRe = re.compile(r"^[ \t]*#[ \t]*(?:include|error|warning|pragma[ \t]+message)\b[^\n]*", re.MULTILINE)

#: 원시 문자열 `R"delim( … )delim"`(접두 `u8` · `L` · `u` · `U` 포함).
_kRawStringRe = re.compile(r'(?<![A-Za-z0-9_])(?:u8|[LuU])?R"([^ ()\\\t\n]{0,16})\((.*?)\)\1"', re.DOTALL)


def toPascal(acronym: str) -> str:
    """`UI` → `Ui` — 약어를 Pascal 낱말로 쓴 철자(지금 트리의 모양)."""
    return acronym[0] + acronym[1:].lower()


def checkRegistry() -> list[str]:
    """등록부 자체의 모순(줄임말과 겹침 · 중복 · 강제 목록이 등록부 밖)을 돌려줍니다."""
    listProblem: list[str] = []
    setNotAcronym = {word.upper() for word in kNotAcronym}
    listProblem += [f"'{acronym}' 은 줄임말 표(kNotAcronym)에도 있습니다" for acronym in kAcronym if acronym in setNotAcronym]
    if len(set(kAcronym)) != len(kAcronym):
        listProblem.append("kAcronym 에 같은 약어가 두 번 있습니다")
    listProblem += [f"강제 약어 '{acronym}' 가 kAcronym 에 없습니다" for acronym in kEnforced if acronym not in kAcronym]
    return listProblem


def resolveAcronyms(listArgument: Iterable[str] | None) -> tuple[str, ...]:
    """`--acronym Ui,Gpu` 같은 인자를 등록부 철자로 풉니다. 비면 전체. 제품 이름표(`SSL`)도 받습니다."""
    listName = [name.strip().upper() for argument in (listArgument or ()) for name in argument.split(",") if name.strip()]
    if not listName:
        return kSelectable
    setKnown = set(kAcronym) | {product.acronym for product in kProductName if product.acronym}
    listUnknown = [name for name in listName if name not in setKnown]
    if listUnknown:
        raise ValueError(f"등록부에 없는 약어: {', '.join(listUnknown)} (Scripts/lint/AcronymRegistry.py 의 kAcronym)")
    return tuple(dict.fromkeys(listName))


@lru_cache(maxsize=64)
def compileAcronymRe(acronyms: tuple[str, ...]) -> re.Pattern[str]:
    """
    Pascal 철자의 약어 낱말 하나를 찾는 정규식. 그룹 `word` 가 약어, `plural` 이 복수 `s`.

    낱말 경계: 뒤에 소문자가 오면 다른 낱말의 앞부분이다(`Idle` · `Aim` · `Guid`). 앞은 묻지 않는다 — `EUiMode` 의 `E` 는 접두다.
    """
    listAlternative = []
    for acronym in sorted(acronyms, key=len, reverse=True):
        if acronym not in kAcronym:
            continue
        pascal = re.escape(toPascal(acronym))
        plural = "" if acronym in kNoPlural else "(?P<plural_x>s)?"
        listAlternative.append(f"{pascal}{plural}")
    if not listAlternative:
        return re.compile(r"(?!x)x")
    # 복수 그룹 이름은 갈래마다 달라야 해서 번호를 붙인다.
    body = "|".join(alternative.replace("plural_x", f"plural{index}") for index, alternative in enumerate(listAlternative))
    return re.compile(f"(?:{body})(?![a-z])")


def isExternalName(name: str, qualifier: str = "") -> bool:
    """남의 이름인가 — 앞머리 · 이름공간 · 이름 표로만 판정한다(외부 헤더 훑기는 사전 실행의 의심 목록일 뿐이다)."""
    return bool(kExternalPrefixRe.match(name)) or qualifier in kExternalNamespace or name in kExternalName


def findProductSpans(name: str, acronyms: tuple[str, ...]) -> tuple[list[tuple[int, int]], list[tuple[int, int, str]]]:
    """이름 안의 제품 이름 자리 — (지킬 구간들, 고쳐 쓸 구간 · 정본 철자들)."""
    listProtected: list[tuple[int, int]] = []
    listRespell: list[tuple[int, int, str]] = []
    for product in kProductName:
        for match in re.finditer(re.escape(product.canonical), name):
            listProtected.append(match.span())
        if product.acronym and product.acronym in acronyms:
            for variant in product.variants:
                for match in re.finditer(re.escape(variant) + r"(?![a-z])", name):
                    listRespell.append((match.start(), match.end(), product.canonical))
    return listProtected, listRespell


@lru_cache(maxsize=None)
def respellName(name: str, acronyms: tuple[str, ...] = kSelectable) -> str:
    """
    이름 하나의 새 철자. 바꿀 것이 없으면 그대로 돌려줍니다(남의 이름인지는 부르는 쪽이 먼저 본다).

    `UiSystem` → `UISystem`, `updateUi` → `updateUI`, `_pGpuScene` → `_pGPUScene`, `entityIds` → `entityIDs`, `uiSystem` · `RHIDevice` · `ImGuiLayer` 는 그대로.
    """
    if _kSkipNameRe.match(name):
        return name
    pattern = compileAcronymRe(acronyms)
    listProtected, listRespell = findProductSpans(name, acronyms)
    listEdit: list[tuple[int, int, str]] = list(listRespell)
    for match in pattern.finditer(name):
        start, end = match.span()
        if any(start < protectEnd and protectStart < end for protectStart, protectEnd in listProtected + [(s, e) for s, e, _ in listRespell]):
            continue
        word = match.group(0)
        bPlural = any(value for key, value in match.groupdict().items() if key.startswith("plural") and value)
        listEdit.append((start, end, word[:-1].upper() + "s" if bPlural else word.upper()))
    if not listEdit:
        return name
    pieces: list[str] = []
    cursor = 0
    for start, end, replacement in sorted(listEdit):
        if start < cursor:
            continue
        pieces += [name[cursor:start], replacement]
        cursor = end
    pieces.append(name[cursor:])
    return "".join(pieces)


@lru_cache(maxsize=None)
def changedAcronyms(name: str, acronyms: tuple[str, ...] = kSelectable) -> tuple[str, ...]:
    """이 이름을 바꾸게 하는 약어들(보고의 약어별 집계용)."""
    return tuple(acronym for acronym in acronyms if respellName(name, (acronym,)) != name)


def findAdjacentAcronyms(name: str) -> list[str]:
    """
    대문자 약어가 이어 붙은 자리(규칙 4) — `RHIUIPass` 의 `RHIUI`, `GPUID` 의 `GPUID`. 약어 하나(`AABB` · `UUID`)는 아니다.
    대문자 묶음을 등록부 약어 둘 이상으로 남김없이 나눌 수 있을 때만 잡는다.
    """
    if _kSkipNameRe.match(name):
        return []
    listFound: list[str] = []
    for match in re.finditer(r"[A-Z]{3,}", name):
        run = match.group(0)
        after = name[match.end():match.end() + 2]
        if after[:1].islower() and not (after[:1] == "s" and not after[1:2].islower()):
            run = run[:-1]   # 마지막 대문자는 다음 낱말의 첫 글자다(`RHIUIPass` → `RHIUI`). 복수 `s` 는 낱말이 아니다(`GPUIDs`).
        if run not in kAcronym and splitIntoAcronyms(run):
            listFound.append(run)
    return listFound


@lru_cache(maxsize=4096)
def splitIntoAcronyms(run: str) -> tuple[str, ...]:
    """`run` 을 등록부 약어 둘 이상으로 남김없이 나눈 한 가지(없으면 빈 튜플)."""
    def splitFrom(index: int) -> tuple[str, ...] | None:
        if index == len(run):
            return ()
        for acronym in kAcronym:
            if run.startswith(acronym, index):
                rest = splitFrom(index + len(acronym))
                if rest is not None:
                    return (acronym,) + rest
        return None
    found = splitFrom(0)
    return found if found and len(found) >= 2 else ()


def maskCode(text: str) -> str:
    """주석 · 문자열 · 원시 문자열 · `#include` 같은 글 줄을 같은 길이의 공백으로 가립니다(줄바꿈은 남긴다)."""
    def blank(match: re.Match[str]) -> str:
        return "\n".join(" " * len(part) for part in match.group(0).split("\n"))

    masked = _kRawStringRe.sub(blank, text)
    masked = blankCommentsAndLiterals(masked)
    return _kDirectiveTextRe.sub(blank, masked)


@dataclass(frozen=True)
class NameSite:
    """코드 안의 이름 한 자리."""

    start: int
    end: int
    name: str
    qualifier: str


def iterateNameSites(text: str, acronyms: tuple[str, ...] = kSelectable, masked: str | None = None) -> list[NameSite]:
    """
    가린 글에서 약어 후보가 든 이름 자리만 돌려줍니다. 전체 토큰을 다 돌지 않고 약어 정규식으로 먼저 찾고 이름 경계로 넓힌다(게이트 속도).
    `qualifier` 는 바로 앞의 `ns::` 이름공간(없으면 빈 글).
    """
    masked = maskCode(text) if masked is None else masked
    pattern = compileAcronymRe(acronyms)
    listSite: list[NameSite] = []
    seenStart: set[int] = set()
    productRe = _compileProductRe()
    for finder in (pattern, productRe):
        for match in finder.finditer(masked):
            start = match.start()
            while start > 0 and (masked[start - 1].isalnum() or masked[start - 1] == "_"):
                start -= 1
            if start in seenStart or (masked[start].isdigit()):
                continue
            end = match.end()
            while end < len(masked) and (masked[end].isalnum() or masked[end] == "_"):
                end += 1
            seenStart.add(start)
            qualifierMatch = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*::\s*$", masked[max(0, start - 64):start])
            listSite.append(NameSite(start, end, masked[start:end], qualifierMatch.group(1) if qualifierMatch else ""))
    listSite.sort(key=lambda site: site.start)
    return listSite


@lru_cache(maxsize=1)
def _compileProductRe() -> re.Pattern[str]:
    variants = [re.escape(variant) for product in kProductName for variant in product.variants]
    return re.compile("|".join(variants) if variants else r"(?!x)x")


def respellCode(text: str, acronyms: tuple[str, ...] = kSelectable, *,
                bTypeNames: bool = True, bValueNames: bool = True) -> tuple[str, dict[str, str]]:
    """
    C++ 글 하나의 이름을 새 철자로 고칩니다. (새 글, {옛 이름: 새 이름}) 를 돌려준다.
    `bTypeNames` 는 대문자로 시작하는 이름(타입 · 이름공간 · 열거형), `bValueNames` 는 소문자 · `_` 로 시작하는 이름(함수 · 변수 · 멤버).
    """
    listPiece: list[str] = []
    mapRename: dict[str, str] = {}
    cursor = 0
    for site in iterateNameSites(text, acronyms):
        bType = site.name[0].isupper()
        if (bType and not bTypeNames) or (not bType and not bValueNames):
            continue
        if isExternalName(site.name, site.qualifier):
            continue
        newName = respellName(site.name, acronyms)
        if newName == site.name:
            continue
        mapRename[site.name] = newName
        listPiece += [text[cursor:site.start], newName]
        cursor = site.end
    if not mapRename:
        return text, {}
    listPiece.append(text[cursor:])
    return "".join(listPiece), mapRename


def respellPathPart(part: str, acronyms: tuple[str, ...] = kSelectable) -> str:
    """파일 · 폴더 이름 한 조각의 새 철자 — 확장자 앞(`UiSystem.h` → `UISystem.h`)만 본다. 소문자 확장자(`*.ui.xml`)는 그대로."""
    stem, dot, suffix = part.partition(".")
    return respellName(stem, acronyms) + dot + suffix


__all__ = [
    "NameSite", "ProductName", "changedAcronyms", "checkRegistry", "compileAcronymRe", "findAdjacentAcronyms", "isExternalName",
    "iterateNameSites", "kAcronym", "kCodeSuffix", "kDataRelDir", "kDataSuffix", "kEnforced", "kExternalName", "kExternalNamespace",
    "kExternalPrefixRe", "kNotAcronym", "kProductName", "kSelectable", "maskCode", "resolveAcronyms", "respellCode", "respellName", "respellPathPart", "splitIntoAcronyms",
    "toPascal",
]
