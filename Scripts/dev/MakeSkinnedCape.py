#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
기사 망토(KayKit `knight.glb` 의 `Knight_Cape`)를 사슬 뼈 넷(cape0 ~ cape3)에 묶은 스킨드 glTF(`.glb`)로 만듭니다 — 스프링 망토 견본의 원본.

KayKit 망토는 가슴 본에 붙은 단단한 메시라 스프링 본(`rigs/knight_cape.rig.xml` 의 `SpringChain`)이 움직일 뼈가 없다. 이 도구가 망토 정점을
높이로 나눠 사슬 뼈 두 개에 선형으로 묶는다(벤치 데모 `BenchSceneRig.cpp` 의 `createCapeSkinnedCopy` 와 같은 규칙 — 정점 높이가 두 뼈 사이면 둘에 나눈다).
사슬 뼈 자리는 `rigs/knight_cape.skeleton.xml` 의 레퍼런스 이동이다(회전은 단위). 정점 공간은 망토 노드 공간(= cape0 공간)이다.

만드는 것(`--out` 폴더, 기본 `Resource/game/shooter3d/models_raw/kaykit/`): knight_cape_chain.glb — 뼈 넷 · 스킨 · 위치 · 법선 · UV(클립 없음).
만든 뒤 `App --import-models` 로 `models/kaykit/knight_cape_chain.mesh` · 스켈레톤을 쓰고 `models_raw/import.stamp` 와 함께 커밋한다.
결과는 결정적이다(같은 입력 = 같은 바이트).

사용법:
  py -3 Scripts/dev/MakeSkinnedCape.py
  py -3 Scripts/dev/MakeSkinnedCape.py --out C:/tmp/cape
