#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
스크린샷 비교 한 자리 — PPM 읽기 · 축소 · PNG 읽기/쓰기 · 배경을 뺀 지표와 허용 오차 판정.

**정확한 픽셀이 아니라 지표를 견준다.** 자동 플레이 게임은 프레임 시간이 벽시계를 따라가 같은 프레임 번호에서도 장면이 조금씩
다르다(AI 의 위치 · 파티클 · 카메라 흔들림). 픽셀 단언은 거기서 무너지고, 특정 색 픽셀 수도 클리어 색 · 톤매핑에 무너진다.
그래서 모서리에서 배경색을 추정해 빼고(전경 비율 · 전경 평균색 · (R-B) 대소), 장면의 모양은 경계 밀도와 거친 격자 밝기로 본다.
허용 오차는 기록할 때 같은 조건 여러 판의 잡음 바닥에서 정한다(`deriveTolerance`).

외부 패키지(Pillow)를 쓰지 않는다 — CTest 가 부르는 자리라 CI 와 새 PC 의 파이썬 그대로 돌아야 한다. PNG 는 8 비트 RGB 만 쓰고
읽는다(기준 이미지는 이 모듈이 쓴 것뿐이다).
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path

#: 전경으로 볼 배경색과의 최대 채널 차이(0~255).
kBackgroundTolerance = 24

#: 경계로 볼 밝기 기울기(|dx| + |dy|, 0~510).
kEdgeThreshold = 48

#: 거친 격자의 칸 수(가로 × 세로) — 장면의 배치(무엇이 화면 어디에 있는가)를 본다.
kGridColumnCount = 8
kGridRowCount = 4

#: 모서리 배경 추정에 쓰는 정사각형 한 변(픽셀, 축소한 이미지 기준).
kCornerPatchSize = 4

#: 지표마다 허용 오차의 바닥 — 기록한 판들이 우연히 똑같아도 이보다 좁게 잡지 않는다.
kToleranceFloor: dict[str, float] = {
    "foregroundFraction": 0.04,
    "foregroundMean": 10.0,
    "meanColor": 8.0,
    "redMinusBlue": 10.0,
    "edgeDensity": 0.04,
    "gridLuma": 24.0,
}

#: 기록한 판들의 퍼짐에 곱하는 배수 — 판 셋의 최대 편차는 실제 퍼짐을 작게 본다.
kSpreadMultiplier = 2.5

_kPngSignature = b"\x89PNG\r\n\x1a\n"


@dataclass
class RgbImage:
    """8 비트 RGB 이미지. `pixels` 는 행 우선 `width * height * 3` 바이트다."""

    width: int
    height: int
    pixels: bytearray

    def getPixel(self, x: int, y: int) -> tuple[int, int, int]:
        offset = (y * self.width + x) * 3
        return self.pixels[offset], self.pixels[offset + 1], self.pixels[offset + 2]


@dataclass
class ImageMetrics:
    """배경을 뺀 지표 묶음. 기준 JSON 에 그대로 들어간다(`toJson` · `fromJson`)."""

    background: list[float]
    foregroundFraction: float
    foregroundMean: list[float]
    meanColor: list[float]
    redMinusBlue: float
    edgeDensity: float
    gridLuma: list[float] = field(default_factory=list)

    def toJson(self) -> dict:
        return {
            "background": [round(value, 2) for value in self.background],
            "foregroundFraction": round(self.foregroundFraction, 5),
            "foregroundMean": [round(value, 2) for value in self.foregroundMean],
            "meanColor": [round(value, 2) for value in self.meanColor],
            "redMinusBlue": round(self.redMinusBlue, 2),
            "edgeDensity": round(self.edgeDensity, 5),
            "gridLuma": [round(value, 2) for value in self.gridLuma],
        }

    @staticmethod
    def fromJson(data: dict) -> "ImageMetrics":
        return ImageMetrics(
            background=list(data["background"]),
            foregroundFraction=float(data["foregroundFraction"]),
            foregroundMean=list(data["foregroundMean"]),
            meanColor=list(data["meanColor"]),
            redMinusBlue=float(data["redMinusBlue"]),
            edgeDensity=float(data["edgeDensity"]),
            gridLuma=list(data["gridLuma"]),
        )


@dataclass(frozen=True)
class MetricViolation:
    """허용 오차를 넘은 지표 하나."""

    metric: str
    expected: float
    actual: float
    tolerance: float

    def describe(self) -> str:
        return f"{self.metric}: expected {self.expected:.4g} +/- {self.tolerance:.4g}, got {self.actual:.4g}"


class ImageFormatError(ValueError):
    """이미지 파일을 읽을 수 없다(형식이 다르거나 잘렸다)."""


