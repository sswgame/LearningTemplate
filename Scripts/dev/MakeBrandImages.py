#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
에디터 스플래시 원본과 앱 아이콘을 같은 그림(SW 모노그램)에서 만듭니다 — 저장소에서 그린 것이라 출처 표기가 필요 없습니다.

만드는 것:
  Resource/editor/textures_raw/splash.png   1376 × 768 RGBA — 어두운 바탕 가운데 모노그램과 강조 줄(임포트하면 editor/textures/splash.dds)
  Source/App/Resources/app.ico              16 · 24 · 32 · 48 · 64 · 128 · 256 — 둥근 사각형 위 같은 모노그램(PNG 를 담은 ICO)

그림은 모양 몇 개(볼록 다각형 · 반원 고리)의 부호 거리(SDF)로 칠합니다. 픽셀 가운데의 거리로 덮임을 정해 테두리가 부드럽고,
크기마다 같은 정의에서 다시 그려 작은 아이콘도 뭉개지지 않습니다. 결과는 결정적이다 — 다시 돌려도 같은 바이트다.

사용법:
  py -3 Scripts/dev/MakeBrandImages.py
  build/Ninja-Debug/Bin/App.exe --import-textures
"""

from __future__ import annotations

import argparse
import math
import pathlib
import struct
import sys
import zlib
from dataclasses import dataclass
from typing import Callable

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common import getProjectRoot

#: 스플래시 크기 — 스플래시 창이 이 크기로 뜬다(`ISplashWindow`).
kSplashSize = (1376, 768)
#: 앱 아이콘에 담는 크기(Windows 셸이 고르는 표준 크기).
kIconSizes = (16, 24, 32, 48, 64, 128, 256)
kSplashRelPath = "Resource/editor/textures_raw/splash.png"
kIconRelPath = "Source/App/Resources/app.ico"

# 색(sRGB 8 비트).
kBackgroundTop = (40, 44, 53)
kBackgroundBottom = (29, 32, 39)
kMarkColor = (206, 212, 222)
kAccentColor = (74, 128, 204)

# 모노그램(모노그램 단위 — 높이 1). S 는 x 0 .. kSWidth, W 는 kWLeft .. kMarkWidth.
kStroke = 0.17
kSWidth = 0.62
kWLeft = 0.74
kWStroke = 0.2
kMarkWidth = kWLeft + 0.9
#: 이어 붙는 모양이 서로 겹치는 폭(모노그램 단위).
kJoinOverlap = 0.02

SignedDistance = Callable[[float, float], float]


def convexPolygonInternal(listPoint: list[tuple[float, float]]) -> SignedDistance:
    """시계 방향(화면 좌표 — y 아래) 볼록 다각형의 부호 거리(안쪽 음수). 변 직선까지의 거리 중 최대 — 바깥 꼭짓점 근처는 조금 작게 잰다."""
    listEdge = []
    for index, (startX, startY) in enumerate(listPoint):
        endX, endY = listPoint[(index + 1) % len(listPoint)]
        length = math.hypot(endX - startX, endY - startY)
        # 화면 좌표의 시계 방향에서 바깥 법선은 (dy, -dx).
        listEdge.append(((endY - startY) / length, -(endX - startX) / length, startX, startY))

    def distance(x: float, y: float) -> float:
        return max(normalX * (x - pointX) + normalY * (y - pointY) for normalX, normalY, pointX, pointY in listEdge)
    return distance


def boxInternal(left: float, top: float, right: float, bottom: float) -> SignedDistance:
    return convexPolygonInternal([(left, top), (right, top), (right, bottom), (left, bottom)])


def halfRingInternal(centerX: float, centerY: float, radius: float, thickness: float, bLeftHalf: bool) -> SignedDistance:
    """가운데 반지름 `radius`, 두께 `thickness` 인 고리의 왼쪽(또는 오른쪽) 절반."""
    def distance(x: float, y: float) -> float:
        ring = abs(math.hypot(x - centerX, y - centerY) - radius) - thickness * 0.5
        side = (x - centerX) if bLeftHalf else (centerX - x)
        return max(ring, side)
    return distance


def slantedStrokeInternal(topX: float, topY: float, bottomX: float, bottomY: float, width: float) -> SignedDistance:
    """위 · 아래를 수평으로 자른 기울어진 획(가로 폭 `width`)."""
    half = width * 0.5
    return convexPolygonInternal([(topX - half, topY), (topX + half, topY), (bottomX + half, bottomY), (bottomX - half, bottomY)])


def unionInternal(listShape: list[SignedDistance]) -> SignedDistance:
    def distance(x: float, y: float) -> float:
        return min(shape(x, y) for shape in listShape)
    return distance


@dataclass
class Layer:
    """한 색으로 칠하는 모양 하나(모노그램 단위)."""

    shape: SignedDistance
    color: tuple[int, int, int]


def makeMarkLayersInternal() -> list[Layer]:
    """SW 모노그램 — 회색 S · W, W 의 안쪽 왼 획 하나만 강조색."""
    half = kStroke * 0.5
    bowlRadius = (0.5 - half) * 0.5
    # 굽이의 바깥 가장자리가 가로획 끝과 맞도록 가운데를 반 획만큼 안으로 들인다.
    upperCenterX = bowlRadius + half
    upperCenterY = half + bowlRadius
    lowerCenterX = kSWidth - bowlRadius - half
    lowerCenterY = 0.5 + bowlRadius
    # 가로획은 굽이 안으로 kJoinOverlap 만큼 더 들어간다 — 두 모양이 같은 선에서 끝나면 그 선의 덮임이 0.5 라 이음매가 보인다.
    letterS = unionInternal([
        boxInternal(upperCenterX - kJoinOverlap, 0.0, kSWidth, kStroke),                       # 위 가로
        halfRingInternal(upperCenterX, upperCenterY, bowlRadius, kStroke, True),               # 위 왼쪽 굽이
        boxInternal(upperCenterX - kJoinOverlap, 0.5 - half, lowerCenterX + kJoinOverlap, 0.5 + half),  # 가운데 가로
        halfRingInternal(lowerCenterX, lowerCenterY, bowlRadius, kStroke, False),              # 아래 오른쪽 굽이
        boxInternal(0.0, 1.0 - kStroke, lowerCenterX + kJoinOverlap, 1.0),                     # 아래 가로
    ])
    # W — 바깥 두 획은 위 끝에서, 안쪽 두 획은 가운데 봉우리(높이 0.3)에서 아래 골짜기로.
    left = kWLeft
    outerLeft = slantedStrokeInternal(left + 0.1, 0.0, left + 0.31, 1.0, kWStroke)
    innerLeft = slantedStrokeInternal(left + 0.45, 0.3, left + 0.31, 1.0, kWStroke)
    innerRight = slantedStrokeInternal(left + 0.45, 0.3, left + 0.59, 1.0, kWStroke)
    outerRight = slantedStrokeInternal(left + 0.8, 0.0, left + 0.59, 1.0, kWStroke)
    return [
        Layer(letterS, kMarkColor),
        Layer(unionInternal([outerLeft, innerRight, outerRight]), kMarkColor),
        Layer(innerLeft, kAccentColor),
    ]


def blendInternal(base: list[float], color: tuple[int, int, int], coverage: float) -> None:
    for channel in range(3):
        base[channel] += (color[channel] - base[channel]) * coverage


def coverageInternal(distanceInPixels: float) -> float:
    """픽셀 가운데의 거리 → 덮임(테두리에서 한 픽셀 폭으로 부드럽게)."""
    return min(max(0.5 - distanceInPixels, 0.0), 1.0)


def renderInternal(width: int, height: int, background: Callable[[int, int], tuple[list[float], float]],
                   listLayer: list[Layer], markOrigin: tuple[float, float], markScale: float) -> bytes:
    """RGBA 행 우선 바이트. `background` 는 (픽셀 → (색, 알파)), 모노그램은 markOrigin 에 높이 markScale 픽셀로 놓는다."""
    originX, originY = markOrigin
    # 모노그램이 닿는 픽셀 범위 — 밖은 거리를 재지 않는다.
    minX = max(int(originX) - 2, 0)
    maxX = min(int(originX + kMarkWidth * markScale) + 3, width)
    minY = max(int(originY) - 2, 0)
    maxY = min(int(originY + markScale) + 3, height)
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            color, alpha = background(x, y)
            if minX <= x < maxX and minY <= y < maxY:
                markX = (x + 0.5 - originX) / markScale
                markY = (y + 0.5 - originY) / markScale
                for layer in listLayer:
                    coverage = coverageInternal(layer.shape(markX, markY) * markScale)
                    if coverage > 0.0:
                        blendInternal(color, layer.color, coverage)
            # 큰 면의 완만한 기울기가 8 비트에서 띠로 보이지 않게 픽셀마다 ±0.5 의 결정적 디더를 더한다.
            dither = ((x * 73856093) ^ (y * 19349663)) % 255 / 255.0 - 0.5
            pixels += bytes(int(min(max(round(color[channel] + dither), 0), 255)) for channel in range(3))
            pixels.append(int(round(alpha * 255.0)))
    return bytes(pixels)


def lerpColorInternal(colorA: tuple[int, int, int], colorB: tuple[int, int, int], t: float) -> list[float]:
    return [colorA[channel] + (colorB[channel] - colorA[channel]) * t for channel in range(3)]


def renderSplashInternal() -> bytes:
    width, height = kSplashSize
    markHeight = 150.0
    markOrigin = ((width - kMarkWidth * markHeight) * 0.5, (height - markHeight) * 0.5 - 16.0)
    accentLine = boxInternal(width * 0.5 - 90.0, markOrigin[1] + markHeight + 34.0, width * 0.5 + 90.0, markOrigin[1] + markHeight + 37.0)

    def background(x: int, y: int) -> tuple[list[float], float]:
        color = lerpColorInternal(kBackgroundTop, kBackgroundBottom, y / (height - 1))
        # 가운데가 조금 밝은 비네트(반지름 = 긴 변 절반).
        offsetX = (x + 0.5 - width * 0.5) / (width * 0.5)
        offsetY = (y + 0.5 - height * 0.5) / (width * 0.5)
        glow = max(0.0, 1.0 - math.hypot(offsetX, offsetY))
        for channel in range(3):
            color[channel] += 10.0 * glow * glow
        coverage = coverageInternal(accentLine(x + 0.5, y + 0.5))
        if coverage > 0.0:
            blendInternal(color, kAccentColor, coverage * 0.8)
        return color, 1.0

    return renderInternal(width, height, background, makeMarkLayersInternal(), markOrigin, markHeight)


def renderIconInternal(size: int) -> bytes:
    cornerRadius = size * 0.22
    inset = 0.5 if size <= 32 else size / 64.0
    plate = (inset, inset, size - inset, size - inset)

    def roundedPlateDistance(x: float, y: float) -> float:
        left, top, right, bottom = plate
        nearestX = min(max(x, left + cornerRadius), right - cornerRadius)
        nearestY = min(max(y, top + cornerRadius), bottom - cornerRadius)
        return math.hypot(x - nearestX, y - nearestY) - cornerRadius

    def background(x: int, y: int) -> tuple[list[float], float]:
        color = lerpColorInternal(kBackgroundTop, kBackgroundBottom, y / max(size - 1, 1))
        return color, coverageInternal(roundedPlateDistance(x + 0.5, y + 0.5))

    markScale = size * 0.78 / kMarkWidth
    markOrigin = ((size - kMarkWidth * markScale) * 0.5, (size - markScale) * 0.5)
    return renderInternal(size, size, background, makeMarkLayersInternal(), markOrigin, markScale)


def encodeRgbaPngInternal(width: int, height: int, rgba: bytes) -> bytes:
    """8 비트 RGBA PNG(색 형식 6, 필터 0)."""
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    stride = width * 4
    raw = b"".join(b"\x00" + rgba[row * stride:(row + 1) * stride] for row in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def encodeIcoInternal(listImage: list[tuple[int, bytes]]) -> bytes:
    """PNG 를 담은 ICO(Windows Vista 부터 모든 크기에서 읽는다). 256 은 디렉터리에 0 으로 적는다."""
    headerSize = 6 + 16 * len(listImage)
    directory = struct.pack("<HHH", 0, 1, len(listImage))
    payload = b""
    for size, png in listImage:
        dimension = 0 if size >= 256 else size
        directory += struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(png), headerSize + len(payload))
        payload += png
    return directory + payload


def writeFileInternal(path: pathlib.Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print(f"wrote {path} ({len(data)} bytes)")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="에디터 스플래시 원본(splash.png)과 앱 아이콘(app.ico)을 같은 모노그램에서 그린다")
    parser.add_argument("--root", type=pathlib.Path, default=None, help="출력 뿌리(기본: 저장소 루트)")
    args = parser.parse_args(argv)
    root = args.root if args.root is not None else getProjectRoot()

    width, height = kSplashSize
    writeFileInternal(root / kSplashRelPath, encodeRgbaPngInternal(width, height, renderSplashInternal()))
    listIcon = [(size, encodeRgbaPngInternal(size, size, renderIconInternal(size))) for size in kIconSizes]
    writeFileInternal(root / kIconRelPath, encodeIcoInternal(listIcon))
    return 0


if __name__ == "__main__":
    sys.exit(main())
