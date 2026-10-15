#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file MakeEditorTheme.py
@brief 에디터 테마 토큰(`Resource/editor/theme/<이름>.tokens.json`)을 Figma 와 주고받고 검사합니다.

    py -3 -m Scripts editor-theme import-figma <입력.json> [--set <컬렉션|세트>] [--mode <모드>] [--name <테마>] [--out <파일>]
    py -3 -m Scripts editor-theme export-figma <tokens.json...> --out <파일>
    py -3 -m Scripts editor-theme check <tokens.json...> [--fail-on-warning]

정본은 저장소의 tokens.json 이고 Figma 는 선택 입력이다(docs/plans/EditorUX.md). 인증 · 네트워크 없이 파일만 읽고 쓴다.

- `import-figma` 의 입력은 두 가지다. (a) Figma REST `GET /v1/files/:key/variables/local` 응답(`meta.variables` · `meta.variableCollections`,
  별칭 `VARIABLE_ALIAS` 를 따라간다), (b) W3C Design Tokens(DTCG) JSON(`$value` · `$type`, 중첩 그룹, 별칭 `{color.accent}`).
  변수 이름 경로(`color/accent` · `color.accent`)는 아래 `kSchema` 표 하나로 토큰 키(`_color._accent`)에 대응시킨다.
  표에 없는 변수는 경고로 보고하고 버린다. 출력 파일이 이미 있으면 그 위에 덮어쓰고(없는 키는 기존 값 유지), 없으면 다크 세트가 바탕이다.
- `export-figma` 는 tokens.json 들을 DTCG JSON 으로 쓴다. 최상위 키 = 세트(= Figma 모드), 값은 `$type: color | number`.
- 출력 서식: CRLF, 들여쓰기 4, 색은 대문자 16진 `#RRGGBB` 또는 `#RRGGBBAA`.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot  # noqa: E402

#: 토큰 키의 정본 표 — 그룹 → 키 이름(앞 `_` 없이). 변수 이름 매핑 · 키 집합 검사 · 수 범위 검사가 모두 이 표를 쓴다.
#: 같은 키 집합이 C++ 쪽 구조체(리플렉션 멤버)와 다크 세트에도 있다 — 하나를 바꾸면 셋을 같이 바꾼다(검사 `check` 가 다크 세트와 견준다).
kSchema: dict[str, tuple[str, ...]] = {
    "color": ("text", "textMuted", "accent", "windowBg", "panelBg", "headerBg", "frameBg", "border", "success", "warning", "error", "info",
              "scrollbarBg", "dockingEmptyBg", "tableRowBgAlt", "overlayBg", "selectionOutline", "highlight", "toggleInactive",
              "axisX", "axisY", "axisZ"),
    "radius": ("window", "frame", "popup", "tab", "scrollbar", "grab"),
    "spacing": ("windowPaddingX", "windowPaddingY", "framePaddingX", "framePaddingY", "itemSpacingX", "itemSpacingY",
                "itemInnerSpacingX", "itemInnerSpacingY", "indent", "scrollbarSize", "grabMinSize"),
    "stroke": ("window", "child", "popup", "frame", "tab"),
    "typography": ("fontSize", "iconScale"),
}
#: 그룹 없이 최상위에 놓이는 수 키.
kScalarKey: tuple[str, ...] = ("density",)
#: 정수로 쓰지 않는 수 키(1.0 처럼 소수점을 남긴다).
kFloatKey: tuple[str, ...] = ("iconScale", "density")
#: 수 범위(포함). 키 이름 또는 그룹 이름으로 찾는다 — 키 이름이 먼저.
kRange: dict[str, tuple[float, float]] = {
    "radius": (0, 32), "spacing": (0, 64), "stroke": (0, 8), "fontSize": (8, 32), "iconScale": (0.5, 3.0), "density": (0.5, 2.0),
}
#: 대비비 검사 쌍(전경, 배경) — 일반 4.5:1, 고대비 세트 7:1.
kContrastPair: tuple[tuple[str, str], ...] = (("text", "windowBg"), ("text", "panelBg"), ("text", "frameBg"), ("textMuted", "windowBg"))
kContrastNormal = 4.5
kContrastHigh = 7.0
kHighContrastName = "highcontrast"
kThemeFolder = "Resource/editor/theme"
kBaseThemeName = "dark"
kColorPattern = re.compile(r"^#[0-9A-F]{6}(?:[0-9A-F]{2})?$")
_kAnyHexPattern = re.compile(r"^#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$")
_kAliasPattern = re.compile(r"^\{(.+)\}$")
_kAliasDepth = 16


