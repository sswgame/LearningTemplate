#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev.MakeSkinnedCape — 결정성, glb 구조(사슬 뼈 넷 · 스킨), 정점 수가 원본 망토와 같은지, 가중치가 위 · 아래 사슬 뼈 둘에 합 1 로 나뉘는지."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev import MakeSkinnedCape as cape  # noqa: E402
from TestMakeSyntheticRig import parseGlbInternal, readAccessorInternal  # noqa: E402

_kRoot = Path(__file__).resolve().parents[2]


def makeInternal() -> bytes:
    return cape.makeSkinnedCape(_kRoot / cape.kSourceGlb, _kRoot / cape.kChainSkeleton)


class MakeSkinnedCapeTest(unittest.TestCase):
    def testSameBytesEveryRun(self) -> None:
        self.assertEqual(makeInternal(), makeInternal())

    def testChainBonesAndSkin(self) -> None:
        document, _binary = parseGlbInternal(makeInternal())
        self.assertEqual([node["name"] for node in document["nodes"][:4]], ["cape0", "cape1", "cape2", "cape3"])
        self.assertEqual(len(document["skins"][0]["joints"]), 4)
        self.assertNotIn("animations", document)

    def testVertexCountMatchesTheSourceCape(self) -> None:
        sourceDocument, sourceBinary = cape.readGlbInternal(_kRoot / cape.kSourceGlb)
        node = next(node for node in sourceDocument["nodes"] if node.get("name") == cape.kCapeNode)
        sourceCount = sum(sourceDocument["accessors"][primitive["attributes"]["POSITION"]]["count"]
                          for primitive in sourceDocument["meshes"][node["mesh"]]["primitives"])
        document, _binary = parseGlbInternal(makeInternal())
        self.assertEqual(document["accessors"][document["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]["count"], sourceCount)
        self.assertGreater(len(sourceBinary), 0)

    def testWeightsSplitBetweenNeighbouringChainBones(self) -> None:
        document, binary = parseGlbInternal(makeInternal())
        attributes = document["meshes"][0]["primitives"][0]["attributes"]
        listJoint = readAccessorInternal(document, binary, attributes["JOINTS_0"])
        listWeight = readAccessorInternal(document, binary, attributes["WEIGHTS_0"])
        listPosition = readAccessorInternal(document, binary, attributes["POSITION"])
        lowestBones = set()
        for joint, weight, position in zip(listJoint, listWeight, listPosition):
            self.assertAlmostEqual(weight[0] + weight[1], 1.0, places=5)
            self.assertEqual(joint[1], joint[0] + 1)
            self.assertEqual((weight[2], weight[3]), (0.0, 0.0))
            if position[1] < -0.9:
                lowestBones.add(joint[1])
        # 망토 아랫단은 사슬 끝 쪽 뼈에 묶인다(높이로 나눴다).
        self.assertIn(3, lowestBones)


if __name__ == "__main__":
    unittest.main()