# ------------------------------------------------------------------------------
# 읽기 · 쓰기
# ------------------------------------------------------------------------------
def parsePpm(data: bytes) -> RgbImage:
    """P6 PPM(최댓값 255)을 읽습니다. 머리의 주석(`#`)도 넘깁니다."""
    listToken: list[bytes] = []
    index = 0
    while len(listToken) < 4:
        while index < len(data) and data[index:index + 1].isspace():
            index += 1
        if index < len(data) and data[index:index + 1] == b"#":
            while index < len(data) and data[index:index + 1] not in (b"\n", b"\r"):
                index += 1
            continue
        start = index
        while index < len(data) and not data[index:index + 1].isspace():
            index += 1
        if start == index:
            raise ImageFormatError("PPM header is truncated")
        listToken.append(data[start:index])
    index += 1  # 머리 끝의 공백 한 글자
    if listToken[0] != b"P6":
        raise ImageFormatError(f"not a binary PPM (magic {listToken[0]!r})")
    width, height, maxValue = (int(token) for token in listToken[1:4])
    if maxValue != 255 or width <= 0 or height <= 0:
        raise ImageFormatError(f"unsupported PPM {width}x{height} max {maxValue}")
    pixelBytes = data[index:index + width * height * 3]
    if len(pixelBytes) != width * height * 3:
        raise ImageFormatError("PPM pixel data is truncated")
    return RgbImage(width, height, bytearray(pixelBytes))


def readPpm(path: Path) -> RgbImage:
    return parsePpm(Path(path).read_bytes())


def encodePng(image: RgbImage) -> bytes:
    """8 비트 RGB PNG 바이트를 만듭니다(필터 0, zlib 9)."""
    stride = image.width * 3
    raw = bytearray()
    for y in range(image.height):
        raw.append(0)
        raw.extend(image.pixels[y * stride:(y + 1) * stride])

    def makeChunkInternal(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 2, 0, 0, 0)
    return (_kPngSignature + makeChunkInternal(b"IHDR", header) + makeChunkInternal(b"IDAT", zlib.compress(bytes(raw), 9))
            + makeChunkInternal(b"IEND", b""))


def writePng(path: Path, image: RgbImage) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(encodePng(image))


def decodePng(data: bytes) -> RgbImage:
    """8 비트 RGB(색 형식 2) · 비인터레이스 PNG 를 읽습니다. 필터 다섯 가지를 모두 풉니다."""
    if not data.startswith(_kPngSignature):
        raise ImageFormatError("not a PNG file")
    index = len(_kPngSignature)
    width = height = 0
    compressed = bytearray()
    while index + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[index:index + 8])
        payload = data[index + 8:index + 8 + length]
        if len(payload) != length:
            raise ImageFormatError("PNG chunk is truncated")
        if kind == b"IHDR":
            width, height, bitDepth, colorType, _, _, interlace = struct.unpack(">IIBBBBB", payload)
            if bitDepth != 8 or colorType != 2 or interlace != 0:
                raise ImageFormatError(f"unsupported PNG (depth {bitDepth}, color {colorType}, interlace {interlace})")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        index += 12 + length
    if width == 0 or height == 0:
        raise ImageFormatError("PNG has no IHDR")
    raw = zlib.decompress(bytes(compressed))
    stride = width * 3
    if len(raw) != height * (stride + 1):
        raise ImageFormatError("PNG pixel data size does not match its header")
    pixels = bytearray(width * height * 3)
    previous = bytearray(stride)
    for y in range(height):
        filterType = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - 3] if x >= 3 else 0
            up = previous[x]
            upLeft = previous[x - 3] if x >= 3 else 0
            if filterType == 1:
                line[x] = (line[x] + left) & 0xFF
            elif filterType == 2:
                line[x] = (line[x] + up) & 0xFF
            elif filterType == 3:
                line[x] = (line[x] + ((left + up) >> 1)) & 0xFF
            elif filterType == 4:
                estimate = left + up - upLeft
                distanceLeft, distanceUp, distanceUpLeft = abs(estimate - left), abs(estimate - up), abs(estimate - upLeft)
                if distanceLeft <= distanceUp and distanceLeft <= distanceUpLeft:
                    predictor = left
                elif distanceUp <= distanceUpLeft:
                    predictor = up
                else:
                    predictor = upLeft
                line[x] = (line[x] + predictor) & 0xFF
            elif filterType != 0:
                raise ImageFormatError(f"unknown PNG filter {filterType}")
        pixels[y * stride:(y + 1) * stride] = line
        previous = line
    return RgbImage(width, height, pixels)


def readPng(path: Path) -> RgbImage:
    return decodePng(Path(path).read_bytes())


