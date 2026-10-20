#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
페어드 애니메이션 견본(`game/shooter3d/data/anim/bow_shoot.pairedanim.xml`)의 활 · 화살 프롭(스킨드 glTF `.glb`)을 만듭니다.

저장소의 CC0 캐릭터 팩(KayKit)에는 시위가 움직이는 활과 화살 클립이 없다. 궁수 역할은 기사의 `2H_Ranged_Shoot` 클립을 쓰고,
활 · 화살 역할의 클립은 이 도구가 만든 합성 프롭이 맡는다(라이선스 문제 없음, 길이 · 시각을 에셋의 타임라인에 맞춘다).

만드는 것(`--out` 폴더, 기본 `Resource/game/shooter3d/models_raw/props/`):
  bow.glb    뼈 다섯(root · grip · upper_limb · lower_limb · string) — 활대 둘은 가는 상자, 시위는 위 · 아래 끝을 활대에, 가운데를 string 본에 묶은 띠.
             클립 `Bow_Shoot_Bow`(당김 0 ~ kReleaseSeconds, 놓으면 한 표본 안에 되돌아감, 길이 kShootSeconds) · `Bow_Idle`(쉼, 반복).
  arrow.glb  뼈 하나(root) — 화살대 상자. 클립 `Arrow_Nock`(시위를 따라 뒤로, 길이 kNockSeconds) · `Arrow_Hidden`(크기 0).

좌표는 glTF(+Y 위, +Z 앞)이다. 결과는 결정적이다(같은 인자 = 같은 바이트, 실수는 소수 여섯째 자리에서 자른다).
만든 뒤 `App --import-models` 로 `models/props/<이름>/` 에 메시 · 클립을 쓰고 `models_raw/import.stamp` 와 함께 커밋한다(`docs/agents/NamingScripts.md` "Resource Assets").

사용법:
  py -3 Scripts/dev/MakeBowProps.py                     # 기본 폴더에 bow.glb · arrow.glb
  py -3 Scripts/dev/MakeBowProps.py --out C:/tmp/props
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common import getProjectRoot
from dev.MakeSyntheticRig import BufferWriter

#: 기본 출력 폴더(저장소 루트 기준).
kDefaultFolder = "Resource/game/shooter3d/models_raw/props"
#: 키 표본율(Hz) — 임포터가 선형 보간한다.
kSampleRate = 30
#: 활 클립 길이 · 놓는 시각(초). 에셋의 공유 알림 `ReleaseArrow` 시각과 같아야 한다(에셋 머리 주석).
kShootSeconds = 1.0
kReleaseSeconds = 0.6
#: 화살 메기기 클립 길이(초) — 에셋의 `start` 0.2 + 이 길이 = 놓는 시각.
kNockSeconds = 0.4
#: 쉼 클립 길이(초).
kIdleSeconds = 1.0
#: 시위를 당기는 거리(m)와 그때 활대가 안으로 휘는 각(라디안).
kDrawDistance = 0.35
kLimbBendRadians = 0.25
#: 활대 반 길이 · 굵기, 화살 길이 · 굵기(m).
kLimbLength = 0.6
kLimbThickness = 0.025
kArrowLength = 0.7
kArrowThickness = 0.008

_kComponentFloat = 5126
_kComponentUShort = 5123
_kTargetArray = 34962
_kTargetElementArray = 34963


def roundInternal(value: float) -> float:
    return float(f"{value:.6f}")


def addBoxInternal(listPosition: list, listNormal: list, listJoint: list, listWeight: list, listIndex: list, center: tuple[float, float, float],
                   half: tuple[float, float, float], joint: int) -> None:
    """뼈 하나에 묶인 상자(면마다 정점 넷, 법선은 면 방향)를 더한다."""
    listFace = (((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
                ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0)))
    for normal, first, second in listFace:
        base = len(listPosition)
        for signFirst, signSecond in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            position = tuple(roundInternal(center[axis] + half[axis] * (normal[axis] + signFirst * first[axis] + signSecond * second[axis])) for axis in range(3))
            listPosition.append(position)
            listNormal.append(tuple(float(value) for value in normal))
            listJoint.append((joint, 0, 0, 0))
            listWeight.append((1.0, 0.0, 0.0, 0.0))
        listIndex.extend((base, base + 1, base + 2, base, base + 2, base + 3))


def makeTimesInternal(seconds: float) -> list[float]:
    keyCount = int(round(seconds * kSampleRate)) + 1
    return [roundInternal(seconds * key / (keyCount - 1)) for key in range(keyCount)]


def quaternionXInternal(angle: float) -> tuple[float, float, float, float]:
    return (roundInternal(math.sin(angle * 0.5)), 0.0, 0.0, roundInternal(math.cos(angle * 0.5)))


