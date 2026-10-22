#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
애니메이션 비용 측정용 합성 리그(스킨드 glTF `.glb` + 후처리 리그 `.rig.xml`)를 만듭니다.

저장소의 캐릭터 스켈레톤은 관절이 수십 개라 스키닝 · 리그 노드 · 군중 비용을 실제 규모(수백 관절)로 잴 수 없다. 눈에 보이는 품질은
CC0 리소스가, 비용 측정은 이 도구가 만든 합성 데이터가 맡는다(라이선스 문제 없음, 본 수 · 위상 · 모프 · 클립을 통제한다).

만드는 것:
  <out>.glb       스킨 · 관절 · 애니메이션(관절마다 회전 키, 메시에 모프 가중치) · 모프 타깃을 포함한 glTF 2.0 바이너리.
                  엔진 임포터(`ModelImporter::readModelAsset`)가 읽는 범위 안이다 — 스킨 하나, 삼각형 프리미티브 하나,
                  JOINTS_0(u16) · WEIGHTS_0(f32) 영향 4 개, 조밀한 모프 POSITION, 선형 보간 샘플러.
  <out>.rig.xml   같은 스켈레톤의 후처리 리그(`RigAsset`) — 다리마다 TwoBoneIK(대상은 루트 본 기준 발 위치), 꼬리 · 머리 ·
                  손가락 · 뱀 몸통 사슬마다 SpringChain. 좌표는 엔진 공간(glTF 의 X 를 뒤집는다).

위상(topology): 뿌리 본 하나에 사슬(척추 · 머리 · 팔 · 다리 · 손가락 · 꼬리)을 붙인다. 사슬마다 최소 본 수(다리는 IK 라 3)를 먼저
주고 남는 본을 사슬 무게 비율로 나눈다(최대 나머지 법, 동률은 사슬 순서) — 본 수가 인자와 정확히 같다.
  biped      척추 · 머리 · 팔 둘 · 다리 둘 · 손가락 여섯(최소 17 본)
  quadruped  가로 척추 · 목머리 · 꼬리 · 다리 네 개(최소 16 본)
  multileg   긴 척추 · 머리 · 다리 여덟(최소 27 본)
  serpent    가로 척추 하나(최소 2 본)

메시: 사슬마다 본 사이를 잇는 원통(한 고리 8 정점, 본마다 `vertices-per-bone / 8` 고리, 끝 고리 하나 더). 정점의 영향 네 개는
사슬을 따라 가까운 본 네 개(자기 · 부모 · 자식 · 조부모 순으로 모자라면 위로)이고, 가중치는 거리로 줄여 합이 1 이다. 메시 정점 수는
(본 수 × vertices-per-bone + 사슬 수 × 8) 근처로 정해진다 — 정점/본과 메시 정점 수를 따로 받으면 서로 모순이라 하나만 받는다.

클립: 관절마다 회전 키를 서로 다른 위상의 사인으로 채운다(축은 사슬 방향에 수직인 씨앗 방향, 진폭 0.35 rad × (1 + 0.25 × 클립 번호),
클립 길이에 클립 번호 + 1 주기가 들어가 이음매 없이 돈다). 모든 값이 움직여야 압축 · 평가 비용이 거짓으로 작지 않다.
모프 가중치도 클립마다 타깃별 위상의 사인이다. 모프 타깃 k 는 사슬 (k mod 사슬 수)의 정점을 반지름 방향으로 민다.

결과는 결정적이다(정수 해시 씨앗, 실수는 소수 여섯째 자리에서 잘라 libm 차이가 바이트로 번지지 않게 한다) — 같은 인자 = 같은 바이트.
인자가 범위를 벗어나면 고치지 않고 오류로 멈춘다.

사용법:
  py -3 Scripts/dev/MakeSyntheticRig.py --topology biped --bones 150 --out C:/tmp/biped_b150.glb
  py -3 Scripts/dev/MakeSyntheticRig.py --representatives        # 대표 네 개를 Resource/game/empty/rigs_raw/ 에 다시 쓴다
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import struct
import sys
from dataclasses import dataclass, field
from xml.sax.saxutils import quoteattr

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common.GltfBufferWriter import BufferWriter
from common.Paths import getProjectRoot

