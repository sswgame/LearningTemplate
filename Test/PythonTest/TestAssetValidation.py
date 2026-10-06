#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
에셋 검증 규칙(`Scripts/common/AssetValidation.py`)의 단위 시험.

검사마다 **잡아야 하는 조각**과 **잡으면 안 되는 조각**을 둘 다 둔다 — 잡지 못하는 검사는 아무것도 지키지 않고, 지나치게 잡는 검사는
아무도 안 돌리게 된다. 마지막으로 저장소의 규칙 표가 읽히고 `Resource/` 에 오류가 없는지 본다(게이트와 같은 판정).
"""

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.AssetValidation import (RuleConfigError, ValidationContext, isShaderWordUsedInternal, loadRules,  # noqa: E402
                                    parseRules, readDdsInfo, readMeshInfo, validate)
from common.AssetValidation import _kReferenceTokenRe  # noqa: E402


def makeDdsInternal(width: int, height: int, mipCount: int, dxgiFormat: int) -> bytes:
    header = bytearray(148)
    header[0:4] = b"DDS "
    struct.pack_into("<I", header, 4, 124)
    struct.pack_into("<II", header, 12, height, width)
    struct.pack_into("<I", header, 28, mipCount)
    struct.pack_into("<I", header, 80, 0x4)
    header[84:88] = b"DX10"
    struct.pack_into("<I", header, 128, dxgiFormat)
    return bytes(header)


def makeMeshInternal(triangleCount: int, radius: float = 1.0) -> bytes:
    vertexCount = triangleCount * 3
    return b"SWMS" + struct.pack("<IIIfI", 2, vertexCount, 48, radius, 0) + bytes(vertexCount * 48)


class AssetValidationFixture(unittest.TestCase):
    """임시 저장소 하나(Resource/ · Source/)에 파일을 깔고 규칙 하나를 돌린다."""

    def setUp(self) -> None:
        self._tempDir = tempfile.TemporaryDirectory()
        self.root = Path(self._tempDir.name)
        (self.root / "Resource").mkdir()
        (self.root / "Source").mkdir()

    def tearDown(self) -> None:
        self._tempDir.cleanup()

    def put(self, relPath: str, content: bytes | str) -> None:
        path = self.root / relPath
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, str):
            path.write_text(content, encoding="utf-8")
        else:
            path.write_bytes(content)

    def run(self, result=None):  # noqa: D401 — unittest 의 이름
        return super().run(result)

    def findMessages(self, ruleEntry: dict, listTarget: list[str] | None = None) -> list[str]:
        listRule = parseRules({"rules": [ruleEntry]})
        return [finding.message for finding in validate(listRule, self.root / "Resource", self.root, listTarget)]


class RuleTableTest(unittest.TestCase):
    def testUnknownCheckIsAnError(self) -> None:
        with self.assertRaises(RuleConfigError):
            parseRules({"rules": [{"name": "x", "check": "no_such_check"}]})

    def testUnknownKeyIsAnError(self) -> None:
        with self.assertRaises(RuleConfigError):
            parseRules({"rules": [{"name": "x", "check": "texture", "max_sise": 4}]})

    def testRepeatedNameIsAnError(self) -> None:
        with self.assertRaises(RuleConfigError):
            parseRules({"rules": [{"name": "x", "check": "orphan"}, {"name": "x", "check": "orphan"}]})

    def testUnknownSeverityIsAnError(self) -> None:
        with self.assertRaises(RuleConfigError):
            parseRules({"rules": [{"name": "x", "check": "orphan", "severity": "fatal"}]})


class HeaderReaderTest(unittest.TestCase):
    def testDdsHeader(self) -> None:
        info = readDdsInfo(makeDdsInternal(256, 128, 9, 98))
        self.assertEqual((256, 128, 9, "BC7_UNORM"), (info.width, info.height, info.mipCount, info.formatName))

    def testTruncatedDdsIsRejected(self) -> None:
        with self.assertRaises(ValueError):
            readDdsInfo(b"DDS " + bytes(20))

    def testMeshHeader(self) -> None:
        self.assertEqual(12, readMeshInfo(makeMeshInternal(12)).triangleCount)

    def testSkinnedMeshCountsItsSkinStream(self) -> None:
        # 판 2 의 스킨 메시는 정점마다 스킨 칸(24 바이트)이 뒤에 붙는다 — 크기 검사가 그것을 센다.
        skinned = b"SWMS" + struct.pack("<IIIfI", 2, 6, 48, 1.0, 4) + bytes(6 * 48) + bytes(6 * 24)
        self.assertEqual(2, readMeshInfo(skinned).triangleCount)
        with self.assertRaises(ValueError):
            readMeshInfo(skinned[:-24])

    def testMeshMorphChunkIsCounted(self) -> None:
        # 정점 뒤 선택 덩어리(MRPH)는 태그 · 길이로 건너뛴다 — 길이가 파일을 넘거나 모르는 태그면 오류.
        base = b"SWMS" + struct.pack("<IIIfI", 2, 3, 48, 1.0, 0) + bytes(3 * 48)
        self.assertEqual(1, readMeshInfo(base + b"MRPH" + struct.pack("<I", 4) + bytes(4)).triangleCount)
        with self.assertRaises(ValueError):
            readMeshInfo(base + b"MRPH" + struct.pack("<I", 8) + bytes(4))
        with self.assertRaises(ValueError):
            readMeshInfo(base + b"XXXX" + struct.pack("<I", 0))

    def testOldMeshVersionIsRejected(self) -> None:
        with self.assertRaises(ValueError):
            readMeshInfo(b"SWMS" + struct.pack("<IIIfI", 1, 3, 48, 1.0, 0) + bytes(3 * 48))

    def testMeshWithWrongSizeIsRejected(self) -> None:
        with self.assertRaises(ValueError):
            readMeshInfo(makeMeshInternal(2)[:-4])


class CheckTest(AssetValidationFixture):
    def testNamingRejectsSpacesAndWrongFolder(self) -> None:
        self.put("Resource/game/p/prefabs/Bad Name.xml", "<a/>")
        self.put("Resource/game/p/prefabs/good.prefab.xml", "<a/>")
        messages = self.findMessages({"name": "n", "check": "naming", "include_patterns": ["game/*/prefabs/*"],
                                      "name_pattern": "[a-z0-9_.\\-]+", "allowed_suffixes": [".prefab.xml"]})
        self.assertEqual(2, len(messages), messages)

    def testTextureLimits(self) -> None:
        self.put("Resource/game/p/textures/big.dds", makeDdsInternal(8192, 8192, 14, 98))
        self.put("Resource/game/p/textures/npot.dds", makeDdsInternal(300, 256, 1, 98))
        self.put("Resource/game/p/textures/odd.dds", makeDdsInternal(256, 256, 9, 2))
        self.put("Resource/game/p/textures/fine.dds", makeDdsInternal(256, 256, 9, 98))
        messages = self.findMessages({"name": "t", "check": "texture", "include_patterns": ["*.dds"], "max_size": 4096,
                                      "power_of_two": True, "allowed_formats": ["BC7_UNORM"]})
        self.assertEqual(3, len(messages), messages)

    def testTextureMipPolicy(self) -> None:
        self.put("Resource/a/partial.dds", makeDdsInternal(256, 256, 3, 98))
        self.put("Resource/a/full.dds", makeDdsInternal(256, 256, 9, 98))
        self.assertEqual(1, len(self.findMessages({"name": "m", "check": "texture", "include_patterns": ["*.dds"], "require_mips": True})))
        self.assertEqual(2, len(self.findMessages({"name": "m", "check": "texture", "include_patterns": ["*.dds"], "require_mips": False})))

    def testMeshBudget(self) -> None:
        self.put("Resource/game/p/models/heavy.mesh", makeMeshInternal(50))
        self.put("Resource/game/p/models/light.mesh", makeMeshInternal(5))
        messages = self.findMessages({"name": "b", "check": "mesh_budget", "include_patterns": ["*.mesh"], "max_triangles": 10})
        self.assertEqual(["50 triangles exceed the budget of 10"], messages)

    def testMissingReference(self) -> None:
        self.put("Resource/game/p/models/here.mesh", makeMeshInternal(1))
        self.put("Resource/game/p/maps/a.scene.xml",
                 '<Scene><x _meshId="game/p/models/here.mesh" _other="game/p/models/gone.mesh" _builtin="Cube"/></Scene>')
        messages = self.findMessages({"name": "r", "check": "missing_reference", "include_patterns": ["*.xml"]})
        self.assertEqual(1, len(messages), messages)
        self.assertIn("gone.mesh", messages[0])

    def testOrphan(self) -> None:
        self.put("Resource/game/p/models/used.mesh", makeMeshInternal(1))
        self.put("Resource/game/p/models/unused.mesh", makeMeshInternal(1))
        self.put("Resource/game/p/maps/a.scene.xml", '<Scene><x _meshId="game/p/models/used.mesh"/></Scene>')
        self.put("Source/Probe.cpp", 'const char* kName = "byname.mesh";')
        self.put("Resource/game/p/models/byname.mesh", makeMeshInternal(1))
        findings = self.findMessages({"name": "o", "check": "orphan", "include_patterns": ["*.mesh"]})
        self.assertEqual(1, len(findings), findings)

    def testMaterial(self) -> None:
        self.put("Resource/engine/shaders/lit.hlsl", "#if USE_FOG\n#endif\n")
        self.put("Resource/a/good.material",
                 '<MaterialDesc shaderPath="engine/shaders/lit.hlsl" blendMode="Opaque"><_properties>'
                 '<item name="fog" type="Keyword" shaderKeyword="USE_FOG"/></_properties><_permutations><_staticSwitches>'
                 '<item name="Fog" keyword="USE_FOG"/></_staticSwitches><_multiCompiles><item name="M" selected="A"><_options><item>A</item>'
                 '</_options></item></_multiCompiles></_permutations></MaterialDesc>')
        self.put("Resource/a/bad.material",
                 '<MaterialDesc shaderPath="engine/shaders/lit.hlsl" blendMode="Glow"><_properties>'
                 '<item name="k" type="Keyword" shaderKeyword="ORPHAN_KEYWORD"/></_properties><_permutations><_staticSwitches>'
                 '<item name="Fog" keyword="USE_FOG"/><item name="Fog" keyword="USE_FOG"/></_staticSwitches><_multiCompiles>'
                 '<item name="M" selected="Z"><_options><item>A</item></_options></item></_multiCompiles></_permutations></MaterialDesc>')
        rule = {"name": "m", "check": "material", "include_patterns": ["*.material"], "allowed_blend_modes": ["Opaque"],
                "require_permutations": True}
        self.assertEqual([], self.findMessages(rule, ["a/good.material"]))
        self.assertEqual(4, len(self.findMessages(rule, ["a/bad.material"])))

    def testMaterialKeywordMustReachAShader(self) -> None:
        self.put("Resource/engine/shaders/lit.hlsl", "#if USE_FOG\n#endif\n")
        self.put("Resource/a/x.material", '<MaterialDesc shaderPath="engine/shaders/lit.hlsl"><_permutations><_staticSwitches>'
                 '<item name="Fog" keyword="USE_FOG"/><item name="Dead" keyword="NOBODY_READS"/></_staticSwitches></_permutations></MaterialDesc>')
        messages = self.findMessages({"name": "k", "check": "material_keywords", "include_patterns": ["*.material"]})
        self.assertEqual(1, len(messages), messages)
        self.assertIn("NOBODY_READS", messages[0])

    def testReferenceTokenStartsAtChunkStart(self) -> None:
        # 덩어리 첫 글자에서만 시작해도 토큰은 같다 — 한글 뒤의 `d.json` 은 한글이 덩어리 글자가 아니라 덩어리가 `d` 에서 시작한다.
        text = "see engine/models/a.mesh and models/b.material.bak, xmodels/c.prefab.xml 한글d.json"
        self.assertEqual(["engine/models/a.mesh", "models/b.material", "xmodels/c.prefab.xml", "d.json"], _kReferenceTokenRe.findall(text))

    def testShaderKeywordUsesWholeWord(self) -> None:
        context = ValidationContext(resourceRoot=Path("."), repositoryRoot=Path("."), listAllPath=[])
        context._shaderText = "#if USE_FOG\n#endif\nUSE_FOGGY"
        self.assertTrue(isShaderWordUsedInternal("USE_FOG", context))
        self.assertFalse(isShaderWordUsedInternal("FOG", context))

    def testComponentType(self) -> None:
        self.put("Source/Engine/Known.h", 'REFLECT( Tooltip = "uses (parentheses)" )\nclass SW_API KnownComponent : public Component\n{};\n')
        self.put("Resource/game/p/prefabs/a.prefab.xml",
                 '<Prefab><GameObject><_listComponent><KnownComponent/><TypoComponent/></_listComponent></GameObject></Prefab>')
        messages = self.findMessages({"name": "c", "check": "component_type", "include_patterns": ["*.prefab.xml"]})
        self.assertEqual(1, len(messages), messages)
        self.assertIn("TypoComponent", messages[0])

    def testEntityIds(self) -> None:
        self.put("Resource/game/p/maps/a.scene.xml",
                 '<Scene><entities><entity id="1"/><entity id="1"/><entity id="0"/><entity id="3">'
                 '<GameObject><_listComponent><MeshComponent _attachOwnerId="9"/></_listComponent></GameObject></entity></entities></Scene>')
        messages = self.findMessages({"name": "e", "check": "entity_id", "include_patterns": ["*.scene.xml"]})
        self.assertEqual(3, len(messages), messages)

    def testPrefabGuid(self) -> None:
        guidA = "11111111-1111-1111-1111-111111111111"
        guidB = "22222222-2222-2222-2222-222222222222"
        self.put("Resource/game/p/prefabs/a.prefab.xml", "<Prefab/>")
        self.put("Resource/game/p/prefabs/a.prefab.xml.meta", f"guid={guidA}\nsourcePath=game/p/prefabs/a.prefab.xml\n")
        self.put("Resource/game/p/prefabs/b.prefab.xml", "<Prefab/>")
        self.put("Resource/game/p/prefabs/b.prefab.xml.meta", f"guid={guidB}\nsourcePath=game/p/prefabs/wrong.prefab.xml\n")
        self.put("Resource/game/p/maps/a.scene.xml",
                 f'<Scene><entities><entity id="1" prefab="game/p/prefabs/a.prefab.xml" prefabGuid="{guidA}"/>'
                 f'<entity id="2" prefab="game/p/prefabs/a.prefab.xml" prefabGuid="{guidB}"/>'
                 f'<entity id="3" prefab="game/p/prefabs/moved/a.prefab.xml" prefabGuid="{guidA}"/></entities></Scene>')
        rule = {"name": "g", "check": "prefab_guid", "include_patterns": ["*.scene.xml", "*.meta"]}
        messages = self.findMessages(rule)
        self.assertEqual(2, len(messages), messages)  # 2 번 엔티티(다른 프리팹의 guid) · b 의 sourcePath
        stale = self.findMessages({**rule, "stale_path_only": True})
        self.assertEqual(1, len(stale), stale)  # 3 번 엔티티(guid 로는 열리지만 경로가 낡았다)

    def testCatalogReference(self) -> None:
        self.put("Resource/game/p/data/units.xml",
                 '<Catalog><Unit id="worker"/><Unit id="barracks" producedBy="worker"/><Unit id="marine" producedBy="barrack" '
                 'requires="barracks academy"/></Catalog>')
        messages = self.findMessages({"name": "u", "check": "catalog_reference", "include_patterns": ["*/units.xml"],
                                      "elements": ["Unit"], "reference_attributes": ["producedBy", "requires"]})
        self.assertEqual(2, len(messages), messages)


class RepositoryRulesTest(unittest.TestCase):
    """저장소의 규칙 표가 읽히고, 지금 `Resource/` 에 오류 심각도 결과가 없다(게이트 `CheckAssetRules` 와 같은 판정)."""

    def testRepositoryRulesLoadAndResourceHasNoErrors(self) -> None:
        listRule = loadRules(kRepositoryRoot / "Config/Editor/AssetValidationRules.json")
        self.assertGreater(len(listRule), 10)
        listError = [finding.format() for finding in validate(listRule, kRepositoryRoot / "Resource", kRepositoryRoot, None, "error")]
        self.assertEqual([], listError)


if __name__ == "__main__":
    unittest.main(verbosity=2)
