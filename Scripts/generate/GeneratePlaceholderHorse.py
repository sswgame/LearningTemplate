"""WildWest 승마의 자리 표시 말(네발, 상자로 만든 스킨드 메시)과 걸음새 클립 넷을 glTF 하나로 만듭니다.

  - `game/wildwest/models_raw/horse_placeholder.gltf` — 몸통 · 목 · 머리 · 다리 넷을 상자로 만들고 뼈마다 강체 스키닝(가중치 1)으로 묶습니다.
    뼈: Root · Body · Neck · Head · LegFL · LegFR · LegBL · LegBR · Saddle(기수 소켓 — 형상 없음). 앞은 +Z, 위는 +Y, 단위는 미터입니다.
  - 클립: Walk(4박 — 왼뒤 · 왼앞 · 오뒤 · 오앞) · Trot(대각 두 쌍) · Canter(3박) · Gallop(4박, 빠르게). 다리는 엉덩이 · 어깨 관절에서 X 축으로 흔들리고
    몸통은 걸음마다 위아래로 조금 흔들립니다. `MountMovementComponent::_gaitParameter`(애니메이터 Float, 0 Idle · 1 Walk · 2 Trot · 3 Canter · 4 Gallop)가
    이 클립들을 고르는 그래프(`data/horse.animgraph.xml`)를 몰고, 좌석(`VehicleSeatComponent::_seatOffset`)은 `Saddle` 뼈의 바인드 자리(0, 1.72, -0.05)입니다.
    (좌석 소켓 `_socketName` 은 외형 컴포넌트의 소켓이라 외형이 없는 말은 쓰지 않습니다.)

진짜 말 모델(CC0 걸음새 클립이 있는 것)이 들어오면 이 파일을 지우고 그 모델로 바꿉니다 — 크기 · 뼈 이름 · 클립 이름을 같게 두면 씬 · 그래프를 다시 쓰지 않습니다.
같은 입력이면 바이트까지 같은 파일이 나옵니다(버퍼는 base64 데이터 URI 로 파일 안에 둡니다).

사용법: py -3 Scripts/generate/GeneratePlaceholderHorse.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import base64
import json
import math
import struct
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)

kOutputPath = "Resource/game/wildwest/models_raw/horse_placeholder.gltf"
kSampleCount = 17            # 클립마다 키 수(처음과 끝이 같은 자세 — 고리)

# 뼈: 이름, 부모 번호(-1 이면 뿌리), 모델 공간 바인드 자리(미터).
kListBone = (
    ("Root", -1, (0.0, 0.0, 0.0)),
    ("Body", 0, (0.0, 1.3, 0.0)),
    ("Neck", 1, (0.0, 1.55, 0.85)),
    ("Head", 2, (0.0, 2.05, 1.1)),
    ("LegFL", 1, (0.25, 0.95, 0.7)),
    ("LegFR", 1, (-0.25, 0.95, 0.7)),
    ("LegBL", 1, (0.25, 0.95, -0.7)),
    ("LegBR", 1, (-0.25, 0.95, -0.7)),
    ("Saddle", 1, (0.0, 1.72, -0.05)),
)

# 상자: 묶을 뼈 번호, 최소 · 최대 모서리(모델 공간).
kListBox = (
    (1, (-0.35, 0.95, -0.9), (0.35, 1.7, 0.9)),       # 몸통
    (2, (-0.15, 1.45, 0.75), (0.15, 2.1, 1.15)),      # 목
    (3, (-0.15, 1.9, 1.0), (0.15, 2.2, 1.65)),        # 머리
    (4, (0.16, 0.0, 0.61), (0.34, 1.0, 0.79)),        # 앞 왼다리
    (5, (-0.34, 0.0, 0.61), (-0.16, 1.0, 0.79)),      # 앞 오른다리
    (6, (0.16, 0.0, -0.79), (0.34, 1.0, -0.61)),      # 뒤 왼다리
    (7, (-0.34, 0.0, -0.79), (-0.16, 1.0, -0.61)),    # 뒤 오른다리
)

# 걸음새: 이름, 한 주기(초), 다리 흔들기 각(라디안), 다리 위상(FL · FR · BL · BR, 0..1), 몸통 오르내림(미터).
kListGait = (
    ("Walk", 1.2, 0.30, (0.25, 0.75, 0.0, 0.5), 0.02),
    ("Trot", 0.7, 0.45, (0.0, 0.5, 0.5, 0.0), 0.05),
    ("Canter", 0.6, 0.60, (0.6, 0.4, 0.0, 0.4), 0.07),
    ("Gallop", 0.45, 0.75, (0.6, 0.5, 0.1, 0.0), 0.09),
)


def makeBoxGeometryInternal(boneIndex: int, minCorner: Sequence[float], maxCorner: Sequence[float],
                            vertexBase: int) -> tuple[list[tuple], list[tuple], list[int], list[int]]:
    """상자 하나의 면 여섯(면마다 정점 넷, 바깥 노멀)과 삼각형 열둘을 만듭니다. 감김은 glTF 규약(반시계가 앞면)입니다."""
    x0, y0, z0 = minCorner
    x1, y1, z1 = maxCorner
    faces = (
        ((1, 0, 0), ((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1))),
        ((-1, 0, 0), ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0))),
        ((0, 1, 0), ((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0))),
        ((0, -1, 0), ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1))),
        ((0, 0, 1), ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))),
        ((0, 0, -1), ((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0))),
    )
    listPosition, listNormal, listJoint, listIndex = [], [], [], []
    for normal, corners in faces:
        start = vertexBase + len(listPosition)
        for corner in corners:
            listPosition.append(corner)
            listNormal.append(normal)
            listJoint.append(boneIndex)
        listIndex.extend((start, start + 1, start + 2, start, start + 2, start + 3))
    return listPosition, listNormal, listJoint, listIndex


def makeLocalTranslationInternal(boneIndex: int) -> tuple[float, float, float]:
    """뼈의 부모 기준 자리입니다(바인드 자세는 회전 없음)."""
    _, parent, position = kListBone[boneIndex]
    if parent < 0:
        return position
    parentPosition = kListBone[parent][2]
    return tuple(round(position[axis] - parentPosition[axis], 6) for axis in range(3))


def makeAxisAngleQuaternionInternal(angle: float) -> tuple[float, float, float, float]:
    """X 축 둘레 회전 쿼터니언 (x, y, z, w) 입니다."""
    return (math.sin(angle * 0.5), 0.0, 0.0, math.cos(angle * 0.5))


class BufferWriterInternal:
    """버퍼 하나에 구간(bufferView)과 접근자(accessor)를 차례로 쌓습니다. 구간은 4 바이트 정렬입니다."""

    def __init__(self) -> None:
        self.bytes = bytearray()
        self.listView: list[dict] = []
        self.listAccessor: list[dict] = []

    def addAccessor(self, data: bytes, componentType: int, count: int, accessorType: str, target: int | None = None,
                    minValue: list | None = None, maxValue: list | None = None) -> int:
        while len(self.bytes) % 4 != 0:
            self.bytes.append(0)
        view = {"buffer": 0, "byteOffset": len(self.bytes), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.bytes.extend(data)
        self.listView.append(view)
        accessor = {"bufferView": len(self.listView) - 1, "componentType": componentType, "count": count, "type": accessorType}
        if minValue is not None:
            accessor["min"] = minValue
            accessor["max"] = maxValue
        self.listAccessor.append(accessor)
        return len(self.listAccessor) - 1


def makeHorseGltf() -> dict:
    """자리 표시 말 glTF 문서(dict)를 만듭니다."""
    writer = BufferWriterInternal()
    listPosition, listNormal, listJoint, listIndex = [], [], [], []
    for boneIndex, minCorner, maxCorner in kListBox:
        positions, normals, joints, indices = makeBoxGeometryInternal(boneIndex, minCorner, maxCorner, len(listPosition))
        listPosition += positions
        listNormal += normals
        listJoint += joints
        listIndex += indices

    kFloat, kUnsignedShort, kUnsignedByte = 5126, 5123, 5121
    kArrayBuffer, kElementArrayBuffer = 34962, 34963
    positionBytes = b"".join(struct.pack("<3f", *position) for position in listPosition)
    minPosition = [round(min(position[axis] for position in listPosition), 6) for axis in range(3)]
    maxPosition = [round(max(position[axis] for position in listPosition), 6) for axis in range(3)]
    positionAccessor = writer.addAccessor(positionBytes, kFloat, len(listPosition), "VEC3", kArrayBuffer, minPosition, maxPosition)
    normalAccessor = writer.addAccessor(b"".join(struct.pack("<3f", *normal) for normal in listNormal), kFloat, len(listNormal), "VEC3", kArrayBuffer)
    jointAccessor = writer.addAccessor(b"".join(struct.pack("<4B", joint, 0, 0, 0) for joint in listJoint), kUnsignedByte, len(listJoint), "VEC4", kArrayBuffer)
    weightAccessor = writer.addAccessor(b"".join(struct.pack("<4f", 1.0, 0.0, 0.0, 0.0) for _ in listJoint), kFloat, len(listJoint), "VEC4", kArrayBuffer)
    indexAccessor = writer.addAccessor(b"".join(struct.pack("<H", index) for index in listIndex), kUnsignedShort, len(listIndex), "SCALAR", kElementArrayBuffer)

    # 바인드 자세는 이동뿐이라 역바인드 행렬은 -자리 이동이다(열 우선).
    inverseBindBytes = b""
    for _, _, position in kListBone:
        inverseBindBytes += struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -position[0], -position[1], -position[2], 1)
    inverseBindAccessor = writer.addAccessor(inverseBindBytes, kFloat, len(kListBone), "MAT4")

    listNode = []
    for boneIndex, (name, _, _) in enumerate(kListBone):
        node = {"name": name, "translation": list(makeLocalTranslationInternal(boneIndex))}
        children = [childIndex for childIndex, bone in enumerate(kListBone) if bone[1] == boneIndex]
        if children:
            node["children"] = children
        listNode.append(node)
    meshNodeIndex = len(listNode)
    listNode.append({"name": "HorsePlaceholder", "mesh": 0, "skin": 0})

    # 클립: 키 시각은 모든 클립이 같은 비율(0..1)을 주기에 곱한 것이다.
    listAnimation = []
    legBones = (4, 5, 6, 7)
    for gaitName, period, swing, phases, bob in kListGait:
        times = [round(period * sample / (kSampleCount - 1), 6) for sample in range(kSampleCount)]
        timeAccessor = writer.addAccessor(b"".join(struct.pack("<f", time) for time in times), kFloat, kSampleCount, "SCALAR",
                                          minValue=[times[0]], maxValue=[times[-1]])
        listChannel, listSampler = [], []
        for legBone, phase in zip(legBones, phases):
            rotations = [makeAxisAngleQuaternionInternal(swing * math.sin(2.0 * math.pi * (sample / (kSampleCount - 1) + phase))) for sample in range(kSampleCount)]
            outputAccessor = writer.addAccessor(b"".join(struct.pack("<4f", *rotation) for rotation in rotations), kFloat, kSampleCount, "VEC4")
            listSampler.append({"input": timeAccessor, "output": outputAccessor, "interpolation": "LINEAR"})
            listChannel.append({"sampler": len(listSampler) - 1, "target": {"node": legBone, "path": "rotation"}})
        bodyRest = makeLocalTranslationInternal(1)
        translations = [(bodyRest[0], bodyRest[1] + bob * math.sin(4.0 * math.pi * sample / (kSampleCount - 1)), bodyRest[2]) for sample in range(kSampleCount)]
        bodyAccessor = writer.addAccessor(b"".join(struct.pack("<3f", *translation) for translation in translations), kFloat, kSampleCount, "VEC3")
        listSampler.append({"input": timeAccessor, "output": bodyAccessor, "interpolation": "LINEAR"})
        listChannel.append({"sampler": len(listSampler) - 1, "target": {"node": 1, "path": "translation"}})
        listAnimation.append({"name": gaitName, "channels": listChannel, "samplers": listSampler})

    return {
        "asset": {"version": "2.0", "generator": "Scripts/generate/GeneratePlaceholderHorse.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, meshNodeIndex]}],
        "nodes": listNode,
        "meshes": [{"name": "HorsePlaceholder", "primitives": [{
            "attributes": {"POSITION": positionAccessor, "NORMAL": normalAccessor, "JOINTS_0": jointAccessor, "WEIGHTS_0": weightAccessor},
            "indices": indexAccessor, "material": 0}]}],
        "materials": [{"name": "HorseCoat", "pbrMetallicRoughness": {"baseColorFactor": [0.42, 0.26, 0.15, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.9}}],
        "skins": [{"name": "HorseSkeleton", "joints": list(range(len(kListBone))), "inverseBindMatrices": inverseBindAccessor, "skeleton": 0}],
        "animations": listAnimation,
        "buffers": [{"byteLength": len(writer.bytes), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(writer.bytes)).decode("ascii")}],
        "bufferViews": writer.listView,
        "accessors": writer.listAccessor,
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]), help="저장소 루트")
    args = parser.parse_args(argv)
    outputPath = Path(args.root) / kOutputPath
    outputPath.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(makeHorseGltf(), ensure_ascii=False, indent=1, sort_keys=False) + "\n"
    outputPath.write_text(text, encoding="utf-8", newline="\n")
    print(f"[GeneratePlaceholderHorse] wrote {kOutputPath} ({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