#: 위상 이름 — 인자 `--topology` 가 고른다.
kTopologies = ("biped", "quadruped", "multileg", "serpent")
#: 본 수 상한 — 임포터는 16 비트 본 번호까지 받지만, 측정 범위(수백)를 넘는 값은 오타로 본다.
kMaxBones = 1024
#: 모프 타깃 · 클립 수 상한.
kMaxMorphs = 64
kMaxClips = 32
#: 클립 길이 범위(초)와 키 표본율(Hz).
kMinClipSeconds = 0.1
kMaxClipSeconds = 60.0
kSampleRate = 30
#: 원통 고리 하나의 정점 수 — 정점/본은 이것의 배수다.
kRingSides = 8
kMaxVerticesPerBone = kRingSides * 16
#: 정점 하나가 받는 본 영향 수(glTF JOINTS_0 / WEIGHTS_0 의 한 벌).
kInfluenceCount = 4
#: 회전 진폭(라디안)과 모프 변위(m).
kRotationAmplitude = 0.35
kMorphOffset = 0.02
#: 대표 리그 — `--representatives` 가 쓰는 것, 저장소에 커밋하는 것, 벤치 · 임포트 테스트가 읽는 것이다.
kRepresentativeFolder = "game/empty/rigs_raw"
kRepresentatives = (("biped", 20), ("biped", 150), ("quadruped", 60), ("multileg", 300))
kRepresentativeMorphs = 2
kRepresentativeClips = 2
kRepresentativeClipSeconds = 1.0
kRepresentativeVerticesPerBone = 8
kRepresentativeSeed = 1

# glTF 상수.
_kComponentFloat = 5126
_kComponentShort = 5122
_kComponentUShort = 5123
_kComponentUInt = 5125
_kTargetArray = 34962
_kTargetElementArray = 34963


@dataclass(frozen=True)
class RigOptions:
    """생성 인자 한 벌. 검사는 `validateOptions` 가 한다."""
    topology: str
    bones: int
    morphs: int
    clips: int
    clipSeconds: float
    verticesPerBone: int
    seed: int


@dataclass(frozen=True)
class ChainSpec:
    """사슬 하나의 형태. `anchor` 는 'root', 'end:<사슬>'(그 사슬 끝점), '<사슬>@<비율>'(그 사슬의 본) 중 하나다."""
    name: str
    anchor: str
    offset: tuple[float, float, float]
    direction: tuple[float, float, float]
    length: float
    weight: float
    minBones: int
    kind: str      # spine · leg · limb · spring
    radius: float


@dataclass
class Chain:
    """본을 배정받은 사슬."""
    spec: ChainSpec
    listBone: list[int] = field(default_factory=list)
    start: tuple[float, float, float] = (0.0, 0.0, 0.0)
    end: tuple[float, float, float] = (0.0, 0.0, 0.0)


@dataclass
class Skeleton:
    """본 이름 · 부모 · 월드 레스트 위치(모든 레스트 회전은 단위라 위치만으로 바인드가 정해진다)."""
    listName: list[str] = field(default_factory=list)
    listParent: list[int] = field(default_factory=list)
    listPosition: list[tuple[float, float, float]] = field(default_factory=list)
    listChain: list[Chain] = field(default_factory=list)


def normalizeInternal(vector: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2])
    return (vector[0] / length, vector[1] / length, vector[2] / length)


def addInternal(a: tuple[float, float, float], b: tuple[float, float, float], scale: float = 1.0) -> tuple[float, float, float]:
    return (a[0] + b[0] * scale, a[1] + b[1] * scale, a[2] + b[2] * scale)


