#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
엔진 팩의 잡음 텍스처 원본(`Resource/engine/textures_raw/perlin.png`)을 만듭니다 — 이음매 없는 Perlin(그래디언트) 잡음 fBm.

만드는 것(원본 폴더 — 엔진은 임포트한 결과 `engine/textures/perlin.dds` 만 읽는다):
  Resource/engine/textures_raw/perlin.png   256 × 256 RGBA. R · G · B 는 서로 다른 씨앗의 잡음(채널마다 하나), A 는 255.

잡음: 격자 꼭짓점마다 해시로 고른 단위 그래디언트 + 5 차 보간(Perlin 2002). 다섯 옥타브를 더하고(진폭 ½ 씩), 격자 좌표를 주기로 접어
가장자리가 이어진다(타일링). 결과는 결정적이다(고정 씨앗 · 정수 해시) — 다시 돌려도 같은 바이트다.

사용법:
  py -3 Scripts/dev/MakeNoiseTexture.py
  build/Ninja-Debug/Bin/App.exe --import-textures
"""

from __future__ import annotations

import argparse
import math
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common import getProjectRoot

#: 텍스처 한 변(텍셀).
kSize = 256
#: 첫 옥타브의 격자 칸 수(한 변). 옥타브마다 두 배 — 주기가 정수라 모든 옥타브가 가장자리에서 이어진다.
kBasePeriod = 8
#: 더하는 옥타브 수.
kOctaveCount = 5
#: 채널마다 쓰는 씨앗(R · G · B).
kChannelSeeds = (11, 23, 37)
#: 그래디언트 방향 수 — 원 위에 고르게 놓는다.
kGradientCount = 16
#: 출력 경로(Resource 기준).
kOutputRelPath = "engine/textures_raw/perlin.png"


def hashLatticeInternal(x: int, y: int, seed: int) -> int:
    """격자 꼭짓점 → 32 비트 정수(플랫폼과 무관한 정수 연산만 쓴다)."""
    value = (x * 374761393 + y * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    value = ((value ^ (value >> 13)) * 1274126177) & 0xFFFFFFFF
    return value ^ (value >> 16)


def fadeInternal(t: float) -> float:
    """Perlin 의 5 차 보간 6t^5 - 15t^4 + 10t^3 — 1 · 2 차 도함수가 격자에서 이어진다."""
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def perlinInternal(x: float, y: float, seed: int, period: int, listGradient: list[tuple[float, float]]) -> float:
    """주기 `period` 로 이어지는 2D Perlin 잡음. 값은 대략 [-0.7, 0.7]."""
    cellX = math.floor(x)
    cellY = math.floor(y)
    fracX = x - cellX
    fracY = y - cellY

    def corner(offsetX: int, offsetY: int) -> float:
        gradientX, gradientY = listGradient[hashLatticeInternal((int(cellX) + offsetX) % period, (int(cellY) + offsetY) % period, seed)
                                            % kGradientCount]
        return gradientX * (fracX - offsetX) + gradientY * (fracY - offsetY)

    blendX = fadeInternal(fracX)
    blendY = fadeInternal(fracY)
    bottom = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * blendX
    top = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * blendX
    return bottom + (top - bottom) * blendY


def fbmInternal(u: float, v: float, seed: int, listGradient: list[tuple[float, float]]) -> float:
    """[0, 1) 텍스처 좌표의 fBm 을 [0, 1] 로 돌려줍니다."""
    total = 0.0
    amplitude = 0.5
    amplitudeSum = 0.0
    period = kBasePeriod
    for octave in range(kOctaveCount):
        total += amplitude * perlinInternal(u * period, v * period, seed + octave * 101, period, listGradient)
        amplitudeSum += amplitude
        amplitude *= 0.5
        period *= 2
    # Perlin 2D 의 값 범위(약 ±0.7)를 [0, 1] 로 편다.
    return min(max(0.5 + total / amplitudeSum * 0.75, 0.0), 1.0)


def makeGradientsInternal() -> list[tuple[float, float]]:
    """원 위에 고르게 놓은 단위 그래디언트. 소수 여섯째 자리에서 잘라 libm 차이가 바이트로 번지지 않게 한다."""
    listGradient = []
    for index in range(kGradientCount):
        angle = 2.0 * math.pi * index / kGradientCount
        listGradient.append((round(math.cos(angle), 6), round(math.sin(angle), 6)))
    return listGradient


def encodeRgbaPngInternal(width: int, height: int, rowBytes: list[bytes]) -> bytes:
    """8 비트 RGBA PNG(색 형식 6, 필터 0)."""
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rowBytes)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def makeNoiseRowsInternal() -> list[bytes]:
    listGradient = makeGradientsInternal()
    listRow = []
    for y in range(kSize):
        row = bytearray()
        for x in range(kSize):
            u = x / kSize
            v = y / kSize
            for seed in kChannelSeeds:
                row.append(int(round(fbmInternal(u, v, seed, listGradient) * 255.0)))
            row.append(255)
        listRow.append(bytes(row))
    return listRow


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="엔진 잡음 텍스처 원본(engine/textures_raw/perlin.png)을 절차 생성으로 쓴다")
    parser.add_argument("--out", type=pathlib.Path, default=None, help="출력 경로(기본: Resource/" + kOutputRelPath + ")")
    args = parser.parse_args(argv)
    outPath = args.out if args.out is not None else getProjectRoot() / "Resource" / kOutputRelPath
    data = encodeRgbaPngInternal(kSize, kSize, makeNoiseRowsInternal())
    outPath.parent.mkdir(parents=True, exist_ok=True)
    outPath.write_bytes(data)
    print(f"wrote {outPath} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
