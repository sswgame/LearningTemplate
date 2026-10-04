#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
환경 쇼케이스(`game/empty/maps/envshowcase.scene.xml`)의 지형 원본을 만듭니다 — 높이장 · 구멍 마스크 · 레이어 가중치(스플랫) · 디테일 맵.

만드는 것(모두 원본 폴더 — 엔진은 임포트한 결과만 읽는다):
  Resource/game/empty/heightfields_raw/valley.png        257 × 257, 16 비트 회색(0..65535 = 높이 0..40 m)
  Resource/game/empty/heightfields_raw/valley_holes.png  256 × 256 칸, 8 비트(128 미만 = 구멍) — 절벽 아래 동굴 입구
  Resource/game/empty/textures_raw/valley_splat.png      257 × 257 RGBA = 풀 · 흙 · 바위 · 모래
  Resource/engine/textures_raw/terrain/terrain_detail.png 256 × 256 RGBA 이음매 없는 무늬(채널마다 레이어 하나)

지형: 256 m 정사각(칸 1 m), 구릉 + 북서쪽 절벽 띠 + 호수 분지(바닥 3 m, 수면 8 m) + 호수로 흘러드는 강 골짜기.
결과는 결정적이다(고정 씨앗) — 다시 돌려도 같은 바이트다. 강 경로(씬의 `_listRiverPoint`)를 끝에 출력한다.

사용법:
  py -3 Scripts/dev/GenerateTerrainShowcase.py
  build/Ninja-Debug/Bin/App.exe --import-heightfields
  build/Ninja-Release/Bin/App.exe --import-textures
