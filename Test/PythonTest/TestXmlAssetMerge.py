#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
XML 에셋 의미 비교 · 3-way 병합(`Scripts/common/XmlAssetMerge.py`, `Scripts/asset/AssetMerge.py`)의 시험 — 저장소의 실제 에셋으로 본다.

- 엔진이 쓴 씬 · 프리팹 · 파이프라인은 읽고 다시 쓰면 **바이트까지 같다**(병합 결과를 엔진이 다시 저장해도 줄이 안 바뀐다).
- 서로 다른 엔티티 · 컴포넌트 · 속성을 고친 두 갈래는 충돌 없이 합쳐지고, 같은 속성을 다르게 고치면 충돌이다.
- git 드라이버 모드는 결과를 %A 에 쓰고 충돌이면 1 이다.
"""

from __future__ import annotations

import copy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.XmlAssetMerge import (computeChildKeys, diffAssets, kConflictMarker, mergeAssets, parseXmlAsset,  # noqa: E402
                                  serializeXmlAsset)

#: 손으로 고친 흔적이 있어 엔진 서식과 바이트가 다른 파일 — 의미로는 같아야 한다.
kHandEditedEngineFile = {
    "game/empty/maps/editortest.scene.xml",  # 주석이 든 시험 씬(낡은 프리팹 경로를 일부러 둔다)
    "game/empty/maps/spriteui.scene.xml",    # 짧은 줄을 손으로 접었다
}

kSceneRelativePath = "game/shooter3d/maps/arena.scene.xml"
kMaterialRelativePath = "engine/materials/defaultmaterial.material"
kCatalogRelativePath = "game/starskirmish/data/units.xml"


def readResourceInternal(relPath: str) -> str:
    return (kRepositoryRoot / "Resource" / relPath).read_text(encoding="utf-8").replace("\r\n", "\n")


def findEntityInternal(asset, entityId: str):
    return next(entity for entity in asset.root.iter("entity") if entity.get("id") == entityId)


class RoundTripTest(unittest.TestCase):
    def testEngineWrittenAssetsRoundTripByteForByte(self) -> None:
        resourceRoot = kRepositoryRoot / "Resource"
        identicalCount = 0
        for path in sorted(list(resourceRoot.rglob("*.xml")) + list(resourceRoot.rglob("*.material"))):
            relPath = path.relative_to(resourceRoot).as_posix()
            text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
            asset = parseXmlAsset(text)
            output = serializeXmlAsset(asset)
            self.assertEqual([], diffAssets(asset, parseXmlAsset(output)), relPath)
            if asset.bEngineFormat and relPath not in kHandEditedEngineFile:
                self.assertEqual(text, output, f"{relPath} does not round-trip byte for byte")
                identicalCount += 1
        self.assertGreater(identicalCount, 30)

    def testHandWrittenFileKeepsDeclarationCommentsAndIndent(self) -> None:
        text = readResourceInternal(kCatalogRelativePath)
        output = serializeXmlAsset(parseXmlAsset(text))
        self.assertTrue(output.startswith('<?xml version="1.0" encoding="utf-8"?>\n<!--'))
        self.assertIn("\n  <Unit ", output)


class DiffTest(unittest.TestCase):
    def testAttributeOrderAndWrappingAreNotChanges(self) -> None:
        before = parseXmlAsset('<Scene><entities><entity id="1" name="A"><GameObject _a="1" _b="2" /></entity></entities></Scene>')
        after = parseXmlAsset('<Scene>\n  <entities>\n    <entity name="A" id="1">\n      <GameObject _b="2"\n         _a="1"/>\n'
                              '    </entity>\n  </entities>\n</Scene>')
        self.assertEqual([], diffAssets(before, after))

    def testRealSceneEditsAreReportedByEntityAndComponent(self) -> None:
        base = parseXmlAsset(readResourceInternal(kSceneRelativePath))
        edited = copy.deepcopy(base)
        camera = findEntityInternal(edited, "2")
        component = next(iter(camera.iter("_listComponent")))[0]
        component.set("_fovY", "0.9")
        entities = edited.root.find("entities")
        entities.remove(findEntityInternal(edited, "3"))
        listChange = diffAssets(base, edited)
        self.assertEqual(2, len(listChange), [change.describe() for change in listChange])
        self.assertEqual({"attribute", "removed"}, {change.kind for change in listChange})
        attributeChange = next(change for change in listChange if change.kind == "attribute")
        self.assertIn("entity#2", attributeChange.path)
        self.assertIn(component.tag, attributeChange.path)
        self.assertEqual("_fovY", attributeChange.name)

    def testComponentsAreKeyedByComponentName(self) -> None:
        parent = parseXmlAsset('<_listComponent><MeshComponent _componentName="Body"/><MeshComponent _componentName="Hat"/>'
                               '<MeshComponent/><MeshComponent/></_listComponent>').root
        self.assertEqual(["MeshComponent#Body", "MeshComponent#Hat", "MeshComponent[0]", "MeshComponent[1]"],
                         [key for key, _ in computeChildKeys(parent)])


class MergeTest(unittest.TestCase):
    def testIndependentEditsOnARealSceneMergeCleanly(self) -> None:
        base = parseXmlAsset(readResourceInternal(kSceneRelativePath))
        ours = copy.deepcopy(base)
        theirs = copy.deepcopy(base)
        # 우리: 카메라의 시야각. 저쪽: 다른 엔티티의 이름 · 새 엔티티 하나. 둘 다 같은 목록 끝에 엔티티를 더한다.
        next(iter(findEntityInternal(ours, "2").iter("_listComponent")))[0].set("_fovY", "0.9")
        findEntityInternal(theirs, "3").set("name", "FloorRenamed")
        oursNew = copy.deepcopy(findEntityInternal(base, "3"))
        oursNew.set("id", "9001")
        ours.root.find("entities").append(oursNew)
        theirsNew = copy.deepcopy(findEntityInternal(base, "3"))
        theirsNew.set("id", "9002")
        theirs.root.find("entities").append(theirsNew)

        merged, listConflict = mergeAssets(base, ours, theirs)
        self.assertEqual([], listConflict)
        self.assertEqual("0.9", next(iter(findEntityInternal(merged, "2").iter("_listComponent")))[0].get("_fovY"))
        self.assertEqual("FloorRenamed", findEntityInternal(merged, "3").get("name"))
        listId = [entity.get("id") for entity in merged.root.iter("entity")]
        self.assertIn("9001", listId)
        self.assertIn("9002", listId)
        self.assertNotIn(kConflictMarker, serializeXmlAsset(merged))
        # 결과를 다시 읽어도 같다(서식이 읽을 수 있는 XML 이다)
        self.assertEqual([], diffAssets(merged, parseXmlAsset(serializeXmlAsset(merged))))

    def testSameAttributeChangedDifferentlyIsAConflict(self) -> None:
        base = parseXmlAsset(readResourceInternal(kSceneRelativePath))
        ours, theirs = copy.deepcopy(base), copy.deepcopy(base)
        findEntityInternal(ours, "2").set("name", "CameraA")
        findEntityInternal(theirs, "2").set("name", "CameraB")
        merged, listConflict = mergeAssets(base, ours, theirs)
        self.assertEqual(1, len(listConflict))
        self.assertEqual("name", listConflict[0].name)
        self.assertEqual("CameraA", findEntityInternal(merged, "2").get("name"))
        self.assertIn(kConflictMarker, serializeXmlAsset(merged))

        preferred, listPreferredConflict = mergeAssets(base, ours, theirs, "theirs")
        self.assertEqual(1, len(listPreferredConflict))
        self.assertEqual("CameraB", findEntityInternal(preferred, "2").get("name"))
        self.assertNotIn(kConflictMarker, serializeXmlAsset(preferred))

    def testDeleteAgainstModifyKeepsTheModifiedElementAndReportsIt(self) -> None:
        base = parseXmlAsset(readResourceInternal(kSceneRelativePath))
        ours, theirs = copy.deepcopy(base), copy.deepcopy(base)
        ours.root.find("entities").remove(findEntityInternal(ours, "3"))
        findEntityInternal(theirs, "3").set("name", "Changed")
        merged, listConflict = mergeAssets(base, ours, theirs)
        self.assertEqual(1, len(listConflict))
        self.assertEqual("Changed", findEntityInternal(merged, "3").get("name"))

    def testDeleteOfAnUntouchedElementWins(self) -> None:
        base = parseXmlAsset(readResourceInternal(kSceneRelativePath))
        ours, theirs = copy.deepcopy(base), copy.deepcopy(base)
        ours.root.find("entities").remove(findEntityInternal(ours, "3"))
        findEntityInternal(theirs, "2").set("name", "Changed")
        merged, listConflict = mergeAssets(base, ours, theirs)
        self.assertEqual([], listConflict)
        self.assertNotIn("3", [entity.get("id") for entity in merged.root.iter("entity")])

    def testMaterialPropertiesAddedOnBothSidesAreUnited(self) -> None:
        base = parseXmlAsset(readResourceInternal(kMaterialRelativePath))
        ours, theirs = copy.deepcopy(base), copy.deepcopy(base)
        oursItem = copy.deepcopy(ours.root.find("_properties")[0])
        oursItem.set("name", "roughnessBias")
        ours.root.find("_properties").append(oursItem)
        theirsItem = copy.deepcopy(theirs.root.find("_properties")[0])
        theirsItem.set("name", "emissiveBoost")
        theirs.root.find("_properties").append(theirsItem)
        merged, listConflict = mergeAssets(base, ours, theirs)
        self.assertEqual([], listConflict)
        listName = [item.get("name") for item in merged.root.find("_properties")]
        self.assertIn("roughnessBias", listName)
        self.assertIn("emissiveBoost", listName)
        self.assertTrue(serializeXmlAsset(merged).startswith("<?xml"))

    def testCatalogEditsToDifferentUnitsMerge(self) -> None:
        base = parseXmlAsset(readResourceInternal(kCatalogRelativePath))
        ours, theirs = copy.deepcopy(base), copy.deepcopy(base)
        listUnitId = [unit.get("id") for unit in base.root.iter("Unit")]
        next(unit for unit in ours.root.iter("Unit") if unit.get("id") == listUnitId[1]).set("hp", "1")
        next(unit for unit in theirs.root.iter("Unit") if unit.get("id") == listUnitId[2]).set("hp", "2")
        merged, listConflict = mergeAssets(base, ours, theirs)
        self.assertEqual([], listConflict)
        mapHp = {unit.get("id"): unit.get("hp") for unit in merged.root.iter("Unit")}
        self.assertEqual("1", mapHp[listUnitId[1]])
        self.assertEqual("2", mapHp[listUnitId[2]])


class GitDriverTest(unittest.TestCase):
    def runDriverInternal(self, listArgument: list[str]) -> subprocess.CompletedProcess:
        return subprocess.run([sys.executable, str(kRepositoryRoot / "Scripts/asset/AssetMerge.py"), *listArgument],
                              capture_output=True, text=True, encoding="utf-8", errors="replace")

    def testGitMergeWritesTheResultIntoOursAndKeepsCrlf(self) -> None:
        base = parseXmlAsset(readResourceInternal(kSceneRelativePath))
        ours, theirs = copy.deepcopy(base), copy.deepcopy(base)
        findEntityInternal(ours, "2").set("name", "Ours")
        findEntityInternal(theirs, "3").set("name", "Theirs")
        with tempfile.TemporaryDirectory() as tempDir:
            listPath = [Path(tempDir) / name for name in ("base.xml", "ours.xml", "theirs.xml")]
            for path, asset in zip(listPath, (base, ours, theirs)):
                path.write_bytes(serializeXmlAsset(asset).replace("\n", "\r\n").encode("utf-8"))
            result = self.runDriverInternal(["git-merge", *map(str, listPath), kSceneRelativePath])
            self.assertEqual(0, result.returncode, result.stderr)
            mergedBytes = listPath[1].read_bytes()
            self.assertIn(b"\r\n", mergedBytes)
            merged = parseXmlAsset(mergedBytes.decode("utf-8"))
            self.assertEqual("Ours", findEntityInternal(merged, "2").get("name"))
            self.assertEqual("Theirs", findEntityInternal(merged, "3").get("name"))

            findEntityInternal(theirs, "2").set("name", "TheirsToo")
            listPath[2].write_text(serializeXmlAsset(theirs), encoding="utf-8")
            listPath[1].write_text(serializeXmlAsset(ours), encoding="utf-8")
            conflicted = self.runDriverInternal(["git-merge", *map(str, listPath), kSceneRelativePath])
            self.assertEqual(1, conflicted.returncode)
            self.assertIn(kConflictMarker, listPath[1].read_text(encoding="utf-8"))

    def testUnreadableInputLeavesAManualMerge(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            listPath = [Path(tempDir) / name for name in ("base.xml", "ours.xml", "theirs.xml")]
            for path in listPath:
                path.write_text("<<< not xml", encoding="utf-8")
            self.assertEqual(2, self.runDriverInternal(["git-merge", *map(str, listPath)]).returncode)

    def testDiffCommandExitCode(self) -> None:
        scenePath = str(kRepositoryRoot / "Resource" / kSceneRelativePath)
        self.assertEqual(0, self.runDriverInternal(["diff", scenePath, scenePath]).returncode)


if __name__ == "__main__":
    unittest.main(verbosity=2)