def computeDrawInternal(time: float) -> float:
    """활 클립의 당김 비율(0 ~ 1) — 놓기 전까지 부드럽게 당기고, 놓으면 다음 표본에서 0 이다."""
    if time >= kReleaseSeconds:
        return 0.0
    ratio = time / kReleaseSeconds
    return roundInternal(ratio * ratio * (3.0 - 2.0 * ratio))


def addClipInternal(writer: BufferWriter, name: str, listTime: list[float], listChannel: list[tuple[int, str, list[tuple]]]) -> dict:
    """클립 하나 — 시각 접근자 하나와 채널(노드, 경로, 값 목록)마다 출력 접근자 하나."""
    timeBounds = ([listTime[0]], [struct.unpack("<f", struct.pack("<f", listTime[-1]))[0]])
    listPart: list[tuple[bytes, int, int, str, bool, tuple[list[float], list[float]] | None]] = [
        (b"".join(struct.pack("<f", value) for value in listTime), _kComponentFloat, len(listTime), "SCALAR", False, timeBounds)]
    for _node, path, listValue in listChannel:
        kind = "VEC4" if path == "rotation" else "VEC3"
        listPart.append((b"".join(struct.pack("<" + "f" * len(value), *value) for value in listValue), _kComponentFloat, len(listValue), kind, False, None))
    listAccessor = writer.addPackedAccessors(listPart)
    listSampler = [{"input": listAccessor[0], "output": output, "interpolation": "LINEAR"} for output in listAccessor[1:]]
    channels = [{"sampler": index, "target": {"node": node, "path": path}} for index, (node, path, _value) in enumerate(listChannel)]
    return {"name": name, "samplers": listSampler, "channels": channels}


def encodeGlbInternal(document: dict, binary: bytes) -> bytes:
    jsonBytes = json.dumps(document, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    jsonBytes += b" " * ((4 - len(jsonBytes) % 4) % 4)
    total = 12 + 8 + len(jsonBytes) + 8 + len(binary)
    header = struct.pack("<4sII", b"glTF", 2, total)
    return header + struct.pack("<I4s", len(jsonBytes), b"JSON") + jsonBytes + struct.pack("<I4s", len(binary), b"BIN\x00") + binary


def makeSkinnedGlbInternal(listBone: list[tuple[str, int, tuple[float, float, float]]], mesh: tuple[list, list, list, list, list], listAnimation: list,
                           writer: BufferWriter, name: str) -> bytes:
    """뼈(이름, 부모, 월드 자리 — 레스트 회전은 단위) · 메시 · 클립으로 glb 를 만든다."""
    listPosition, listNormal, listJoint, listWeight, listIndex = mesh
    position = writer.addVec3(listPosition, _kTargetArray)
    normal = writer.addVec3(listNormal, _kTargetArray)
    joints = writer.addAccessor(b"".join(struct.pack("<4H", *value) for value in listJoint), _kComponentUShort, len(listJoint), "VEC4", _kTargetArray)
    weights = writer.addAccessor(b"".join(struct.pack("<4f", *value) for value in listWeight), _kComponentFloat, len(listWeight), "VEC4", _kTargetArray)
    indices = writer.addAccessor(b"".join(struct.pack("<H", value) for value in listIndex), _kComponentUShort, len(listIndex), "SCALAR", _kTargetElementArray)
    matrixPayload = bytearray()
    for _boneName, _parent, world in listBone:
        matrixPayload.extend(struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -world[0], -world[1], -world[2], 1))
    inverseBind = writer.addAccessor(bytes(matrixPayload), _kComponentFloat, len(listBone), "MAT4")
    listNode: list[dict] = []
    for bone, (boneName, parent, world) in enumerate(listBone):
        parentWorld = listBone[parent][2] if parent >= 0 else (0.0, 0.0, 0.0)
        node: dict = {"name": boneName, "translation": [roundInternal(world[axis] - parentWorld[axis]) for axis in range(3)]}
        children = [child for child, (_childName, childParent, _childWorld) in enumerate(listBone) if childParent == bone]
        if children:
            node["children"] = children
        listNode.append(node)
    meshNode = len(listNode)
    listNode.append({"name": name, "mesh": 0, "skin": 0})
    while len(writer.data) % 4 != 0:
        writer.data.append(0)
    document = {
        "asset": {"version": "2.0", "generator": "Scripts/dev/MakeBowProps.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, meshNode]}],
        "nodes": listNode,
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": position, "NORMAL": normal, "JOINTS_0": joints, "WEIGHTS_0": weights},
                                                  "indices": indices, "mode": 4}]}],
        "skins": [{"joints": list(range(len(listBone))), "inverseBindMatrices": inverseBind, "skeleton": 0}],
        "animations": listAnimation,
        "accessors": writer.listAccessor,
        "bufferViews": writer.listView,
        "buffers": [{"byteLength": len(writer.data)}],
    }
    return encodeGlbInternal(document, bytes(writer.data))