"""

from __future__ import annotations

import math
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common import getProjectRoot, useUtf8Stdout

kResolution = 257
kHeightRange = 40.0
kWaterLevel = 8.0
kLakeCenter = (166.0, 150.0)
kLakeRadius = 42.0
kLakeBottom = 3.0
# 강 중심선(샘플 좌표)과 수면 높이(m) — 북서 구릉에서 호수로 내려간다.
kRiverPoint = ((24.0, 40.0, 16.0), (70.0, 78.0, 13.0), (104.0, 112.0, 10.5), (132.0, 136.0, kWaterLevel))
kRiverHalfWidth = 4.0


def hashUnitInternal(x: int, z: int, seed: int) -> float:
    value = (x * 374761393 + z * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    value = ((value ^ (value >> 13)) * 1274126177) & 0xFFFFFFFF
    value ^= value >> 16
    return value / 4294967295.0


def smoothInternal(t: float) -> float:
    return t * t * (3.0 - 2.0 * t)


def valueNoiseInternal(x: float, z: float, seed: int, period: int = 0) -> float:
    """격자 값 노이즈 [0, 1]. period 가 0 이 아니면 그 격자 수로 이어진다(이음매 없는 무늬)."""
    cellX = math.floor(x)
    cellZ = math.floor(z)
    fracX = smoothInternal(x - cellX)
    fracZ = smoothInternal(z - cellZ)

    def corner(offsetX: int, offsetZ: int) -> float:
        gridX = int(cellX) + offsetX
        gridZ = int(cellZ) + offsetZ
        if period > 0:
            gridX %= period
            gridZ %= period
        return hashUnitInternal(gridX, gridZ, seed)

    bottom = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * fracX
    top = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * fracX
    return bottom + (top - bottom) * fracZ


def fbmInternal(x: float, z: float, seed: int, octaveCount: int, period: int = 0) -> float:
    total = 0.0
    amplitude = 0.5
    frequency = 1.0
    for octave in range(octaveCount):
        total += amplitude * valueNoiseInternal(x * frequency, z * frequency, seed + octave * 31, period * int(frequency) if period > 0 else 0)
        amplitude *= 0.5
        frequency *= 2.0
    return total / (1.0 - 0.5 ** octaveCount)


def smoothStepInternal(edge0: float, edge1: float, value: float) -> float:
    ratio = min(max((value - edge0) / (edge1 - edge0), 0.0), 1.0)
    return smoothInternal(ratio)


def riverDistanceInternal(x: float, z: float) -> tuple[float, float]:
    """강 중심선까지의 거리와 가장 가까운 점의 수면 높이."""
    bestDistance = 1.0e9
    bestHeight = 0.0
    for index in range(len(kRiverPoint) - 1):
        startX, startZ, startY = kRiverPoint[index]
        endX, endZ, endY = kRiverPoint[index + 1]
        axisX = endX - startX
        axisZ = endZ - startZ
        ratio = ((x - startX) * axisX + (z - startZ) * axisZ) / (axisX * axisX + axisZ * axisZ)
        ratio = min(max(ratio, 0.0), 1.0)
        distance = math.hypot(x - (startX + axisX * ratio), z - (startZ + axisZ * ratio))
        if distance < bestDistance:
            bestDistance = distance
            bestHeight = startY + (endY - startY) * ratio
    return bestDistance, bestHeight


def computeHeightInternal(x: float, z: float) -> float:
    height = 16.0 + 6.0 * (fbmInternal(x / 64.0, z / 64.0, 11, 5) - 0.5) * 2.0
    # 북서쪽 절벽 띠 — x + z 가 작은 쪽이 높은 고원이다.
    plateau = smoothStepInternal(118.0, 104.0, x * 0.55 + z * 0.45)
    height += 16.0 * plateau
    # 호수 분지.
    # 물가가 동그랗지 않게 반지름을 노이즈로 흔든다.
    lakeDistance = math.hypot(x - kLakeCenter[0], z - kLakeCenter[1]) + 14.0 * (fbmInternal(x / 20.0, z / 20.0, 23, 3) - 0.5)
    basin = smoothStepInternal(kLakeRadius + 30.0, kLakeRadius - 8.0, lakeDistance)
    height = height + (kLakeBottom + 2.0 * (lakeDistance / kLakeRadius) - height) * basin
    # 강 골짜기 — 수면 아래 1.5 m 바닥, 양옆으로 완만하게.
    riverDistance, riverSurface = riverDistanceInternal(x, z)
    valley = smoothStepInternal(kRiverHalfWidth + 10.0, kRiverHalfWidth - 1.0, riverDistance)
    bed = riverSurface - 1.5 + 0.3 * riverDistance
    height = height + (min(height, bed) - height) * valley
    # 강둑은 수면보다 높아야 강이 뜨지 않는다.
    if kRiverHalfWidth <= riverDistance <= kRiverHalfWidth + 6.0 and basin < 0.05:
        height = max(height, riverSurface + 0.4)
    return min(max(height, 0.0), kHeightRange)


def computeSlopeInternal(listHeight: list[float], x: int, z: int) -> float:
    left = listHeight[z * kResolution + max(x - 1, 0)]
    right = listHeight[z * kResolution + min(x + 1, kResolution - 1)]
    down = listHeight[max(z - 1, 0) * kResolution + x]
    up = listHeight[min(z + 1, kResolution - 1) * kResolution + x]
    return math.degrees(math.atan(math.hypot((right - left) * 0.5, (up - down) * 0.5)))


def writePngInternal(path: pathlib.Path, width: int, height: int, bitDepth: int, colorType: int, rowBytes: list[bytes]) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rowBytes)
    header = struct.pack(">IIBBBBB", width, height, bitDepth, colorType, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print(f"wrote {path} ({len(data)} bytes)")


def writeTerrainSourcesInternal(resourceRoot: pathlib.Path) -> None:
    listHeight = [computeHeightInternal(float(x), float(z)) for z in range(kResolution) for x in range(kResolution)]

    heightRows = []
    for z in range(kResolution):
        row = bytearray()
        for x in range(kResolution):
            row += struct.pack(">H", int(round(listHeight[z * kResolution + x] / kHeightRange * 65535.0)))
        heightRows.append(bytes(row))
    writePngInternal(resourceRoot / "game/empty/heightfields_raw/valley.png", kResolution, kResolution, 16, 0, heightRows)

    # 구멍 — 절벽 아래 동굴 입구 하나(3 × 2 칸).
    holeRows = []
    for z in range(kResolution - 1):
        row = bytearray(255 for _ in range(kResolution - 1))
        if 96 <= z < 98:
            for x in range(58, 61):
                row[x] = 0
        holeRows.append(bytes(row))
    writePngInternal(resourceRoot / "game/empty/heightfields_raw/valley_holes.png", kResolution - 1, kResolution - 1, 8, 0, holeRows)

    splatRows = []
    for z in range(kResolution):
        row = bytearray()
        for x in range(kResolution):
            height = listHeight[z * kResolution + x]
            slope = computeSlopeInternal(listHeight, x, z)
            rock = max(smoothStepInternal(28.0, 40.0, slope), smoothStepInternal(34.0, 38.0, height))
            sand = smoothStepInternal(kWaterLevel + 1.6, kWaterLevel + 0.4, height) * (1.0 - rock)
            riverDistance, _ = riverDistanceInternal(float(x), float(z))
            dirtNoise = smoothStepInternal(0.58, 0.72, fbmInternal(x / 24.0, z / 24.0, 71, 4))
            dirt = max(dirtNoise, smoothStepInternal(kRiverHalfWidth + 5.0, kRiverHalfWidth + 1.0, riverDistance)) * (1.0 - rock) * (1.0 - sand)
            grass = max(0.0, 1.0 - rock - sand - dirt)
            total = grass + dirt + rock + sand
            row += bytes(int(round(value / total * 255.0)) for value in (grass, dirt, rock, sand))
        splatRows.append(bytes(row))
    writePngInternal(resourceRoot / "game/empty/textures_raw/valley_splat.png", kResolution, kResolution, 8, 6, splatRows)


def writeDetailTextureInternal(resourceRoot: pathlib.Path) -> None:
    kSize = 256
    detailRows = []
    for y in range(kSize):
        row = bytearray()
        for x in range(kSize):
            u = x / kSize
            v = y / kSize
            # 채널마다 한 레이어 — 0.5 가 중립(셰이더가 × 2 해 곱한다). 모두 256 텍셀로 이어진다.
            grass = 0.5 + 0.3 * (fbmInternal(u * 16.0, v * 16.0, 3, 4, 16) - 0.5) * 2.0
            dirt = 0.5 + 0.3 * (fbmInternal(u * 8.0, v * 8.0, 5, 4, 8) - 0.5) * 2.0
            strata = math.sin((v * 6.0 + 0.4 * fbmInternal(u * 4.0, v * 4.0, 7, 3, 4)) * 2.0 * math.pi * 3.0)
            rock = 0.5 + 0.18 * strata + 0.22 * (fbmInternal(u * 16.0, v * 16.0, 9, 3, 16) - 0.5) * 2.0
            ripple = math.sin((u * 12.0 + 0.6 * fbmInternal(u * 4.0, v * 4.0, 13, 2, 4)) * 2.0 * math.pi)
            sand = 0.5 + 0.12 * ripple + 0.1 * (fbmInternal(u * 32.0, v * 32.0, 17, 2, 32) - 0.5) * 2.0
            row += bytes(int(round(min(max(value, 0.0), 1.0) * 255.0)) for value in (grass, dirt, rock, sand))
        detailRows.append(bytes(row))
    writePngInternal(resourceRoot / "engine/textures_raw/terrain/terrain_detail.png", kSize, kSize, 8, 6, detailRows)


def main() -> int:
    useUtf8Stdout()
    resourceRoot = getProjectRoot() / "Resource"
    writeTerrainSourcesInternal(resourceRoot)
    writeDetailTextureInternal(resourceRoot)
    # 씬의 강 경로(월드 — 지형 원점 (-128, 0, -128) 기준)와 수면 높이.
    print("river points (world):", ";".join(f"{x - 128.0:g},{y:g},{z - 128.0:g}" for x, z, y in kRiverPoint))
    return 0


if __name__ == "__main__":
    sys.exit(main())