def crossInternal(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def roundInternal(value: float) -> float:
    """소수 여섯째 자리에서 자른다 — 플랫폼 libm 의 마지막 비트 차이가 바이트로 번지지 않게 한다."""
    rounded = round(value, 6)
    return 0.0 if rounded == 0.0 else rounded


def hashUnitInternal(seed: int, a: int, b: int) -> float:
    """정수 셋 → [0, 1). 플랫폼과 무관한 정수 연산만 쓴다."""
    value = (a * 374761393 + b * 668265263 + seed * 1442695041 + 0x9E3779B9) & 0xFFFFFFFF
    value = ((value ^ (value >> 13)) * 1274126177) & 0xFFFFFFFF
    value = value ^ (value >> 16)
    return value / 4294967296.0


def makeChainSpecsInternal(topology: str) -> list[ChainSpec]:
    """위상의 사슬 목록(glTF 공간, Y 위, 단위 m). 부모 사슬이 앞에 온다."""
    up = (0.0, 1.0, 0.0)
    down = (0.0, -1.0, 0.0)
    forward = (0.0, 0.0, 1.0)
    if topology == "biped":
        listSpec = [
            ChainSpec("spine", "root", (0.0, 1.0, 0.0), up, 0.6, 4.0, 1, "spine", 0.15),
            ChainSpec("head", "end:spine", (0.0, 0.05, 0.0), up, 0.3, 1.0, 1, "spring", 0.1),
            ChainSpec("arm_l", "spine@0.9", (0.15, 0.0, 0.0), (1.0, 0.0, 0.0), 0.6, 2.0, 1, "limb", 0.05),
            ChainSpec("arm_r", "spine@0.9", (-0.15, 0.0, 0.0), (-1.0, 0.0, 0.0), 0.6, 2.0, 1, "limb", 0.05),
            ChainSpec("leg_l", "root", (0.1, 0.95, 0.0), down, 0.9, 2.0, 3, "leg", 0.07),
            ChainSpec("leg_r", "root", (-0.1, 0.95, 0.0), down, 0.9, 2.0, 3, "leg", 0.07),
        ]
        for side, sign in (("l", 1.0), ("r", -1.0)):
            for finger in range(3):
                listSpec.append(ChainSpec(f"finger_{side}{finger}", f"end:arm_{side}", (0.03 * sign, 0.0, 0.03 * (finger - 1)),
                                          (sign, 0.0, 0.0), 0.12, 0.5, 1, "spring", 0.012))
        return listSpec
    if topology == "quadruped":
        listSpec = [
            ChainSpec("spine", "root", (0.0, 0.8, -0.5), forward, 1.0, 4.0, 1, "spine", 0.18),
            ChainSpec("head", "end:spine", (0.0, 0.05, 0.05), normalizeInternal((0.0, 1.0, 1.0)), 0.5, 2.0, 1, "spring", 0.08),
            ChainSpec("tail", "spine@0.0", (0.0, 0.0, -0.05), (0.0, 0.0, -1.0), 0.6, 2.0, 1, "spring", 0.04),
        ]
        for name, fraction, sign in (("leg_fl", 1.0, 1.0), ("leg_fr", 1.0, -1.0), ("leg_bl", 0.0, 1.0), ("leg_br", 0.0, -1.0)):
            listSpec.append(ChainSpec(name, f"spine@{fraction}", (0.15 * sign, -0.05, 0.0), down, 0.75, 2.0, 3, "leg", 0.06))
        return listSpec
    if topology == "multileg":
        listSpec = [
            ChainSpec("spine", "root", (0.0, 0.6, -1.0), forward, 2.0, 4.0, 1, "spine", 0.2),
            ChainSpec("head", "end:spine", (0.0, 0.0, 0.05), forward, 0.4, 1.0, 1, "spring", 0.12),
        ]
        for pair, fraction in enumerate((0.1, 0.37, 0.63, 0.9)):
            for side, sign in (("l", 1.0), ("r", -1.0)):
                listSpec.append(ChainSpec(f"leg_{pair}{side}", f"spine@{fraction}", (0.2 * sign, 0.0, 0.0),
                                          normalizeInternal((sign, -1.0, 0.0)), 0.8, 2.0, 3, "leg", 0.04))
        return listSpec
    if topology == "serpent":
        return [ChainSpec("spine", "root", (0.0, 0.1, -2.0), forward, 4.0, 1.0, 1, "spring", 0.08)]
    raise ValueError(f"unknown topology '{topology}' (one of {', '.join(kTopologies)})")


def getMinimumBones(topology: str) -> int:
    """위상이 받는 가장 작은 본 수(뿌리 하나 + 사슬 최소 본 수의 합)."""
    return 1 + sum(spec.minBones for spec in makeChainSpecsInternal(topology))


def validateOptions(options: RigOptions) -> None:
    """범위 밖 인자는 고치지 않고 ValueError 로 멈춘다."""
    if options.topology not in kTopologies:
        raise ValueError(f"--topology '{options.topology}' is not one of {', '.join(kTopologies)}")
    minimum = getMinimumBones(options.topology)
    if not minimum <= options.bones <= kMaxBones:
        raise ValueError(f"--bones {options.bones} is outside [{minimum}, {kMaxBones}] for topology '{options.topology}'")
    if not 0 <= options.morphs <= kMaxMorphs:
        raise ValueError(f"--morphs {options.morphs} is outside [0, {kMaxMorphs}]")
    if not 1 <= options.clips <= kMaxClips:
        raise ValueError(f"--clips {options.clips} is outside [1, {kMaxClips}]")
    if not kMinClipSeconds <= options.clipSeconds <= kMaxClipSeconds:
        raise ValueError(f"--clip-seconds {options.clipSeconds} is outside [{kMinClipSeconds}, {kMaxClipSeconds}]")
    if options.verticesPerBone <= 0 or options.verticesPerBone % kRingSides != 0 or options.verticesPerBone > kMaxVerticesPerBone:
        raise ValueError(f"--vertices-per-bone {options.verticesPerBone} must be a multiple of {kRingSides} in [{kRingSides}, {kMaxVerticesPerBone}]")
    if not 0 <= options.seed <= 0xFFFFFFFF:
        raise ValueError(f"--seed {options.seed} is outside the 32-bit range")


def allocateBonesInternal(listSpec: list[ChainSpec], boneCount: int) -> list[int]:
    """사슬마다 최소 본 수를 주고 남는 본을 무게 비율로 나눈다(최대 나머지 법, 동률은 앞 사슬)."""
    listCount = [spec.minBones for spec in listSpec]
    remaining = boneCount - 1 - sum(listCount)
    totalWeight = sum(spec.weight for spec in listSpec)
    listShare = [remaining * spec.weight / totalWeight for spec in listSpec]
    for index, share in enumerate(listShare):
        listCount[index] += int(math.floor(share))
    leftover = boneCount - 1 - sum(listCount)
    order = sorted(range(len(listSpec)), key=lambda index: (-(listShare[index] - math.floor(listShare[index])), index))
    for index in order[:leftover]:
        listCount[index] += 1
    return listCount


def buildSkeleton(options: RigOptions) -> Skeleton:
    """위상과 본 수로 스켈레톤을 만든다. 본 순서는 뿌리 → 사슬 순서 → 사슬 안 순서(부모가 앞)."""
    listSpec = makeChainSpecsInternal(options.topology)
    listCount = allocateBonesInternal(listSpec, options.bones)
    skeleton = Skeleton()
    skeleton.listName.append("root")
    skeleton.listParent.append(-1)
    skeleton.listPosition.append((0.0, 0.0, 0.0))
    mapChain: dict[str, Chain] = {}
    for spec, count in zip(listSpec, listCount):
        if spec.anchor == "root":
            parentBone = 0
            anchorPosition = skeleton.listPosition[0]
        elif spec.anchor.startswith("end:"):
            parentChain = mapChain[spec.anchor[4:]]
            parentBone = parentChain.listBone[-1]
            anchorPosition = parentChain.end
        else:
            chainName, fractionText = spec.anchor.split("@")
            parentChain = mapChain[chainName]
            parentBone = parentChain.listBone[int(round(float(fractionText) * (len(parentChain.listBone) - 1)))]
            anchorPosition = skeleton.listPosition[parentBone]
        chain = Chain(spec)
        chain.start = addInternal(anchorPosition, spec.offset)
        chain.end = addInternal(chain.start, spec.direction, spec.length)
        previous = parentBone
        for index in range(count):
            position = tuple(roundInternal(value) for value in addInternal(chain.start, spec.direction, spec.length * index / count))
            boneIndex = len(skeleton.listName)
            skeleton.listName.append(f"{spec.name}_{index:02d}")
            skeleton.listParent.append(previous)
            skeleton.listPosition.append(position)
            chain.listBone.append(boneIndex)
            previous = boneIndex
        mapChain[spec.name] = chain
        skeleton.listChain.append(chain)
    if len(skeleton.listName) != options.bones:
        raise ValueError(f"internal: built {len(skeleton.listName)} bones for {options.bones}")
    return skeleton


def makeFrameInternal(direction: tuple[float, float, float]) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    """사슬 방향에 수직인 단위 벡터 둘."""
    helper = (0.0, 1.0, 0.0) if abs(direction[1]) < 0.9 else (1.0, 0.0, 0.0)
    first = normalizeInternal(crossInternal(direction, helper))
    second = crossInternal(direction, first)
    return first, second


def findInfluencesInternal(skeleton: Skeleton, chain: Chain, along: float) -> list[tuple[int, float]]:
    """사슬을 따라 `along`(본 단위 위치)에서 가까운 본 네 개와 가중치(합 1)."""
    count = len(chain.listBone)
    owner = min(int(math.floor(along)), count - 1)
    listCandidate: list[tuple[int, float]] = []
    # 사슬 안의 본은 위치 차, 사슬 밖(부모 쪽)은 사슬 시작에서 본 하나씩 멀어진다.
    for offset in (0, -1, 1, -2, 2, -3, 3):
        slot = owner + offset
        if 0 <= slot < count:
            listCandidate.append((chain.listBone[slot], abs(along - (slot + 0.5))))
    ancestor = skeleton.listParent[chain.listBone[0]]
    distance = along + 0.5
    while ancestor >= 0:
        listCandidate.append((ancestor, distance))
        ancestor = skeleton.listParent[ancestor]
        distance += 1.0
    listCandidate.sort(key=lambda item: (item[1], item[0]))
    listChosen = listCandidate[:kInfluenceCount]
    listWeight = [1.0 / (1.0 + 4.0 * distance * distance) for _, distance in listChosen]
    total = sum(listWeight)
    listResult = [(bone, roundInternal(weight / total)) for (bone, _), weight in zip(listChosen, listWeight)]
    while len(listResult) < kInfluenceCount:
        listResult.append((0, 0.0))
    # 잘린 반올림의 나머지를 첫 영향에 둔다 — 합이 정확히 1 이다(f32 로 다시 정규화하지 않아도 된다).
    first = roundInternal(1.0 - sum(weight for _, weight in listResult[1:]))
    return [(listResult[0][0], first)] + listResult[1:]


@dataclass
class MeshData:
    listPosition: list[tuple[float, float, float]] = field(default_factory=list)
    listNormal: list[tuple[float, float, float]] = field(default_factory=list)
    listJoint: list[tuple[int, int, int, int]] = field(default_factory=list)
    listWeight: list[tuple[float, float, float, float]] = field(default_factory=list)
    listIndex: list[int] = field(default_factory=list)
    listMorph: list[list[tuple[float, float, float]]] = field(default_factory=list)


def buildMesh(skeleton: Skeleton, options: RigOptions) -> MeshData:
    """사슬마다 원통을 두르고, 모프 타깃 k 는 사슬 (k mod 사슬 수)의 정점을 반지름 방향으로 민다."""
    mesh = MeshData()
    ringsPerBone = options.verticesPerBone // kRingSides
    listChainRange: list[tuple[int, int]] = []
    for chain in skeleton.listChain:
        spec = chain.spec
        first, second = makeFrameInternal(spec.direction)
        boneCount = len(chain.listBone)
        ringCount = boneCount * ringsPerBone + 1
        base = len(mesh.listPosition)
        for ring in range(ringCount):
            along = ring / ringsPerBone
            center = addInternal(chain.start, spec.direction, spec.length * along / boneCount)
            listInfluence = findInfluencesInternal(skeleton, chain, min(along, boneCount - 1e-6))
            for side in range(kRingSides):
                angle = 2.0 * math.pi * side / kRingSides
                radial = addInternal(tuple(value * math.cos(angle) for value in first), second, math.sin(angle))
                mesh.listPosition.append(tuple(roundInternal(value) for value in addInternal(center, radial, spec.radius)))
                mesh.listNormal.append(tuple(roundInternal(value) for value in radial))
                mesh.listJoint.append(tuple(bone for bone, _ in listInfluence))
                mesh.listWeight.append(tuple(weight for _, weight in listInfluence))
        for ring in range(ringCount - 1):
            for side in range(kRingSides):
                nextSide = (side + 1) % kRingSides
                a = base + ring * kRingSides + side
                b = base + ring * kRingSides + nextSide
                c = base + (ring + 1) * kRingSides + side
                d = base + (ring + 1) * kRingSides + nextSide
                mesh.listIndex.extend((a, c, b, b, c, d))
        listChainRange.append((base, len(mesh.listPosition)))
    for target in range(options.morphs):
        chainIndex = target % len(skeleton.listChain)
        begin, end = listChainRange[chainIndex]
        listDelta = [(0.0, 0.0, 0.0)] * len(mesh.listPosition)
        for vertex in range(begin, end):
            normal = mesh.listNormal[vertex]
            listDelta[vertex] = tuple(roundInternal(value * kMorphOffset) for value in normal)
        mesh.listMorph.append(listDelta)
    return mesh


def quaternionInternal(axis: tuple[float, float, float], angle: float) -> tuple[float, float, float, float]:
    half = angle * 0.5
    sine = math.sin(half)
    return (roundInternal(axis[0] * sine), roundInternal(axis[1] * sine), roundInternal(axis[2] * sine), roundInternal(math.cos(half)))


def makeGltfInternal(skeleton: Skeleton, mesh: MeshData, options: RigOptions) -> tuple[dict, bytes]:
    writer = BufferWriter()
    vertexCount = len(mesh.listPosition)
    position = writer.addVec3(mesh.listPosition, _kTargetArray)
    normal = writer.addVec3(mesh.listNormal, _kTargetArray)
    jointPayload = b"".join(struct.pack("<4H", *joints) for joints in mesh.listJoint)
    joints = writer.addAccessor(jointPayload, _kComponentUShort, vertexCount, "VEC4", _kTargetArray)
    weightPayload = b"".join(struct.pack("<4f", *weights) for weights in mesh.listWeight)
    weights = writer.addAccessor(weightPayload, _kComponentFloat, vertexCount, "VEC4", _kTargetArray)
    bWideIndex = vertexCount > 0xFFFF
    indexPayload = b"".join(struct.pack("<I" if bWideIndex else "<H", index) for index in mesh.listIndex)
    indices = writer.addAccessor(indexPayload, _kComponentUInt if bWideIndex else _kComponentUShort, len(mesh.listIndex), "SCALAR",
                                 _kTargetElementArray)
    listTarget = [{"POSITION": writer.addVec3(delta, _kTargetArray)} for delta in mesh.listMorph]

    boneCount = len(skeleton.listName)
    matrixPayload = bytearray()
    for bone in range(boneCount):
        x, y, z = skeleton.listPosition[bone]
        # 레스트 회전이 모두 단위라 역 바인드는 월드 위치의 역 이동이다(열 우선).
        matrixPayload.extend(struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1))
    inverseBind = writer.addAccessor(bytes(matrixPayload), _kComponentFloat, boneCount, "MAT4")

    listNode: list[dict] = []
    listChildren: list[list[int]] = [[] for _ in range(boneCount)]
    for bone in range(1, boneCount):
        listChildren[skeleton.listParent[bone]].append(bone)
    for bone in range(boneCount):
        parent = skeleton.listParent[bone]
        parentPosition = skeleton.listPosition[parent] if parent >= 0 else (0.0, 0.0, 0.0)
        local = [roundInternal(skeleton.listPosition[bone][axis] - parentPosition[axis]) for axis in range(3)]
        node: dict = {"name": skeleton.listName[bone], "translation": local}
        if listChildren[bone]:
            node["children"] = listChildren[bone]
        listNode.append(node)
    meshNode = len(listNode)
    listNode.append({"name": "body", "mesh": 0, "skin": 0})

    # 관절마다 사슬 방향에 수직인 씨앗 축 하나. 클립마다 위상 · 진폭 · 주기가 다르다.
    listAxis: list[tuple[float, float, float]] = [(0.0, 1.0, 0.0)] * boneCount
    for chain in skeleton.listChain:
        first, second = makeFrameInternal(chain.spec.direction)
        for bone in chain.listBone:
            theta = 2.0 * math.pi * hashUnitInternal(options.seed, bone, 7919)
            listAxis[bone] = normalizeInternal(addInternal(tuple(value * math.cos(theta) for value in first), second, math.sin(theta)))
    keyCount = int(round(options.clipSeconds * kSampleRate)) + 1
    listAnimation: list[dict] = []
    for clip in range(options.clips):
        listTime = [roundInternal(options.clipSeconds * key / (keyCount - 1)) for key in range(keyCount)]
        timeBounds = ([listTime[0]], [struct.unpack("<f", struct.pack("<f", listTime[-1]))[0]])
        listPart: list[tuple[bytes, int, int, str, bool, tuple[list[float], list[float]] | None]] = [
            (b"".join(struct.pack("<f", value) for value in listTime), _kComponentFloat, keyCount, "SCALAR", False, timeBounds)]
        amplitude = kRotationAmplitude * (1.0 + 0.25 * clip)
        cycles = clip + 1
        for bone in range(boneCount):
            phase = 2.0 * math.pi * hashUnitInternal(options.seed, bone, 104729 + clip)
            payload = bytearray()
            for key in range(keyCount):
                angle = amplitude * math.sin(2.0 * math.pi * cycles * key / (keyCount - 1) + phase)
                # 회전 출력은 표준이 허용하는 정규화 short(오차 3e-5)다 — f32 의 절반 크기라 대표 리그가 커밋 한도(1 MB) 안에 들어간다.
                payload.extend(struct.pack("<4h", *(int(round(value * 32767.0)) for value in quaternionInternal(listAxis[bone], angle))))
            listPart.append((bytes(payload), _kComponentShort, keyCount, "VEC4", True, None))
        if options.morphs > 0:
            payload = bytearray()
            for key in range(keyCount):
                for target in range(options.morphs):
                    phase = 2.0 * math.pi * hashUnitInternal(options.seed, target, 15485863 + clip)
                    value = roundInternal(0.5 + 0.5 * math.sin(2.0 * math.pi * cycles * key / (keyCount - 1) + phase))
                    payload.extend(struct.pack("<f", value))
            listPart.append((bytes(payload), _kComponentFloat, keyCount * options.morphs, "SCALAR", False, None))
        listAccessor = writer.addPackedAccessors(listPart)
        timeAccessor = listAccessor[0]
        listSampler = [{"input": timeAccessor, "output": output, "interpolation": "LINEAR"} for output in listAccessor[1:]]
        listChannel: list[dict] = [{"sampler": bone, "target": {"node": bone, "path": "rotation"}} for bone in range(boneCount)]
        if options.morphs > 0:
            listChannel.append({"sampler": boneCount, "target": {"node": meshNode, "path": "weights"}})
        listAnimation.append({"name": f"clip_{clip:02d}", "samplers": listSampler, "channels": listChannel})

    primitive: dict = {"attributes": {"POSITION": position, "NORMAL": normal, "JOINTS_0": joints, "WEIGHTS_0": weights},
                       "indices": indices, "mode": 4}
    meshEntry: dict = {"name": "body", "primitives": [primitive]}
    if listTarget:
        primitive["targets"] = listTarget
        meshEntry["weights"] = [0.0] * len(listTarget)
        meshEntry["extras"] = {"targetNames": [f"morph_{target:02d}" for target in range(len(listTarget))]}
    while len(writer.data) % 4 != 0:
        writer.data.append(0)
    document = {
        "asset": {"version": "2.0", "generator": "Scripts/dev/MakeSyntheticRig.py",
                  "extras": {"topology": options.topology, "bones": options.bones, "morphs": options.morphs, "clips": options.clips,
                             "clip_seconds": options.clipSeconds, "vertices_per_bone": options.verticesPerBone, "seed": options.seed,
                             "mesh_vertices": vertexCount}},
        "scene": 0,
        "scenes": [{"nodes": [0, meshNode]}],
        "nodes": listNode,
        "meshes": [meshEntry],
        "skins": [{"joints": list(range(boneCount)), "inverseBindMatrices": inverseBind, "skeleton": 0}],
        "animations": listAnimation,
        "accessors": writer.listAccessor,
        "bufferViews": writer.listView,
        "buffers": [{"byteLength": len(writer.data)}],
    }
    return document, bytes(writer.data)