def makeBow() -> bytes:
    """활 — root · grip(손잡이) · 활대 둘 · 시위. 시위 띠는 끝을 활대에, 가운데를 string 본에 묶는다."""
    listBone = [("root", -1, (0.0, 0.0, 0.0)), ("grip", 0, (0.0, 0.0, 0.0)), ("upper_limb", 1, (0.0, 0.05, 0.0)), ("lower_limb", 1, (0.0, -0.05, 0.0)),
                ("string", 1, (0.0, 0.0, -0.06))]
    listPosition: list = []
    listNormal: list = []
    listJoint: list = []
    listWeight: list = []
    listIndex: list = []
    half = kLimbLength * 0.5
    addBoxInternal(listPosition, listNormal, listJoint, listWeight, listIndex, (0.0, 0.05 + half, 0.0), (kLimbThickness, half, kLimbThickness), 2)
    addBoxInternal(listPosition, listNormal, listJoint, listWeight, listIndex, (0.0, -0.05 - half, 0.0), (kLimbThickness, half, kLimbThickness), 3)
    # 시위: 위 끝(upper_limb) · 가운데(string) · 아래 끝(lower_limb) 세 고리, 고리마다 정점 둘(좌우)로 띠 두 칸.
    tipHeight = 0.05 + kLimbLength
    listRing = ((tipHeight, 2), (0.0, 4), (-tipHeight, 3))
    base = len(listPosition)
    for height, joint in listRing:
        for side in (-1.0, 1.0):
            listPosition.append((roundInternal(side * 0.003), roundInternal(height), -0.06))
            listNormal.append((0.0, 0.0, -1.0))
            listJoint.append((joint, 0, 0, 0))
            listWeight.append((1.0, 0.0, 0.0, 0.0))
    for ring in range(2):
        first = base + ring * 2
        listIndex.extend((first, first + 1, first + 3, first, first + 3, first + 2))
    writer = BufferWriter()
    listShootTime = makeTimesInternal(kShootSeconds)
    listDraw = [computeDrawInternal(time) for time in listShootTime]
    shoot = addClipInternal(writer, "Bow_Shoot_Bow", listShootTime, [
        (4, "translation", [(0.0, 0.0, roundInternal(-0.06 - kDrawDistance * draw)) for draw in listDraw]),
        (2, "rotation", [quaternionXInternal(-kLimbBendRadians * draw) for draw in listDraw]),
        (3, "rotation", [quaternionXInternal(kLimbBendRadians * draw) for draw in listDraw]),
    ])
    listIdleTime = makeTimesInternal(kIdleSeconds)
    idle = addClipInternal(writer, "Bow_Idle", listIdleTime, [(4, "translation", [(0.0, 0.0, -0.06) for _time in listIdleTime])])
    return makeSkinnedGlbInternal(listBone, (listPosition, listNormal, listJoint, listWeight, listIndex), [shoot, idle], writer, "bow")


def makeArrow() -> bytes:
    """화살 — 뼈 하나와 화살대. 메기기는 시위를 따라 뒤로 가고, 숨김은 크기 0 이다."""
    listBone = [("root", -1, (0.0, 0.0, 0.0))]
    listPosition: list = []
    listNormal: list = []
    listJoint: list = []
    listWeight: list = []
    listIndex: list = []
    addBoxInternal(listPosition, listNormal, listJoint, listWeight, listIndex, (0.0, 0.0, kArrowLength * 0.5 - 0.06), (kArrowThickness, kArrowThickness, kArrowLength * 0.5), 0)
    writer = BufferWriter()
    listNockTime = makeTimesInternal(kNockSeconds)
    # 메기기는 활의 당김과 같은 곡선을 공유 시각(start 0.2 + t)에서 따라간다.
    nockStart = kReleaseSeconds - kNockSeconds
    nock = addClipInternal(writer, "Arrow_Nock", listNockTime,
                           [(0, "translation", [(0.0, 0.0, roundInternal(-kDrawDistance * computeDrawInternal(nockStart + time))) for time in listNockTime])])
    listHiddenTime = makeTimesInternal(kIdleSeconds)
    hidden = addClipInternal(writer, "Arrow_Hidden", listHiddenTime, [(0, "scale", [(0.0, 0.0, 0.0) for _time in listHiddenTime])])
    return makeSkinnedGlbInternal(listBone, (listPosition, listNormal, listJoint, listWeight, listIndex), [nock, hidden], writer, "arrow")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="페어드 애니메이션 견본의 활 · 화살 프롭(스킨드 glb)을 만듭니다")
    parser.add_argument("--out", default="", help="출력 폴더(기본: Resource/game/shooter3d/models_raw/props)")
    args = parser.parse_args(argv)
    folder = pathlib.Path(args.out) if args.out else getProjectRoot() / kDefaultFolder
    folder.mkdir(parents=True, exist_ok=True)
    for fileName, data in (("bow.glb", makeBow()), ("arrow.glb", makeArrow())):
        (folder / fileName).write_bytes(data)
        print(f"[MakeBowProps] {folder / fileName} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
