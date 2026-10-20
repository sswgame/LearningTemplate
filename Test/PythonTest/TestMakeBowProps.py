#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev.MakeBowProps — 결정성, glb 구조(스킨 · 뼈 · 클립 이름), 클립 길이가 페어드 애니메이션 견본의 타임라인과 맞는지, 놓는 순간 시위가 돌아오는지."""

from __future__ import annotations

import sys
import unittest
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from dev import MakeBowProps as props  # noqa: E402
from TestMakeSyntheticRig import parseGlbInternal, readAccessorInternal  # noqa: E402

_kRoot = Path(__file__).resolve().parents[2]
_kPairedAsset = _kRoot / "Resource/game/shooter3d/data/anim/bow_shoot.pairedanim.xml"


def findClipLengthInternal(document: dict, binary: bytes, name: str) -> float:
    for animation in document["animations"]:
        if animation["name"] == name:
            return float(readAccessorInternal(document, binary, animation["samplers"][0]["input"])[-1][0])
    raise AssertionError(f"clip {name} is missing")


class MakeBowPropsTest(unittest.TestCase):
    def testSameBytesEveryRun(self) -> None:
        self.assertEqual(props.makeBow(), props.makeBow())
        self.assertEqual(props.makeArrow(), props.makeArrow())

    def testBowHasSkinBonesAndClips(self) -> None:
        document, _binary = parseGlbInternal(props.makeBow())
        self.assertEqual([node["name"] for node in document["nodes"][:5]], ["root", "grip", "upper_limb", "lower_limb", "string"])
        self.assertEqual(len(document["skins"][0]["joints"]), 5)
        self.assertEqual(sorted(animation["name"] for animation in document["animations"]), ["Bow_Idle", "Bow_Shoot_Bow"])

    def testClipLengthsFitThePairedTimeline(self) -> None:
        root = ElementTree.parse(_kPairedAsset).getroot()
        duration = float(root.get("duration"))
        roles = {role.get("name"): role for role in root.findall("Role")}
        bowDocument, bowBinary = parseGlbInternal(props.makeBow())
        arrowDocument, arrowBinary = parseGlbInternal(props.makeArrow())
        self.assertEqual(roles["Bow"].get("clip"), "Bow_Shoot_Bow")
        self.assertLessEqual(findClipLengthInternal(bowDocument, bowBinary, "Bow_Shoot_Bow"), duration + 1e-3)
        arrowStart = float(roles["Arrow"].get("start"))
        nockLength = findClipLengthInternal(arrowDocument, arrowBinary, "Arrow_Nock")
        self.assertLessEqual(arrowStart + nockLength, duration + 1e-3)
        # 화살은 놓는 시각에 클립이 끝나 나가고, 공유 알림 ReleaseArrow 가 같은 시각이다.
        release = float(root.find("Notify").get("time"))
        self.assertAlmostEqual(arrowStart + nockLength, release, places=3)
        self.assertAlmostEqual(props.kReleaseSeconds, release, places=3)

    def testStringSnapsBackOnRelease(self) -> None:
        document, binary = parseGlbInternal(props.makeBow())
        shoot = next(animation for animation in document["animations"] if animation["name"] == "Bow_Shoot_Bow")
        listTime = [value[0] for value in readAccessorInternal(document, binary, shoot["samplers"][0]["input"])]
        listString = readAccessorInternal(document, binary, shoot["samplers"][0]["output"])
        before = max(index for index, time in enumerate(listTime) if time < props.kReleaseSeconds)
        after = min(index for index, time in enumerate(listTime) if time >= props.kReleaseSeconds)
        self.assertLess(listString[before][2], -0.3)
        self.assertAlmostEqual(listString[after][2], -0.06, places=5)


if __name__ == "__main__":
    unittest.main()
