#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev.MakeEditorTheme — Figma REST · DTCG 읽기, 별칭, 모르는 변수, 왕복(export → import), 검사(키 집합 · 색 · 대비비)."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from dev import MakeEditorTheme as theme  # noqa: E402

_kRoot = Path(__file__).resolve().parents[2]


def loadThemeInternal(name: str) -> dict:
    return theme.readJson(_kRoot / "Resource/editor/theme" / f"{name}.tokens.json")


def figmaResponseInternal() -> dict:
    """컬렉션 하나(모드 Dark · Light), 별칭 하나, 모르는 변수 둘(이름 · 타입)."""
    def variable(name: str, kind: str, dark: object, light: object) -> dict:
        return {"name": name, "variableCollectionId": "C1", "resolvedType": kind, "valuesByMode": {"M1": dark, "M2": light}}
    return {"meta": {
        "variableCollections": {"C1": {"name": "Editor", "defaultModeId": "M1", "modes": [{"modeId": "M1", "name": "Dark"}, {"modeId": "M2", "name": "Light"}]}},
        "variables": {
            "V1": variable("color/accent", "COLOR", {"r": 1.0, "g": 0.0, "b": 0.0, "a": 1.0}, {"r": 0.0, "g": 0.0, "b": 1.0, "a": 1.0}),
            "V2": variable("color/text", "COLOR", {"type": "VARIABLE_ALIAS", "id": "V1"}, {"r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0}),
            "V3": variable("radius/window", "FLOAT", 9, 2),
            "V4": variable("spacing/windowPaddingX", "FLOAT", 12.5, 3),
            "V5": variable("color/scrollbarBg", "COLOR", {"r": 0.0, "g": 0.0, "b": 0.0, "a": 0.5}, {"r": 0.0, "g": 0.0, "b": 0.0, "a": 0.5}),
            "V6": variable("density", "FLOAT", 1.25, 1.0),
            "V7": variable("brand/logo", "COLOR", {"r": 0.5, "g": 0.5, "b": 0.5, "a": 1.0}, {"r": 0.5, "g": 0.5, "b": 0.5, "a": 1.0}),
            "V8": variable("color/name", "STRING", "x", "y"),
        },
    }}


def dtcgInternal() -> dict:
    return {"color": {"$type": "color", "accent": {"$value": "#112233"}, "text": {"$value": "{color.accent}"},
                      "border": {"$value": {"colorSpace": "srgb", "components": [1, 0, 0], "alpha": 1}}},
            "radius": {"$type": "number", "window": {"$value": "7px"}},
            "stroke": {"frame": {"$type": "number", "$value": {"value": 2, "unit": "px"}}},
            "mystery": {"$type": "number", "x": {"$value": 1}}}


def applyInternal(flat: dict) -> tuple[dict, list[str]]:
    base = loadThemeInternal("dark")
    listWarning: list[str] = []
    theme.applyVariables(base, flat, listWarning)
    return base, listWarning


class EditorThemeTest(unittest.TestCase):
    def testShippedThemesPassCheck(self) -> None:
        for name in ("dark", "charcoal", "midnight", "light", "highcontrast"):
            listError, listWarning = theme.checkTokens(loadThemeInternal(name), name)
            self.assertEqual((listError, listWarning), ([], []), name)

    def testFigmaRestDefaultMode(self) -> None:
        listWarning: list[str] = []
        flat = theme.extractVariables(figmaResponseInternal(), None, None, listWarning)
        base, listApplyWarning = applyInternal(flat)
        self.assertEqual(base["_color"]["_accent"], "#FF0000")
        self.assertEqual(base["_color"]["_text"], "#FF0000")          # 별칭을 따라간다
        self.assertEqual(base["_color"]["_scrollbarBg"], "#00000080")  # 알파
        self.assertEqual(base["_radius"]["_window"], 9)
        self.assertIsInstance(base["_radius"]["_window"], int)
        self.assertEqual(base["_spacing"]["_windowPaddingX"], 12.5)
        self.assertEqual(base["_density"], 1.25)
        self.assertTrue(any("brand/logo" in message for message in listApplyWarning))   # 모르는 변수는 경고
        self.assertTrue(any("STRING" in message for message in listWarning))

    def testFigmaRestNamedMode(self) -> None:
        flat = theme.extractVariables(figmaResponseInternal(), "Editor", "Light", [])
        base, _ = applyInternal(flat)
        self.assertEqual(base["_color"]["_accent"], "#0000FF")
        self.assertEqual(base["_color"]["_text"], "#000000")
        self.assertEqual(base["_radius"]["_window"], 2)

    def testFigmaRestBadSelection(self) -> None:
        with self.assertRaises(theme.ThemeError):
            theme.extractVariables(figmaResponseInternal(), "Nope", None, [])
        with self.assertRaises(theme.ThemeError):
            theme.extractVariables(figmaResponseInternal(), None, "Nope", [])

    def testDtcgAliasObjectColorAndUnits(self) -> None:
        listWarning: list[str] = []
        flat = theme.extractVariables(dtcgInternal(), None, None, listWarning)
        base, listApplyWarning = applyInternal(flat)
        self.assertEqual(base["_color"]["_accent"], "#112233")
        self.assertEqual(base["_color"]["_text"], "#112233")
        self.assertEqual(base["_color"]["_border"], "#FF0000")
        self.assertEqual(base["_radius"]["_window"], 7)
        self.assertEqual(base["_stroke"]["_frame"], 2)
        self.assertTrue(any("mystery.x" in message for message in listApplyWarning))

    def testDtcgBrokenAliasIsWarnedNotApplied(self) -> None:
        listWarning: list[str] = []
        flat = theme.extractVariables({"color": {"$type": "color", "accent": {"$value": "{color.nope}"}}}, None, None, listWarning)
        base, listApplyWarning = applyInternal(flat)
        self.assertEqual(base["_color"]["_accent"], loadThemeInternal("dark")["_color"]["_accent"])
        self.assertTrue(listWarning)
        self.assertTrue(listApplyWarning)

    def testNameMappingAcceptsSlashAndDot(self) -> None:
        self.assertEqual(theme.mapVariableName("color/accent"), ("color", "accent"))
        self.assertEqual(theme.mapVariableName("Color.WindowBg"), ("color", "windowBg"))
        self.assertEqual(theme.mapVariableName("density"), ("", "density"))
        self.assertIsNone(theme.mapVariableName("color/nope"))
        self.assertIsNone(theme.mapVariableName("a/b/c"))

    def testRoundTripExportThenImport(self) -> None:
        listName = ("dark", "light", "highcontrast")
        listData = [loadThemeInternal(name) for name in listName]
        dtcg = theme.toDtcg(listData)
        self.assertEqual(list(dtcg), list(listName))
        for name, data in zip(listName, listData):
            self.assertEqual(theme.fromDtcgSetInternal(dtcg[name], loadThemeInternal("dark"), []), data, name)

    def testCommandsRoundTripThroughFiles(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / "Resource/editor/theme"
            folder.mkdir(parents=True)
            for name in ("dark", "light"):
                (folder / f"{name}.tokens.json").write_bytes((_kRoot / "Resource/editor/theme" / f"{name}.tokens.json").read_bytes())
            outFile = root / "out/figma.json"
            self.assertEqual(theme.main(["--root", str(root), "export-figma", "Resource/editor/theme/dark.tokens.json",
                                         "Resource/editor/theme/light.tokens.json", "--out", str(outFile)]), 0)
            self.assertEqual(theme.main(["--root", str(root), "import-figma", str(outFile), "--set", "light", "--name", "copy"]), 0)
            copy = (folder / "copy.tokens.json").read_bytes()
            expected = (folder / "light.tokens.json").read_bytes().replace(b'"_name": "light"', b'"_name": "copy"')
            self.assertEqual(copy, expected)          # CRLF · 들여쓰기 · 대문자 16진까지 바이트로 같다
            self.assertIn(b"\r\n", copy)

    def testImportOverExistingKeepsUnlistedKeys(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / "Resource/editor/theme"
            folder.mkdir(parents=True)
            source = (_kRoot / "Resource/editor/theme/dark.tokens.json").read_bytes()
            (folder / "dark.tokens.json").write_bytes(source)
            inputFile = root / "in.json"
            inputFile.write_text(json.dumps({"color": {"accent": {"$type": "color", "$value": "#010203"}}}), encoding="utf-8")
            self.assertEqual(theme.main(["--root", str(root), "import-figma", str(inputFile), "--name", "dark"]), 0)
            after = theme.readJson(folder / "dark.tokens.json")
            before = json.loads(source)
            self.assertEqual(after["_color"]["_accent"], "#010203")
            after["_color"]["_accent"] = before["_color"]["_accent"]
            self.assertEqual(after, before)

    def testCheckFindsMissingKeyBadColorRangeAndContrast(self) -> None:
        data = loadThemeInternal("dark")
        del data["_radius"]["_grab"]
        data["_color"]["_accent"] = "#abc"
        data["_spacing"]["_indent"] = -1
        data["_color"]["_text"] = "#222831"       # 창 배경과 거의 같다
        listError, listWarning = theme.checkTokens(data, "dark")
        self.assertTrue(any("radius.grab" in message for message in listError))
        self.assertTrue(any("accent" in message for message in listError))
        self.assertTrue(any("indent" in message for message in listError))
        self.assertEqual(listWarning, [])         # 색 오류가 있으면 대비비는 건너뛴다
        data["_color"]["_accent"] = "#ABCDEF"
        data["_radius"]["_grab"] = 3
        data["_spacing"]["_indent"] = 20
        listError, listWarning = theme.checkTokens(data, "dark")
        self.assertEqual(listError, [])
        self.assertEqual(len([message for message in listWarning if message.startswith("contrast")]), 3)

    def testContrastRatioKnownValues(self) -> None:
        self.assertAlmostEqual(theme.contrastRatio("#000000", "#FFFFFF"), 21.0, places=2)
        self.assertAlmostEqual(theme.contrastRatio("#777777", "#FFFFFF"), 4.48, places=2)

    def testHighContrastThresholdIsSeven(self) -> None:
        data = loadThemeInternal("highcontrast")
        data["_color"]["_text"] = "#888888"        # 검정 위 약 5.9:1
        _, listWarning = theme.checkTokens(data, "highcontrast")
        self.assertTrue(any("7.0:1" in message for message in listWarning))


if __name__ == "__main__":
    unittest.main()