# ------------------------------------------------------------------------------
# 축소 · 지표
# ------------------------------------------------------------------------------
def downscale(image: RgbImage, width: int, height: int) -> RgbImage:
    """면적 평균으로 줄입니다. 원본보다 크게는 만들지 않습니다(그때는 원본 크기)."""
    width = min(width, image.width)
    height = min(height, image.height)
    pixels = bytearray(width * height * 3)
    for targetY in range(height):
        sourceY0 = targetY * image.height // height
        sourceY1 = max(sourceY0 + 1, (targetY + 1) * image.height // height)
        for targetX in range(width):
            sourceX0 = targetX * image.width // width
            sourceX1 = max(sourceX0 + 1, (targetX + 1) * image.width // width)
            arrSum = [0, 0, 0]
            for sourceY in range(sourceY0, sourceY1):
                rowOffset = sourceY * image.width * 3
                for sourceX in range(sourceX0, sourceX1):
                    offset = rowOffset + sourceX * 3
                    arrSum[0] += image.pixels[offset]
                    arrSum[1] += image.pixels[offset + 1]
                    arrSum[2] += image.pixels[offset + 2]
            count = (sourceY1 - sourceY0) * (sourceX1 - sourceX0)
            targetOffset = (targetY * width + targetX) * 3
            for channel in range(3):
                pixels[targetOffset + channel] = (arrSum[channel] + count // 2) // count
    return RgbImage(width, height, pixels)


def estimateBackground(image: RgbImage) -> list[float] | None:
    """
    네 모서리 정사각형의 평균색 가운데 채널별 중앙값 — 한 모서리를 무엇이 가려도 배경은 남은 셋이 정한다.
    가운데 두 모서리(정렬한 2 · 3 번째)도 서로 다르면 **배경이 없는 장면**(1 인칭 · 화면을 채운 지형 — 위는 하늘, 아래는 땅)이라 None 이다.
    그런 장면에서 모서리 중앙값을 배경으로 쓰면 카메라가 조금만 돌아도 "전경" 이 절반씩 오간다.
    """
    patch = max(1, min(kCornerPatchSize, image.width // 4, image.height // 4))
    listCornerMean: list[list[float]] = []
    for originX, originY in ((0, 0), (image.width - patch, 0), (0, image.height - patch), (image.width - patch, image.height - patch)):
        arrSum = [0, 0, 0]
        for y in range(originY, originY + patch):
            for x in range(originX, originX + patch):
                red, green, blue = image.getPixel(x, y)
                arrSum[0] += red
                arrSum[1] += green
                arrSum[2] += blue
        listCornerMean.append([value / (patch * patch) for value in arrSum])
    background: list[float] = []
    for channel in range(3):
        listValue = sorted(corner[channel] for corner in listCornerMean)
        if listValue[2] - listValue[1] > kBackgroundTolerance:
            return None
        background.append((listValue[1] + listValue[2]) / 2.0)
    return background


def computeLuma(red: int, green: int, blue: int) -> float:
    return 0.299 * red + 0.587 * green + 0.114 * blue


def computeMetrics(image: RgbImage) -> ImageMetrics:
    """
    배경을 뺀 지표를 셉니다. 전경이 하나도 없으면 전경 평균은 배경색입니다.
    배경이 없는 장면(`estimateBackground` 가 None)은 화면 전체가 전경이다 — 전경 비율 1, 전경 평균 = 화면 평균, 배경 칸은 [-1, -1, -1].
    """
    estimated = estimateBackground(image)
    bNoBackground = estimated is None
    background = estimated if estimated is not None else [-1.0, -1.0, -1.0]
    pixelCount = image.width * image.height
    foregroundCount = 0
    arrForegroundSum = [0, 0, 0]
    arrTotalSum = [0, 0, 0]
    listLuma = [0.0] * pixelCount
    arrGridSum = [0.0] * (kGridColumnCount * kGridRowCount)
    arrGridCount = [0] * (kGridColumnCount * kGridRowCount)
    for y in range(image.height):
        gridRow = min(kGridRowCount - 1, y * kGridRowCount // image.height)
        for x in range(image.width):
            red, green, blue = image.getPixel(x, y)
            arrTotalSum[0] += red
            arrTotalSum[1] += green
            arrTotalSum[2] += blue
            luma = computeLuma(red, green, blue)
            listLuma[y * image.width + x] = luma
            cell = gridRow * kGridColumnCount + min(kGridColumnCount - 1, x * kGridColumnCount // image.width)
            arrGridSum[cell] += luma
            arrGridCount[cell] += 1
            if bNoBackground or max(abs(red - background[0]), abs(green - background[1]), abs(blue - background[2])) > kBackgroundTolerance:
                foregroundCount += 1
                arrForegroundSum[0] += red
                arrForegroundSum[1] += green
                arrForegroundSum[2] += blue

    edgeCount = 0
    for y in range(image.height - 1):
        for x in range(image.width - 1):
            luma = listLuma[y * image.width + x]
            gradient = abs(listLuma[y * image.width + x + 1] - luma) + abs(listLuma[(y + 1) * image.width + x] - luma)
            if gradient > kEdgeThreshold:
                edgeCount += 1
    edgeArea = max(1, (image.width - 1) * (image.height - 1))

    foregroundMean = [value / foregroundCount for value in arrForegroundSum] if foregroundCount else list(background)
    return ImageMetrics(
        background=background,
        foregroundFraction=foregroundCount / pixelCount,
        foregroundMean=foregroundMean,
        meanColor=[value / pixelCount for value in arrTotalSum],
        redMinusBlue=foregroundMean[0] - foregroundMean[2],
        edgeDensity=edgeCount / edgeArea,
        gridLuma=[arrGridSum[cell] / max(1, arrGridCount[cell]) for cell in range(len(arrGridSum))],
    )


def flattenMetricsInternal(metrics: ImageMetrics) -> dict[str, list[float]]:
    """지표 이름 → 값 목록(스칼라도 길이 1). 배경색은 판정하지 않는다 — 전경 지표가 이미 배경을 기준으로 잰 것이다."""
    return {
        "foregroundFraction": [metrics.foregroundFraction],
        "foregroundMean": list(metrics.foregroundMean),
        "meanColor": list(metrics.meanColor),
        "redMinusBlue": [metrics.redMinusBlue],
        "edgeDensity": [metrics.edgeDensity],
        "gridLuma": list(metrics.gridLuma),
    }


def averageMetrics(listMetrics: list[ImageMetrics]) -> ImageMetrics:
    """여러 판의 지표를 칸마다 평균합니다(기준을 기록할 때)."""
    if not listMetrics:
        raise ValueError("averageMetrics needs at least one sample")
    count = len(listMetrics)

    def meanListInternal(listList: list[list[float]]) -> list[float]:
        return [sum(values) / count for values in zip(*listList)]

    return ImageMetrics(
        background=meanListInternal([metrics.background for metrics in listMetrics]),
        foregroundFraction=sum(metrics.foregroundFraction for metrics in listMetrics) / count,
        foregroundMean=meanListInternal([metrics.foregroundMean for metrics in listMetrics]),
        meanColor=meanListInternal([metrics.meanColor for metrics in listMetrics]),
        redMinusBlue=sum(metrics.redMinusBlue for metrics in listMetrics) / count,
        edgeDensity=sum(metrics.edgeDensity for metrics in listMetrics) / count,
        gridLuma=meanListInternal([metrics.gridLuma for metrics in listMetrics]),
    )


def deriveTolerance(listMetrics: list[ImageMetrics]) -> dict[str, float]:
    """같은 조건 여러 판의 퍼짐에서 지표별 허용 오차를 정합니다 — max(바닥, 배수 × 평균에서의 최대 편차)."""
    mean = flattenMetricsInternal(averageMetrics(listMetrics))
    tolerance: dict[str, float] = {}
    for name, listMean in mean.items():
        deviation = 0.0
        for metrics in listMetrics:
            for value, center in zip(flattenMetricsInternal(metrics)[name], listMean):
                deviation = max(deviation, abs(value - center))
        tolerance[name] = round(max(kToleranceFloor[name], kSpreadMultiplier * deviation), 5)
    return tolerance


def compareMetrics(expected: ImageMetrics, actual: ImageMetrics, tolerance: dict[str, float]) -> list[MetricViolation]:
    """허용 오차를 넘은 지표들입니다. 목록 지표(색 · 격자)는 칸마다 보고 가장 많이 벗어난 칸 하나를 알립니다."""
    listViolation: list[MetricViolation] = []
    flatExpected = flattenMetricsInternal(expected)
    flatActual = flattenMetricsInternal(actual)
    for name, listExpected in flatExpected.items():
        limit = tolerance.get(name, kToleranceFloor[name])
        listActual = flatActual[name]
        if len(listActual) != len(listExpected):
            listViolation.append(MetricViolation(name, float(len(listExpected)), float(len(listActual)), 0.0))
            continue
        worst: MetricViolation | None = None
        for index, (expectedValue, actualValue) in enumerate(zip(listExpected, listActual)):
            if abs(actualValue - expectedValue) > limit:
                label = name if len(listExpected) == 1 else f"{name}[{index}]"
                candidate = MetricViolation(label, expectedValue, actualValue, limit)
                if worst is None or abs(actualValue - expectedValue) > abs(worst.actual - worst.expected):
                    worst = candidate
        if worst is not None:
            listViolation.append(worst)
    return listViolation
