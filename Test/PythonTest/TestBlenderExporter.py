#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Blender 내보내기 애드온(`Tools/DCC/Blender/sw_engine_exporter`)의 bpy 없는 부분 — 이름 · 경로 규약, 좌표계 변환, 소켓 XML, 임포트 명령.

CI 에는 Blender 가 없다. 그래서 bpy 를 쓰는 `__init__.py` 는 읽지 않고, 패키지 자리에 빈 모듈을 세워 나머지만 올린다.
좌표계 변환은 **엔진의 식**과 맞춰 본다: 엔진 `quaternion::createFromYawPitchRoll` 을 옮긴 식으로 만든 회전이 이 애드온이 쓴 각에서
같은 행렬로 돌아와야 한다.
"""

from __future__ import annotations

import importlib.util
import math
import os
import random
import sys
import tempfile
import types
import unittest
import xml.etree.ElementTree as ElementTree
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
kPackageDir = kRepositoryRoot / "Tools" / "DCC" / "Blender" / "sw_engine_exporter"


def loadAddonModulesInternal() -> types.SimpleNamespace:
    package = types.ModuleType("sw_engine_exporter")
    package.__path__ = [str(kPackageDir)]
    sys.modules["sw_engine_exporter"] = package
    loaded = {}
    for name in ("Conventions", "SocketXml", "EngineImport"):
        spec = importlib.util.spec_from_file_location(f"sw_engine_exporter.{name}", kPackageDir / f"{name}.py")
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        setattr(package, name, module)
        loaded[name] = module
    return types.SimpleNamespace(**loaded)


kAddon = loadAddonModulesInternal()
Conventions = kAddon.Conventions
SocketXml = kAddon.SocketXml
EngineImport = kAddon.EngineImport


def assertMatrixNear(testCase: unittest.TestCase, expected, actual, places: int = 5) -> None:
    for row in range(3):
        for column in range(3):
            testCase.assertAlmostEqual(expected[row][column], actual[row][column], places=places, msg=f"[{row}][{column}]")


class NamingTest(unittest.TestCase):
    def testResourceNamesAreLowercaseSnake(self) -> None:
        self.assertEqual("hero_sword_001", Conventions.makeResourceName("Hero Sword.001"))
        self.assertEqual("crate-a", Conventions.makeResourceName("  Crate-A "))
        with self.assertRaises(ValueError):
            Conventions.makeResourceName("...")
        self.assertTrue(Conventions.kResourceNameRe.match(Conventions.makeResourceName("Ünïcode Böx")))

    def testSocketNamesDropThePrefixAndBlenderSuffix(self) -> None:
        self.assertEqual("Muzzle", Conventions.makeSocketName("SOCKET_Muzzle.002"))
        self.assertEqual("Hand_R", Conventions.makeSocketName("Hand R"))
        with self.assertRaises(ValueError):
            Conventions.makeSocketName("SOCKET_9lives")

    def testExportPathsFollowTheResourceLayout(self) -> None:
        paths = Conventions.makeExportPaths("D:\\Repo\\", "Shooter3D", "Rifle Mk2")
        self.assertEqual("D:/Repo/Resource/game/shooter3d/models_raw/rifle_mk2.glb", paths["source"])
        self.assertEqual("D:/Repo/Resource/game/shooter3d/models/rifle_mk2.sockets.xml", paths["sockets"])
        self.assertEqual("game/shooter3d/models/rifle_mk2.mesh", paths["meshID"])
        self.assertEqual("D:/Repo/Resource/engine/models_raw/cube.glb", Conventions.makeExportPaths("D:/Repo", "engine", "Cube")["source"])


class AxisConversionTest(unittest.TestCase):
    def testBlenderAxesLandOnEngineAxes(self) -> None:
        # Blender +Z(위) → 엔진 +Y, Blender -Y(앞) → 엔진 +Z, Blender +X → 엔진 -X(glTF 는 그대로, 엔진 임포터가 X 를 뒤집는다)
        self.assertEqual([0.0, 1.0, 0.0], Conventions.convertVectorToEngine([0.0, 0.0, 1.0]))
        self.assertEqual([0.0, 0.0, 1.0], Conventions.convertVectorToEngine([0.0, -1.0, 0.0]))
        self.assertEqual([-1.0, 0.0, 0.0], Conventions.convertVectorToEngine([1.0, 0.0, 0.0]))
        self.assertEqual([2.0, 4.0, 3.0], Conventions.convertScaleToEngine([2.0, 3.0, 4.0]))

    def testEulerMatchesTheEngineQuaternionFormula(self) -> None:
        generator = random.Random(1234)
        for _ in range(200):
            pitch, yaw, roll = (generator.uniform(-1.5, 1.5), generator.uniform(-3.1, 3.1), generator.uniform(-3.1, 3.1))
            rotation = Conventions.makeEngineRotationMatrix(pitch, yaw, roll)
            fromEngine = Conventions.makeRotationMatrixFromQuaternion(Conventions.makeEngineQuaternion(pitch, yaw, roll))
            assertMatrixNear(self, rotation, fromEngine)
            degrees = Conventions.computeEngineEulerDegrees(rotation)
            rebuilt = Conventions.makeEngineRotationMatrix(*(math.radians(value) for value in degrees))
            assertMatrixNear(self, rotation, rebuilt)

    def testBlenderRotationAboutUpBecomesEngineYaw(self) -> None:
        # Blender 에서 위(+Z) 축으로 90 도 → 엔진에서 위(+Y) 축 회전. 엔진은 왼손이라 같은 물리 회전이 요 -90 으로 적힌다.
        angle = math.radians(90.0)
        blenderRotation = [[math.cos(angle), -math.sin(angle), 0.0], [math.sin(angle), math.cos(angle), 0.0], [0.0, 0.0, 1.0]]
        degrees = Conventions.computeEngineEulerDegrees(Conventions.convertRotationToEngine(blenderRotation))
        self.assertAlmostEqual(0.0, degrees[0], places=4)
        self.assertAlmostEqual(-90.0, degrees[1], places=4)
        self.assertAlmostEqual(0.0, degrees[2], places=4)
        # 회전한 점이 같은 곳으로 간다: Blender 에서 (1,0,0) → (0,1,0), 엔진에서는 C·(0,1,0) = (0,0,-1)
        engineRotation = Conventions.makeEngineRotationMatrix(*(math.radians(value) for value in degrees))
        rotated = Conventions.transformVector3(engineRotation, Conventions.convertVectorToEngine([1.0, 0.0, 0.0]))
        for expected, actual in zip(Conventions.convertVectorToEngine([0.0, 1.0, 0.0]), rotated):
            self.assertAlmostEqual(expected, actual, places=5)

    def testMirroredMatrixKeepsAPositiveRotation(self) -> None:
        translation, rotation, scale = Conventions.decomposeMatrix4([[-2, 0, 0, 1], [0, 3, 0, 2], [0, 0, 4, 3], [0, 0, 0, 1]])
        self.assertEqual([1, 2, 3], translation)
        self.assertEqual([-2.0, 3.0, 4.0], scale)
        assertMatrixNear(self, [[1, 0, 0], [0, 1, 0], [0, 0, 1]], rotation)


class SocketXmlTest(unittest.TestCase):
    def testDraftMatchesTheEngineSocketFormat(self) -> None:
        # 손 본 아래 0.42 m 앞(Blender -Y), 위축으로 90 도 돌린 총구 소켓
        angle = math.radians(90.0)
        localMatrix = [[math.cos(angle), -math.sin(angle), 0.0, 0.0], [math.sin(angle), math.cos(angle), 0.0, -0.42], [0.0, 0.0, 1.0, 0.0],
                       [0.0, 0.0, 0.0, 1.0]]
        listSocket = [SocketXml.makeSocketDraft("SOCKET_Muzzle", "hand_r", localMatrix),
                      SocketXml.makeSocketDraft("SOCKET_Back", "", [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 1.5], [0, 0, 0, 1]], "GroundPoint")]
        text = SocketXml.makeSocketXml(listSocket)
        root = ElementTree.fromstring(text)
        self.assertEqual("SocketSet", root.tag)
        listElement = root.findall("Socket")
        self.assertEqual(["Back", "Muzzle"], [element.get("name") for element in listElement])
        back, muzzle = listElement
        self.assertIsNone(back.get("parent"))
        self.assertEqual("GroundPoint", back.get("kind"))
        self.assertEqual("0 1.5 0", back.get("translation"))
        self.assertIsNone(back.get("rotation"))
        self.assertEqual("hand_r", muzzle.get("parent"))
        self.assertEqual("0 0 0.42", muzzle.get("translation"))
        self.assertEqual("0 -90 0", muzzle.get("rotation"))
        for element in listElement:
            self.assertTrue(set(element.attrib) <= {"name", "parent", "kind", "translation", "rotation", "scale", "preview", "fallback", "anchor"})

    def testDuplicateSocketNamesAreRejected(self) -> None:
        identity = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        with self.assertRaises(ValueError):
            SocketXml.makeSocketXml([SocketXml.makeSocketDraft("SOCKET_A", "", identity), SocketXml.makeSocketDraft("SOCKET_A.001", "", identity)])

    def testDraftIsWrittenOnlyOnce(self) -> None:
        identity = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        with tempfile.TemporaryDirectory() as tempDir:
            path = os.path.join(tempDir, "models", "rifle.sockets.xml")
            self.assertTrue(SocketXml.writeSocketXmlIfMissing(path, [SocketXml.makeSocketDraft("SOCKET_A", "", identity)]))
            Path(path).write_text("<SocketSet><!-- hand edited --></SocketSet>\n", encoding="utf-8")
            self.assertFalse(SocketXml.writeSocketXmlIfMissing(path, [SocketXml.makeSocketDraft("SOCKET_B", "", identity)]))
            self.assertIn("hand edited", Path(path).read_text(encoding="utf-8"))
            self.assertFalse(SocketXml.writeSocketXmlIfMissing(os.path.join(tempDir, "none.sockets.xml"), []))

    def testEngineSocketKindsKnowTheDefaultKind(self) -> None:
        kinds = ElementTree.parse(kRepositoryRoot / "Resource/engine/character/default.socketkinds.xml").getroot()
        self.assertIn("Attach", [kind.get("name") for kind in kinds.iter("Kind")])


class EngineImportTest(unittest.TestCase):
    def testImportCommandRunsFromBin(self) -> None:
        listArgument, workingDirectory = EngineImport.makeImportCommand("D:/Repo/build/Ninja-Debug/Bin/App.exe")
        self.assertEqual(["D:/Repo/build/Ninja-Debug/Bin/App.exe", "--import-models"], listArgument)
        self.assertEqual("D:/Repo/build/Ninja-Debug/Bin", workingDirectory.replace("\\", "/"))

    def testFindsTheFirstBuiltApp(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            self.assertEqual("", EngineImport.findAppExecutable(tempDir))
            appPath = Path(tempDir) / "build/Ninja-Release/Bin/App.exe"
            appPath.parent.mkdir(parents=True)
            appPath.write_bytes(b"")
            self.assertEqual(str(appPath), EngineImport.findAppExecutable(tempDir))
            self.assertEqual("", EngineImport.findAppExecutable(tempDir, str(Path(tempDir) / "missing.exe")))

    def testImportOutputSummary(self) -> None:
        bClean, listLine = EngineImport.summarizeImportOutput("noise\n[Info] Model import: 3 sources, 1 imported, 0 problems.\n")
        self.assertTrue(bClean)
        self.assertEqual(1, len(listLine))
        bClean, _ = EngineImport.summarizeImportOutput("[Error] Failed to read glTF 'x'\n")
        self.assertFalse(bClean)


if __name__ == "__main__":
    unittest.main(verbosity=2)