"""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys
import xml.etree.ElementTree as ElementTree

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common.GltfBufferWriter import BufferWriter  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402

#: 원본 glb · 사슬 스켈레톤 · 기본 출력 폴더(저장소 루트 기준).
kSourceGlb = "Resource/game/shooter3d/models_raw/kaykit/knight.glb"
kChainSkeleton = "Resource/game/shooter3d/rigs/knight_cape.skeleton.xml"
kDefaultFolder = "Resource/game/shooter3d/models_raw/kaykit"
#: 원본에서 망토 메시를 가진 노드 이름과 출력 이름.
kCapeNode = "Knight_Cape"
kOutputName = "knight_cape_chain"

_kComponentFloat = 5126
_kComponentUShort = 5123
_kComponentUInt = 5125
_kTargetArray = 34962
_kTargetElementArray = 34963
_kComponentFormat = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
_kTypeWidth = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def readGlbInternal(path: pathlib.Path) -> tuple[dict, bytes]:
    """glb 의 JSON 문서와 BIN 청크입니다."""
    data = path.read_bytes()
    magic, _version, _length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF":
        raise ValueError(f"{path} is not a glb")
    jsonLength, _jsonType = struct.unpack_from("<I4s", data, 12)
    document = json.loads(data[20:20 + jsonLength])
    binOffset = 20 + jsonLength
    binLength, _binType = struct.unpack_from("<I4s", data, binOffset)
    return document, data[binOffset + 8:binOffset + 8 + binLength]


def readAccessorInternal(document: dict, binary: bytes, accessorIndex: int) -> list[tuple]:
    """접근자 하나를 원소(튜플) 목록으로 읽습니다(정규화 · 희소 접근자는 쓰지 않는 원본이라 다루지 않는다)."""
    accessor = document["accessors"][accessorIndex]
    if "sparse" in accessor or accessor.get("normalized", False):
        raise ValueError(f"accessor {accessorIndex} is sparse or normalized - not supported")
    view = document["bufferViews"][accessor["bufferView"]]
    width = _kTypeWidth[accessor["type"]]
    elementFormat = "<" + _kComponentFormat[accessor["componentType"]] * width
    elementSize = struct.calcsize(elementFormat)
    stride = view.get("byteStride", elementSize)
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    return [struct.unpack_from(elementFormat, binary, start + index * stride) for index in range(accessor["count"])]


def readChainInternal(path: pathlib.Path) -> list[tuple[str, int, tuple[float, float, float]]]:
    """사슬 스켈레톤의 (이름, 부모, 모델 자리)입니다 — 레퍼런스 이동을 부모부터 더한다(회전은 단위라야 한다)."""
    root = ElementTree.parse(path).getroot()
    listBone: list[tuple[str, int, tuple[float, float, float]]] = []
    for bone in root.iterfind("_listBone/SkeletonBone"):
        reference = bone.find("_referencePose")
        if reference is None:
            raise ValueError(f"chain bone {bone.get('_name')} has no _referencePose")
        if [float(value) for value in reference.get("_rotation", "").split(",")] != [0.0, 0.0, 0.0, 1.0]:
            raise ValueError(f"chain bone {bone.get('_name')} has a rest rotation - the cape chain must be straight translations")
        local = tuple(float(value) for value in reference.get("_translation", "").split(","))
        parent = int(bone.get("_parentIndex", ""))
        base = listBone[parent][2] if parent >= 0 else (0.0, 0.0, 0.0)
        listBone.append((bone.get("_name", ""), parent, (base[0] + local[0], base[1] + local[1], base[2] + local[2])))
    if len(listBone) < 2:
        raise ValueError("the cape chain needs at least two bones")
    return listBone


def computeSkinInternal(height: float, listBone: list[tuple[str, int, tuple[float, float, float]]]) -> tuple[tuple[int, int, int, int], tuple[float, float, float, float]]:
    """정점 높이를 위 · 아래 사슬 뼈 둘에 나눈 (뼈 넷, 가중치 넷)입니다(`createCapeSkinnedCopy` 와 같은 규칙)."""
    lower = 0
    while lower + 2 < len(listBone) and height < listBone[lower + 1][2][1]:
        lower += 1
    top = listBone[lower][2][1]
    bottom = listBone[lower + 1][2][1]
    fraction = min(max((top - height) / max(top - bottom, 1e-4), 0.0), 1.0)
    fraction = float(f"{fraction:.6f}")
    return (lower, lower + 1, 0, 0), (1.0 - fraction, fraction, 0.0, 0.0)


def makeSkinnedCape(sourceGlb: pathlib.Path, chainSkeleton: pathlib.Path) -> bytes:
    """망토 메시를 사슬 뼈에 묶은 glb 바이트입니다."""
    document, binary = readGlbInternal(sourceGlb)
    listNode = [node for node in document["nodes"] if node.get("name") == kCapeNode and "mesh" in node]
    if len(listNode) != 1:
        raise ValueError(f"{sourceGlb} must have exactly one mesh node named {kCapeNode}")
    listPrimitive = document["meshes"][listNode[0]["mesh"]]["primitives"]
    listBone = readChainInternal(chainSkeleton)

    listPosition: list[tuple[float, float, float]] = []
    listNormal: list[tuple[float, float, float]] = []
    listUv: list[tuple[float, float]] = []
    listIndex: list[int] = []
    for primitive in listPrimitive:
        attributes = primitive["attributes"]
        base = len(listPosition)
        listPosition.extend(readAccessorInternal(document, binary, attributes["POSITION"]))
        listNormal.extend(readAccessorInternal(document, binary, attributes["NORMAL"]))
        listUv.extend(readAccessorInternal(document, binary, attributes["TEXCOORD_0"]))
        listIndex.extend(base + value[0] for value in readAccessorInternal(document, binary, primitive["indices"]))
    listJoint: list[tuple[int, int, int, int]] = []
    listWeight: list[tuple[float, float, float, float]] = []
    for position in listPosition:
        joint, weight = computeSkinInternal(position[1], listBone)
        listJoint.append(joint)
        listWeight.append(weight)

    writer = BufferWriter()
    position = writer.addVec3(listPosition, _kTargetArray)
    normal = writer.addVec3(listNormal, _kTargetArray)
    uv = writer.addAccessor(b"".join(struct.pack("<2f", *value) for value in listUv), _kComponentFloat, len(listUv), "VEC2", _kTargetArray)
    joints = writer.addAccessor(b"".join(struct.pack("<4H", *value) for value in listJoint), _kComponentUShort, len(listJoint), "VEC4", _kTargetArray)
    weights = writer.addAccessor(b"".join(struct.pack("<4f", *value) for value in listWeight), _kComponentFloat, len(listWeight), "VEC4", _kTargetArray)
    bWideIndex = len(listPosition) > 0xFFFF
    indices = writer.addAccessor(b"".join(struct.pack("<I" if bWideIndex else "<H", value) for value in listIndex), _kComponentUInt if bWideIndex else _kComponentUShort,
                                 len(listIndex), "SCALAR", _kTargetElementArray)
    matrixPayload = bytearray()
    for _name, _parent, world in listBone:
        matrixPayload.extend(struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -world[0], -world[1], -world[2], 1))
    inverseBind = writer.addAccessor(bytes(matrixPayload), _kComponentFloat, len(listBone), "MAT4")
    listOutNode: list[dict] = []
    for bone, (name, parent, world) in enumerate(listBone):
        parentWorld = listBone[parent][2] if parent >= 0 else (0.0, 0.0, 0.0)
        node: dict = {"name": name, "translation": [float(f"{world[axis] - parentWorld[axis]:.6f}") for axis in range(3)]}
        children = [child for child, (_childName, childParent, _childWorld) in enumerate(listBone) if childParent == bone]
        if children:
            node["children"] = children
        listOutNode.append(node)
    meshNode = len(listOutNode)
    listOutNode.append({"name": kOutputName, "mesh": 0, "skin": 0})
    while len(writer.data) % 4 != 0:
        writer.data.append(0)
    outDocument = {
        "asset": {"version": "2.0", "generator": "Scripts/dev/MakeSkinnedCape.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, meshNode]}],
        "nodes": listOutNode,
        "meshes": [{"name": kOutputName, "primitives": [{"attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": uv, "JOINTS_0": joints,
                                                                          "WEIGHTS_0": weights}, "indices": indices, "mode": 4}]}],
        "skins": [{"joints": list(range(len(listBone))), "inverseBindMatrices": inverseBind, "skeleton": 0}],
        "accessors": writer.listAccessor,
        "bufferViews": writer.listView,
        "buffers": [{"byteLength": len(writer.data)}],
    }
    jsonBytes = json.dumps(outDocument, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    jsonBytes += b" " * ((4 - len(jsonBytes) % 4) % 4)
    binaryBytes = bytes(writer.data)
    total = 12 + 8 + len(jsonBytes) + 8 + len(binaryBytes)
    return (struct.pack("<4sII", b"glTF", 2, total) + struct.pack("<I4s", len(jsonBytes), b"JSON") + jsonBytes + struct.pack("<I4s", len(binaryBytes), b"BIN\x00")
            + binaryBytes)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="기사 망토를 사슬 뼈 넷에 묶은 스킨드 glb(스프링 망토 원본)를 만듭니다")
    parser.add_argument("--out", default="", help="출력 폴더(기본: Resource/game/shooter3d/models_raw/kaykit)")
    args = parser.parse_args(argv)
    root = getProjectRoot()
    folder = pathlib.Path(args.out) if args.out else root / kDefaultFolder
    folder.mkdir(parents=True, exist_ok=True)
    data = makeSkinnedCape(root / kSourceGlb, root / kChainSkeleton)
    target = folder / f"{kOutputName}.glb"
    target.write_bytes(data)
    print(f"[MakeSkinnedCape] {target} ({len(data)} bytes)")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