def encodeGlbInternal(document: dict, binary: bytes) -> bytes:
    """glb 컨테이너 — 헤더, JSON 청크(공백 채움), BIN 청크(0 채움)."""
    jsonBytes = json.dumps(document, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    jsonBytes += b" " * ((4 - len(jsonBytes) % 4) % 4)
    total = 12 + 8 + len(jsonBytes) + 8 + len(binary)
    header = struct.pack("<4sII", b"glTF", 2, total)
    return header + struct.pack("<I4s", len(jsonBytes), b"JSON") + jsonBytes + struct.pack("<I4s", len(binary), b"BIN\x00") + binary


def makeRigDocument(skeleton: Skeleton) -> dict:
    """후처리 리그(`RigAsset` 형식, 엔진 공간). 다리는 TwoBoneIK, 스프링 사슬(본 둘 이상)은 SpringChain."""
    listTarget: list[dict] = []
    listNode: list[dict] = []
    for chain in skeleton.listChain:
        listName = [skeleton.listName[bone] for bone in chain.listBone]
        if chain.spec.kind == "leg":
            # 발 위치(셋째 관절의 끝)를 루트 기준으로 0.1 m 올린 곳 — 다리가 늘 굽는다. glTF → 엔진은 X 를 뒤집는다.
            foot = addInternal(chain.start, chain.spec.direction, chain.spec.length * 3 / len(chain.listBone))
            translation = [roundInternal(-foot[0]), roundInternal(foot[1] + 0.1), roundInternal(foot[2])]
            targetName = f"foot_{chain.spec.name}"
            listTarget.append({"name": targetName, "bone": "root", "translation": translation})
            listNode.append({"type": "TwoBoneIK", "name": f"ik_{chain.spec.name}", "root": listName[0], "mid": listName[1], "end": listName[2],
                             "target": targetName})
        elif chain.spec.kind == "spring" and len(listName) >= 2:
            listNode.append({"type": "SpringChain", "name": f"spring_{chain.spec.name}", "bones": listName})
    return {"targets": listTarget, "nodes": listNode}


def makeRigXmlInternal(document: dict) -> str:
    """`RigAsset` XML 글. 값은 속성(벡터는 `x,y,z`), 이름 목록은 칸 이름의 요소 안 `<item>` 이다(엔진 `RigXMLReader` 의 배치)."""
    listLine: list[str] = ["<RigAsset>"]
    for section in ("targets", "nodes"):
        listLine.append(f"\t<{section}>")
        for entry in document[section]:
            listAttribute: list[str] = []
            listChild: list[tuple[str, list[str]]] = []
            for key, value in entry.items():
                if isinstance(value, list) and all(isinstance(element, str) for element in value):
                    listChild.append((key, value))
                elif isinstance(value, list):
                    listAttribute.append(f"{key}={quoteattr(','.join(repr(element) for element in value))}")
                else:
                    listAttribute.append(f"{key}={quoteattr(str(value))}")
            attributeText = (" " + " ".join(listAttribute)) if listAttribute else ""
            if not listChild:
                listLine.append(f"\t\t<item{attributeText} />")
                continue
            listLine.append(f"\t\t<item{attributeText}>")
            for key, listName in listChild:
                listLine.append(f"\t\t\t<{key}>")
                listLine.extend(f"\t\t\t\t<item>{name}</item>" for name in listName)
                listLine.append(f"\t\t\t</{key}>")
            listLine.append("\t\t</item>")
        listLine.append(f"\t</{section}>")
    listLine.append("</RigAsset>")
    return "\n".join(listLine) + "\n"


def makeRig(options: RigOptions) -> tuple[bytes, bytes]:
    """(glb 바이트, rig.xml 바이트). 같은 인자는 같은 바이트다."""
    validateOptions(options)
    skeleton = buildSkeleton(options)
    mesh = buildMesh(skeleton, options)
    document, binary = makeGltfInternal(skeleton, mesh, options)
    rigText = makeRigXmlInternal(makeRigDocument(skeleton))
    return encodeGlbInternal(document, binary), rigText.encode("ascii")


def makeRepresentativeName(topology: str, bones: int) -> str:
    """대표 리그의 파일 줄기(`biped_b020`)."""
    return f"{topology}_b{bones:03d}"


def makeRepresentativeOptions(topology: str, bones: int) -> RigOptions:
    return RigOptions(topology, bones, kRepresentativeMorphs, kRepresentativeClips, kRepresentativeClipSeconds, kRepresentativeVerticesPerBone,
                      kRepresentativeSeed)


def writeRig(outPath: pathlib.Path, options: RigOptions) -> tuple[int, int]:
    """`<out>.glb` 와 옆 `<out>.rig.xml` 을 쓰고 두 크기를 돌려줍니다."""
    if outPath.suffix != ".glb":
        raise ValueError(f"--out '{outPath}' must end with .glb")
    glbBytes, rigBytes = makeRig(options)
    outPath.parent.mkdir(parents=True, exist_ok=True)
    outPath.write_bytes(glbBytes)
    outPath.with_suffix(".rig.xml").write_bytes(rigBytes)
    return len(glbBytes), len(rigBytes)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="애니메이션 비용 측정용 합성 리그(.glb + .rig.xml)를 결정적으로 만든다")
    parser.add_argument("--representatives", action="store_true", help=f"대표 네 개를 Resource/{kRepresentativeFolder}/ 에 쓴다(--out-dir 만 함께 받는다)")
    parser.add_argument("--out-dir", type=pathlib.Path, default=None, help="--representatives 의 출력 폴더(기본: Resource/" + kRepresentativeFolder + ")")
    parser.add_argument("--topology", choices=kTopologies, default=None)
    parser.add_argument("--bones", type=int, default=None, help=f"본 수(위상 최소 ~ {kMaxBones})")
    parser.add_argument("--morphs", type=int, default=0, help=f"모프 타깃 수(0 ~ {kMaxMorphs})")
    parser.add_argument("--clips", type=int, default=1, help=f"클립 수(1 ~ {kMaxClips})")
    parser.add_argument("--clip-seconds", type=float, default=1.0, help=f"클립 길이(초, {kMinClipSeconds} ~ {kMaxClipSeconds}), 키는 {kSampleRate} Hz")
    parser.add_argument("--vertices-per-bone", type=int, default=kRingSides, help=f"본 하나의 정점 수({kRingSides} 의 배수) — 메시 정점 수를 정한다")
    parser.add_argument("--seed", type=int, default=1, help="씨앗(32 비트) — 회전 축 · 위상을 고른다")
    parser.add_argument("--out", type=pathlib.Path, default=None, help="출력 .glb 경로(옆에 .rig.xml)")
    args = parser.parse_args(argv)

    try:
        if args.representatives:
            if args.topology is not None or args.bones is not None or args.out is not None:
                raise ValueError("--representatives takes only --out-dir")
            outDir = args.out_dir if args.out_dir is not None else getProjectRoot() / "Resource" / kRepresentativeFolder
            for topology, bones in kRepresentatives:
                outPath = outDir / (makeRepresentativeName(topology, bones) + ".glb")
                glbSize, rigSize = writeRig(outPath, makeRepresentativeOptions(topology, bones))
                print(f"wrote {outPath} ({glbSize} bytes) + .rig.xml ({rigSize} bytes)")
            return 0
        if args.topology is None or args.bones is None or args.out is None:
            raise ValueError("--topology, --bones and --out are required (or --representatives)")
        options = RigOptions(args.topology, args.bones, args.morphs, args.clips, args.clip_seconds, args.vertices_per_bone, args.seed)
        glbSize, rigSize = writeRig(args.out, options)
        print(f"wrote {args.out} ({glbSize} bytes) + .rig.xml ({rigSize} bytes)")
        return 0
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