class ThemeError(Exception):
    """입력을 읽을 수 없거나 선택이 모호한 오류 — 메시지가 사용자에게 보일 문장이다."""


def themePathInternal(root: Path, name: str) -> Path:
    return root / kThemeFolder / f"{name}.tokens.json"


def readJson(path: Path) -> dict:
    """UTF-8 JSON 파일 하나를 읽는다(BOM 허용). 최상위가 오브젝트가 아니면 ThemeError."""
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError) as error:
        raise ThemeError(f"{path}: cannot read JSON ({error})") from error
    if not isinstance(data, dict):
        raise ThemeError(f"{path}: top level must be an object")
    return data


def formatTokens(data: dict) -> str:
    """tokens 오브젝트 → 파일 글(들여쓰기 4, CRLF, 끝 줄바꿈)."""
    return json.dumps(data, indent=4, ensure_ascii=False).replace("\n", "\r\n") + "\r\n"


def writeTokens(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as file:
        file.write(formatTokens(data))


def formatColor(red: float, green: float, blue: float, alpha: float = 1.0) -> str:
    """0..1 채널 → `#RRGGBB`(알파가 1 이 아니면 `#RRGGBBAA`)."""
    def channel(value: float) -> int:
        return max(0, min(255, round(value * 255)))
    text = f"#{channel(red):02X}{channel(green):02X}{channel(blue):02X}"
    return text if channel(alpha) == 255 else text + f"{channel(alpha):02X}"


def parseColor(value: object) -> str | None:
    """색 값(16진 문자열 · DTCG 색 오브젝트 · Figma {r,g,b,a})을 정본 형식으로. 읽을 수 없으면 None."""
    if isinstance(value, str):
        return value.upper() if _kAnyHexPattern.match(value) else None
    if isinstance(value, dict):
        if "r" in value and "g" in value and "b" in value:
            channel = [value["r"], value["g"], value["b"]]
            alpha = value.get("a", 1.0)
        elif isinstance(value.get("components"), list) and len(value["components"]) == 3:
            channel = value["components"]
            alpha = value.get("alpha", 1.0)
        else:
            return None
        if all(isinstance(item, (int, float)) for item in [*channel, alpha]):
            return formatColor(channel[0], channel[1], channel[2], alpha)
        if isinstance(value.get("hex"), str):
            return parseColor(value["hex"])
    return None


def parseNumber(value: object) -> float | None:
    """수 값(수 · `8px` · {value, unit:px})을 수로. 읽을 수 없으면 None."""
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        match = re.fullmatch(r"\s*(-?\d+(?:\.\d+)?)\s*(?:px)?\s*", value)
        return float(match.group(1)) if match else None
    if isinstance(value, dict) and value.get("unit", "px") == "px":
        return parseNumber(value.get("value"))
    return None


def mapVariableName(name: str) -> tuple[str, str] | None:
    """변수 이름 경로 → (그룹, 키). 그룹 없는 키는 ("", 키). 표에 없으면 None. 철자의 대소문자는 가리지 않는다."""
    listPart = [part for part in re.split(r"[/.]", name.strip()) if part]
    if len(listPart) == 1:
        for key in kScalarKey:
            if listPart[0].lower() == key.lower():
                return "", key
        return None
    if len(listPart) != 2:
        return None
    for group, listKey in kSchema.items():
        if listPart[0].lower() != group.lower():
            continue
        for key in listKey:
            if listPart[1].lower() == key.lower():
                return group, key
    return None


# ---------------------------------------------------------------------------------------------------------------------
# 입력 읽기 — 둘 다 "변수 이름 경로 → 값(색은 문자열, 수는 수)" 평평한 표로 만든다.
# ---------------------------------------------------------------------------------------------------------------------

def figmaValueInternal(variables: dict, variable: dict, modeName: str | None, modeId: str, depth: int, listWarning: list[str]) -> object:
    """Figma 변수 하나의 모드 값. 별칭이면 대상의 같은 이름 모드(없으면 기본 모드) 값을 따라간다."""
    value = variable.get("valuesByMode", {}).get(modeId)
    if isinstance(value, dict) and value.get("type") == "VARIABLE_ALIAS":
        target = variables.get(value.get("id"))
        if target is None or depth >= _kAliasDepth:
            listWarning.append(f"alias of '{variable.get('name')}' cannot be resolved (missing target or cycle)")
            return None
        return figmaValueInternal(variables, target, modeName, targetModeInternal(target, modeName, variables), depth + 1, listWarning)
    return value


def targetModeInternal(variable: dict, modeName: str | None, variables: dict) -> str:
    """별칭 대상 변수의 모드 id — 같은 이름 모드가 있으면 그것, 없으면 기본 모드."""
    collection = variables["__collections__"].get(variable.get("variableCollectionId"), {})
    for mode in collection.get("modes", []):
        if mode.get("name") == modeName:
            return mode["modeId"]
    return collection.get("defaultModeId") or next(iter(variable.get("valuesByMode", {})), "")


def extractFigmaRest(data: dict, setName: str | None, modeName: str | None, listWarning: list[str]) -> dict[str, object]:
    """Figma REST 변수 응답 → {변수 이름: 값}. COLOR 는 16진 문자열, FLOAT 는 수."""
    meta = data.get("meta", data)
    variables = dict(meta.get("variables", {}))
    collections = dict(meta.get("variableCollections", {}))
    if not variables or not collections:
        raise ThemeError("not a Figma variables response (meta.variables / meta.variableCollections missing)")
    listName = [collection.get("name") for collection in collections.values()]
    if setName is None:
        if len(collections) != 1:
            raise ThemeError(f"--set is required: collections are {listName}")
        collectionId = next(iter(collections))
    else:
        listId = [key for key, collection in collections.items() if collection.get("name") == setName]
        if not listId:
            raise ThemeError(f"collection '{setName}' not found: collections are {listName}")
        collectionId = listId[0]
    collection = collections[collectionId]
    listMode = collection.get("modes", [])
    if modeName is None:
        modeId = collection.get("defaultModeId") or listMode[0]["modeId"]
        modeName = next((mode.get("name") for mode in listMode if mode.get("modeId") == modeId), None)
    else:
        listModeId = [mode["modeId"] for mode in listMode if mode.get("name") == modeName]
        if not listModeId:
            raise ThemeError(f"mode '{modeName}' not found: modes are {[mode.get('name') for mode in listMode]}")
        modeId = listModeId[0]
    variables["__collections__"] = collections
    mapFlat: dict[str, object] = {}
    for variable in variables.values():
        if not isinstance(variable, dict) or variable.get("variableCollectionId") != collectionId:
            continue
        name = str(variable.get("name", ""))
        value = figmaValueInternal(variables, variable, modeName, modeId, 0, listWarning)
        resolvedType = variable.get("resolvedType")
        if resolvedType == "COLOR":
            mapFlat[name] = parseColor(value) if value is not None else None
        elif resolvedType == "FLOAT":
            mapFlat[name] = value if isinstance(value, (int, float)) and not isinstance(value, bool) else None
        else:
            mapFlat[name] = None
            listWarning.append(f"variable '{name}' has type {resolvedType} (only COLOR and FLOAT are read)")
    return mapFlat


def flattenDtcgInternal(node: dict, path: list[str], inheritedType: str | None, mapLeaf: dict[str, tuple[object, str | None]]) -> None:
    nodeType = node.get("$type", inheritedType)
    if "$value" in node:
        mapLeaf[".".join(path)] = (node["$value"], nodeType)
        return
    for key, child in node.items():
        if not key.startswith("$") and isinstance(child, dict):
            flattenDtcgInternal(child, [*path, key], nodeType, mapLeaf)


def extractDtcg(data: dict, setName: str | None, listWarning: list[str]) -> dict[str, object]:
    """DTCG JSON → {변수 이름: 값}. 최상위가 세트(알려진 그룹 이름이 아닌 키)이면 --set 으로 고른다."""
    knownTop = {group.lower() for group in kSchema} | {key.lower() for key in kScalarKey}
    listTop = [key for key, child in data.items() if not key.startswith("$") and isinstance(child, dict)]
    bSetLevel = bool(listTop) and not any(key.lower() in knownTop for key in listTop)
    root = data
    if bSetLevel:
        if setName is None:
            if len(listTop) != 1:
                raise ThemeError(f"--set is required: sets are {listTop}")
            setName = listTop[0]
        if setName not in data or not isinstance(data[setName], dict):
            raise ThemeError(f"set '{setName}' not found: sets are {listTop}")
        root = data[setName]
    elif setName is not None:
        listWarning.append("--set ignored: the file has no set level")
    mapLeaf: dict[str, tuple[object, str | None]] = {}
    flattenDtcgInternal(root, [], None, mapLeaf)
    mapFlat: dict[str, object] = {}
    for name, (value, nodeType) in mapLeaf.items():
        depth = 0
        while isinstance(value, str) and _kAliasPattern.match(value) and depth < _kAliasDepth:
            aliasPath = _kAliasPattern.match(value).group(1)  # type: ignore[union-attr]
            if aliasPath not in mapLeaf:
                listWarning.append(f"alias of '{name}' points at unknown '{aliasPath}'")
                value = None
                break
            value, aliasType = mapLeaf[aliasPath]
            nodeType = nodeType or aliasType
            depth += 1
        else:
            if depth >= _kAliasDepth:
                listWarning.append(f"alias of '{name}' is a cycle")
                value = None
        if value is None:
            mapFlat[name] = None
        elif nodeType == "color" or parseColor(value) is not None:
            mapFlat[name] = parseColor(value)
        else:
            mapFlat[name] = parseNumber(value)
    return mapFlat


def extractVariables(data: dict, setName: str | None, modeName: str | None, listWarning: list[str]) -> dict[str, object]:
    """입력 형식(Figma REST 또는 DTCG)을 알아보고 평평한 표로 만든다. DTCG 에서는 --mode 도 세트 이름으로 읽는다."""
    meta = data.get("meta")
    if isinstance(meta, dict) and "variables" in meta or "variableCollections" in data:
        return extractFigmaRest(data, setName, modeName, listWarning)
    return extractDtcg(data, setName if setName is not None else modeName, listWarning)


# ---------------------------------------------------------------------------------------------------------------------
# 토큰에 덮어쓰기 · 검사
# ---------------------------------------------------------------------------------------------------------------------

def normalizeNumber(key: str, value: float) -> int | float:
    rounded = round(value, 4)
    if key in kFloatKey:
        return float(rounded)
    return int(rounded) if rounded == int(rounded) else rounded


def applyVariables(base: dict, mapFlat: dict[str, object], listWarning: list[str]) -> list[str]:
    """평평한 표를 `base` 에 덮어쓴다. 덮어쓴 키(`group.key`)의 목록을 돌려주고, 못 쓴 변수는 경고에 더한다."""
    listApplied: list[str] = []
    for name, value in mapFlat.items():
        target = mapVariableName(name)
        if target is None:
            listWarning.append(f"unknown variable '{name}' dropped (not in the token table)")
            continue
        group, key = target
        bColor = group == "color"
        if value is None or (bColor and not isinstance(value, str)) or (not bColor and isinstance(value, str)):
            listWarning.append(f"variable '{name}' dropped: value is missing or has the wrong type for {group or key}")
            continue
        if group == "":
            base[f"_{key}"] = normalizeNumber(key, float(value))  # type: ignore[arg-type]
        else:
            base[f"_{group}"][f"_{key}"] = value if bColor else normalizeNumber(key, float(value))  # type: ignore[arg-type]
        listApplied.append(f"{group}.{key}" if group else key)
    return listApplied


def listExpectedKey() -> set[str]:
    """정본 표의 키 집합 — `group.key` 와 그룹 없는 키."""
    setKey = {f"{group}.{key}" for group, listKey in kSchema.items() for key in listKey}
    return setKey | set(kScalarKey)


def listActualKey(data: dict) -> set[str]:
    setKey: set[str] = set()
    for key, value in data.items():
        if key in ("_name", "_displayName"):
            continue
        if isinstance(value, dict):
            setKey |= {f"{key[1:]}.{inner[1:]}" for inner in value}
        else:
            setKey.add(key[1:])
    return setKey


def checkKeySet(data: dict) -> list[str]:
    """키 집합이 정본 표(= 다크 세트)와 같은지. 어긋남을 문장 목록으로."""
    expected = listExpectedKey()
    actual = listActualKey(data)
    listError = [f"missing key: {key}" for key in sorted(expected - actual)]
    listError += [f"unknown key: {key}" for key in sorted(actual - expected)]
    for key in ("_name", "_displayName"):
        if not isinstance(data.get(key), str) or not data.get(key):
            listError.append(f"{key} must be a non-empty string")
    return listError


def relativeLuminance(color: str, backdrop: str | None = None) -> float:
    """WCAG 상대 휘도. 알파가 있으면 `backdrop` 색 위에 올린 값."""
    def channels(text: str) -> tuple[float, float, float, float]:
        return (int(text[1:3], 16) / 255, int(text[3:5], 16) / 255, int(text[5:7], 16) / 255,
                int(text[7:9], 16) / 255 if len(text) == 9 else 1.0)
    red, green, blue, alpha = channels(color)
    if alpha < 1.0 and backdrop is not None:
        back = channels(backdrop)
        red, green, blue = (value * alpha + other * (1 - alpha) for value, other in zip((red, green, blue), back[:3]))

    def linear(value: float) -> float:
        return value / 12.92 if value <= 0.03928 else ((value + 0.055) / 1.055) ** 2.4
    return 0.2126 * linear(red) + 0.7152 * linear(green) + 0.0722 * linear(blue)


def contrastRatio(foreground: str, background: str) -> float:
    """WCAG 대비비(1..21). 전경의 알파는 배경 위에 올려 계산한다."""
    first = relativeLuminance(foreground, background)
    second = relativeLuminance(background)
    high, low = max(first, second), min(first, second)
    return (high + 0.05) / (low + 0.05)


def checkTokens(data: dict, fileStem: str) -> tuple[list[str], list[str]]:
    """tokens 오브젝트 하나 → (오류, 경고). 오류 = 키 집합 · 색 형식 · 수 범위, 경고 = 대비비 · 이름."""
    listError = checkKeySet(data)
    listWarning: list[str] = []
    if data.get("_name") != fileStem:
        listWarning.append(f"_name '{data.get('_name')}' differs from the file name '{fileStem}'")
    for group, listKey in kSchema.items():
        section = data.get(f"_{group}")
        if not isinstance(section, dict):
            continue
        for key in listKey:
            value = section.get(f"_{key}")
            if value is None:
                continue
            if group == "color":
                if not isinstance(value, str) or not kColorPattern.match(value):
                    listError.append(f"color {group}.{key}={value!r}: use upper-case #RRGGBB or #RRGGBBAA")
                continue
            low, high = kRange.get(key, kRange.get(group, (0, 1e9)))
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high:
                listError.append(f"{group}.{key}={value!r}: expected a number in [{low}, {high}]")
    density = data.get("_density")
    low, high = kRange["density"]
    if "_density" in data and (isinstance(density, bool) or not isinstance(density, (int, float)) or not low <= density <= high):
        listError.append(f"density={density!r}: expected a number in [{low}, {high}]")
    mapColor = data.get("_color")
    if isinstance(mapColor, dict) and not [error for error in listError if error.startswith("color ")]:
        threshold = kContrastHigh if data.get("_name") == kHighContrastName or fileStem == kHighContrastName else kContrastNormal
        for foreground, background in kContrastPair:
            first, second = mapColor.get(f"_{foreground}"), mapColor.get(f"_{background}")
            if isinstance(first, str) and isinstance(second, str):
                ratio = contrastRatio(first, second)
                if ratio < threshold:
                    listWarning.append(f"contrast {foreground} on {background} is {ratio:.2f}:1 (< {threshold}:1)")
    return listError, listWarning


# ---------------------------------------------------------------------------------------------------------------------
# DTCG 쓰기
# ---------------------------------------------------------------------------------------------------------------------

def toDtcg(listData: list[dict]) -> dict:
    """tokens 오브젝트들 → DTCG JSON(최상위 키 = 세트 = Figma 모드)."""
    result: dict = {}
    for data in listData:
        section: dict = {"$extensions": {"sw.editor": {"name": data["_name"], "displayName": data["_displayName"]}}}
        for group in kSchema:
            section[group] = {}
            for innerKey, value in data[f"_{group}"].items():
                bColor = group == "color"
                section[group][innerKey[1:]] = {"$type": "color" if bColor else "number", "$value": value}
        for key in kScalarKey:
            section[key] = {"$type": "number", "$value": data[f"_{key}"]}
        result[data["_name"]] = section
    return result


def fromDtcgSetInternal(setData: dict, base: dict, listWarning: list[str]) -> dict:
    """DTCG 세트 하나 → tokens 오브젝트(다크 바탕 위에)."""
    result = json.loads(json.dumps(base))
    mapFlat = extractDtcg(setData, None, listWarning)
    applyVariables(result, mapFlat, listWarning)
    extension = setData.get("$extensions", {}).get("sw.editor", {}) if isinstance(setData.get("$extensions"), dict) else {}
    if isinstance(extension.get("name"), str):
        result["_name"] = extension["name"]
    if isinstance(extension.get("displayName"), str):
        result["_displayName"] = extension["displayName"]
    return result


# ---------------------------------------------------------------------------------------------------------------------
# 명령
# ---------------------------------------------------------------------------------------------------------------------

def resolvePathInternal(root: Path, text: str) -> Path:
    path = Path(text)
    return path if path.is_absolute() else root / path


def dtcgDisplayNameInternal(data: dict, setName: str | None) -> str | None:
    """DTCG 세트의 `$extensions.sw.editor.displayName`(export-figma 가 쓴다). 없으면 None."""
    section = data.get(setName) if setName in data else (data if len(data) == 1 else None)
    if setName is None and len(data) == 1:
        section = next(iter(data.values()))
    extension = section.get("$extensions", {}).get("sw.editor", {}) if isinstance(section, dict) and isinstance(section.get("$extensions"), dict) else {}
    value = extension.get("displayName")
    return value if isinstance(value, str) else None


def runImport(args: argparse.Namespace, root: Path) -> int:
    inputPath = resolvePathInternal(root, args.input)
    listWarning: list[str] = []
    rawData = readJson(inputPath)
    mapFlat = extractVariables(rawData, args.set, args.mode, listWarning)
    if args.out:
        outPath = resolvePathInternal(root, args.out)
    elif args.name:
        outPath = themePathInternal(root, args.name)
    else:
        raise ThemeError("--name or --out is required")
    name = args.name or outPath.name.removesuffix(".tokens.json")
    basePath = themePathInternal(root, kBaseThemeName)
    bExisting = outPath.exists()
    base = readJson(outPath if bExisting else basePath)
    if not bExisting:
        base["_name"] = name
        base["_displayName"] = args.display_name or dtcgDisplayNameInternal(rawData, args.set if args.set is not None else args.mode) or name
    elif args.display_name:
        base["_displayName"] = args.display_name
    listApplied = applyVariables(base, mapFlat, listWarning)
    setApplied = set(listApplied)
    if not bExisting:
        listKept = sorted(listExpectedKey() - setApplied)
        if listKept:
            listWarning.append(f"{len(listKept)} keys not in the input were filled from the {kBaseThemeName} set: {', '.join(listKept)}")
    listError = checkKeySet(base)
    for message in listWarning:
        print(f"[editor-theme] warning: {message}")
    if listError:
        for message in listError:
            print(f"[editor-theme] error: {message}")
        return 1
    writeTokens(outPath, base)
    print(f"[editor-theme] {len(listApplied)} values applied, {len(listWarning)} warnings -> {outPath}")
    return 0


def runExport(args: argparse.Namespace, root: Path) -> int:
    listData = [readJson(resolvePathInternal(root, item)) for item in args.tokens]
    listError = [f"{item}: {message}" for item, data in zip(args.tokens, listData) for message in checkKeySet(data)]
    if listError:
        for message in listError:
            print(f"[editor-theme] error: {message}")
        return 1
    outPath = resolvePathInternal(root, args.out)
    outPath.parent.mkdir(parents=True, exist_ok=True)
    with open(outPath, "w", encoding="utf-8", newline="") as file:
        file.write(json.dumps(toDtcg(listData), indent=2, ensure_ascii=False) + "\n")
    print(f"[editor-theme] {len(listData)} sets -> {outPath}")
    return 0


def runCheck(args: argparse.Namespace, root: Path) -> int:
    errorTotal = 0
    warningTotal = 0
    for item in args.tokens:
        path = resolvePathInternal(root, item)
        listError, listWarning = checkTokens(readJson(path), path.name.removesuffix(".tokens.json"))
        for message in listError:
            print(f"{item}: error: {message}")
        for message in listWarning:
            print(f"{item}: warning: {message}")
        errorTotal += len(listError)
        warningTotal += len(listWarning)
    print(f"[editor-theme] {len(args.tokens)} files, {errorTotal} errors, {warningTotal} warnings")
    return 1 if errorTotal or (args.fail_on_warning and warningTotal) else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="에디터 테마 토큰(tokens.json)을 Figma 변수 · DTCG 와 주고받고 검사한다.")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    sub = parser.add_subparsers(dest="command", required=True)

    importer = sub.add_parser("import-figma", description="Figma 변수 JSON(REST 응답 또는 DTCG)을 tokens.json 으로.")
    importer.add_argument("input", help="Figma REST 응답 또는 DTCG JSON")
    importer.add_argument("--set", default=None, help="Figma 컬렉션 이름 · DTCG 최상위 세트 이름(하나뿐이면 생략)")
    importer.add_argument("--mode", default=None, help="Figma 모드 이름(기본: 컬렉션의 기본 모드). DTCG 에서는 --set 의 다른 이름")
    importer.add_argument("--name", default=None, help="테마 이름(_name). 기본: --out 의 파일 이름")
    importer.add_argument("--display-name", default=None, help="_displayName(새 파일이면 기본은 이름)")
    importer.add_argument("--out", default=None, help=f"출력(기본 {kThemeFolder}/<이름>.tokens.json)")
    importer.set_defaults(handler=runImport)

    exporter = sub.add_parser("export-figma", description="tokens.json 들을 DTCG JSON 으로(세트 = 모드).")
    exporter.add_argument("tokens", nargs="+", help="tokens.json 파일들")
    exporter.add_argument("--out", required=True, help="출력 파일")
    exporter.set_defaults(handler=runExport)

    checker = sub.add_parser("check", description="tokens.json 의 키 집합 · 색 형식 · 수 범위 · 대비비를 검사한다.")
    checker.add_argument("tokens", nargs="+", help="tokens.json 파일들")
    checker.add_argument("--fail-on-warning", action="store_true", help="경고(대비비 등)도 실패로 센다")
    checker.set_defaults(handler=runCheck)

    args = parser.parse_args(argv)
    root = (args.root or getProjectRoot()).resolve()
    try:
        return args.handler(args, root)
    except ThemeError as error:
        print(f"[editor-theme] error: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
