#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev.MakeSyntheticRig — 결정성(같은 인자 = 같은 바이트), 본 수 · 위상 · 모프 · 클립이 출력과 일치, 영향 네 개 · 값이 움직임, 범위 밖 인자 거절, 커밋한 대표와 같음."""

from __future__ import annotations

import json
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from dev import MakeSyntheticRig as rig  # noqa: E402

_kRoot = Path(__file__).resolve().parents[2]


def parseGlbInternal(data: bytes) -> tuple[dict, bytes]:
    magic, version, total = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2 or total != len(data):
        raise AssertionError("not a glb 2.0 container")
    jsonLength, jsonKind = struct.unpack_from("<I4s", data, 12)
    document = json.loads(data[20:20 + jsonLength].decode("ascii"))
    binLength, binKind = struct.unpack_from("<I4s", data, 20 + jsonLength)
    if jsonKind != b"JSON" or binKind != b"BIN\x00":
        raise AssertionError("glb chunks are not JSON + BIN")
    return document, data[28 + jsonLength:28 + jsonLength + binLength]


def readAccessorInternal(document: dict, binary: bytes, index: int) -> list[tuple]:
    accessor = document["accessors"][index]
    view = document["bufferViews"][accessor["bufferView"]]
    width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4, "MAT4": 16}[accessor["type"]]
    code = {5126: "f", 5123: "H", 5125: "I", 5122: "h"}[accessor["componentType"]]
    offset = view["byteOffset"] + accessor.get("byteOffset", 0)
    stride = struct.calcsize("<" + code * width)
    listValue = []
    for element in range(accessor["count"]):
        values = struct.unpack_from("<" + code * width, binary, offset + element * stride)
        if accessor.get("normalized"):
            values = tuple(max(value / 32767.0, -1.0) for value in values)
        listValue.append(values)
    return listValue


def makeOptionsInternal(topology: str, bones: int, morphs: int = 3, clips: int = 2, seed: int = 7) -> rig.RigOptions:
    return rig.RigOptions(topology, bones, morphs, clips, 0.5, 16, seed)


class MakeSyntheticRigTest(unittest.TestCase):
    def testSameArgumentsGiveSameBytesAndSeedChangesThem(self) -> None:
        first = rig.makeRig(makeOptionsInternal("biped", 60))
        second = rig.makeRig(makeOptionsInternal("biped", 60))
        self.assertEqual(first, second)
        other = rig.makeRig(makeOptionsInternal("biped", 60, seed=8))
        self.assertNotEqual(first[0], other[0])

    def testOutputMatchesBonesTopologyMorphsAndClips(self) -> None:
        for topology in rig.kTopologies:
            for bones in (rig.getMinimumBones(topology), 60, 150, 300):
                with self.subTest(topology=topology, bones=bones):
                    glbBytes, rigBytes = rig.makeRig(makeOptionsInternal(topology, bones))
                    document, binary = parseGlbInternal(glbBytes)
                    skin = document["skins"][0]
                    self.assertEqual(bones, len(skin["joints"]))
                    self.assertEqual(bones, len(readAccessorInternal(document, binary, skin["inverseBindMatrices"])))
                    self.assertEqual(topology, document["asset"]["extras"]["topology"])
                    listName = [document["nodes"][joint]["name"] for joint in skin["joints"]]
                    self.assertEqual(len(listName), len(set(listName)))
                    primitive = document["meshes"][0]["primitives"][0]
                    self.assertEqual(3, len(primitive["targets"]))
                    self.assertEqual(3, len(document["meshes"][0]["extras"]["targetNames"]))
                    self.assertEqual(2, len(document["animations"]))
                    for animation in document["animations"]:
                        rotationCount = sum(1 for channel in animation["channels"] if channel["target"]["path"] == "rotation")
                        weightCount = sum(1 for channel in animation["channels"] if channel["target"]["path"] == "weights")
                        self.assertEqual((bones, 1), (rotationCount, weightCount))
                    rigDocument = json.loads(rigBytes.decode("ascii"))
                    setName = set(listName)
                    for node in rigDocument["nodes"]:
                        listBone = node["bones"] if node["type"] == "SpringChain" else [node["root"], node["mid"], node["end"]]
                        self.assertTrue(set(listBone) <= setName, node["name"])
                    legCount = sum(1 for name in listName if name.startswith("leg_") and name.endswith("_00"))
                    self.assertEqual(legCount, sum(1 for node in rigDocument["nodes"] if node["type"] == "TwoBoneIK"))

    def testVerticesHaveFourInfluencesSummingToOne(self) -> None:
        glbBytes, _ = rig.makeRig(makeOptionsInternal("quadruped", 60))
        document, binary = parseGlbInternal(glbBytes)
        attributes = document["meshes"][0]["primitives"][0]["attributes"]
        listJoint = readAccessorInternal(document, binary, attributes["JOINTS_0"])
        listWeight = readAccessorInternal(document, binary, attributes["WEIGHTS_0"])
        self.assertEqual(document["asset"]["extras"]["mesh_vertices"], len(listJoint))
        for joints, weights in zip(listJoint, listWeight):
            self.assertAlmostEqual(1.0, sum(weights), places=5)
            self.assertEqual(4, sum(1 for weight in weights if weight > 0.0))
            self.assertEqual(4, len(set(joints)))
            self.assertTrue(all(joint < 60 for joint in joints))

    def testEveryJointRotationMoves(self) -> None:
        glbBytes, _ = rig.makeRig(makeOptionsInternal("multileg", 60))
        document, binary = parseGlbInternal(glbBytes)
        for animation in document["animations"]:
            for channel in animation["channels"]:
                listKey = readAccessorInternal(document, binary, animation["samplers"][channel["sampler"]]["output"])
                spread = max(max(key) - min(key) for key in zip(*listKey))
                self.assertGreater(spread, 0.05, channel["target"])

    def testOutOfRangeArgumentsStop(self) -> None:
        for options in (makeOptionsInternal("biped", rig.getMinimumBones("biped") - 1), makeOptionsInternal("serpent", rig.kMaxBones + 1),
                        makeOptionsInternal("biped", 60, morphs=rig.kMaxMorphs + 1), makeOptionsInternal("biped", 60, clips=0),
                        rig.RigOptions("biped", 60, 0, 1, 0.5, 12, 1), rig.RigOptions("biped", 60, 0, 1, 0.0, 8, 1),
                        rig.RigOptions("tripod", 60, 0, 1, 0.5, 8, 1)):
            with self.subTest(options=options):
                with self.assertRaises(ValueError):
                    rig.makeRig(options)

    def testCommittedRepresentativesMatchTheGenerator(self) -> None:
        folder = _kRoot / "Resource" / rig.kRepresentativeFolder
        for topology, bones in rig.kRepresentatives:
            with self.subTest(topology=topology, bones=bones):
                stem = rig.makeRepresentativeName(topology, bones)
                glbBytes, rigBytes = rig.makeRig(rig.makeRepresentativeOptions(topology, bones))
                self.assertEqual(glbBytes, (folder / f"{stem}.glb").read_bytes(), "run: py -3 Scripts/dev/MakeSyntheticRig.py --representatives")
                self.assertEqual(rigBytes, (folder / f"{stem}.rig.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
