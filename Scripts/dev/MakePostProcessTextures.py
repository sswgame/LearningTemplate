"""후처리 시나리오의 텍스처 원본을 짓는다 — 컬러 그레이딩 LUT 띠 · 블룸 먼지 마스크(Content/game/empty/textures_raw/postprocess/*.png).

시나리오 game/empty/automation/postprocess_* 의 무대(maps/postprocessvolume.scene.xml)가 볼륨의 후처리 값으로 가리킨다.
  lut_sepia.png  — 256 × 16 LUT 띠(언리얼과 같은 배치: 가로 = 파랑 조각 16 개 × 빨강 16 칸, 세로 = 초록). 휘도를 세피아 색으로 옮긴다.
  bloomdirt.png  — 256 × 256 먼지 마스크. 고정 씨앗의 부드러운 얼룩 원들(가장자리일수록 많다)이고 바탕은 검정이다.
텍스처 임포트(App --import-textures)가 textures/postprocess/*.dds 로 바꾼다 — LUT 는 규칙 Color_Grading_Luts(Config/Editor/TextureImportConfig.json:
압축 없음 · 밉 없음 · 선형)를 탄다. 압축하거나 밉을 두면 조각 경계가 섞인다. LUT 주소 식은 Engine/Renderer/Frame/PostProcess/FramePostGrade.h 의
computeLUTUV 와 post_grade.hlsli 의 CameraGrade::computeLUTUV 와 같다 — 배치를 고치면 셋을 함께 고친다.

  py -3 Scripts/dev/MakePostProcessTextures.py           # 쓴다
  py -3 Scripts/dev/MakePostProcessTextures.py --check   # 저장소 파일과 바이트가 같은지만 본다(다르면 1)
"""

from __future__ import annotations

import argparse
import struct
import zlib
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[2]
_OUTPUT_DIR = _ROOT / "Content/game/empty/textures_raw/postprocess"
_LUT_SIZE = 16
_SEPIA = (1.07, 0.74, 0.43)  # 휘도 1 이 가는 색(세피아 톤)


def makePng(width: int, height: int, pixels: list[tuple[int, int, int, int]]) -> bytes:
    """RGBA8 PNG(필터 없음)를 만든다 — 압축 수준을 고정해 같은 입력이 같은 바이트다(--check)."""

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    raw = bytearray()
    for row in range(height):
        raw.append(0)
        for red, green, blue, alpha in pixels[row * width : (row + 1) * width]:
            raw += bytes((red, green, blue, alpha))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def makeSepiaLUT() -> bytes:
    """휘도를 세피아로 옮기는 LUT 띠입니다. 텍셀 (조각 b, 칸 r, 줄 g)의 입력 색은 (r, g, b) / (N - 1) 이다."""
    size = _LUT_SIZE
    pixels: list[tuple[int, int, int, int]] = []
    for row in range(size):
        for slice_ in range(size):
            for column in range(size):
                red, green, blue = column / (size - 1), row / (size - 1), slice_ / (size - 1)
                luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue
                out = [min(1.0, luminance * channel) for channel in _SEPIA]
                pixels.append((round(out[0] * 255), round(out[1] * 255), round(out[2] * 255), 255))
    return makePng(size * size, size, pixels)


def makeBloomDirt() -> bytes:
    """먼지 마스크입니다 — 선형 합동 난수(고정 씨앗)로 놓은 부드러운 원 40 개. 같은 입력이 같은 바이트다."""
    size = 256
    state = 12345
    spots = []
    for _ in range(40):
        values = []
        for _ in range(3):
            state = (state * 1103515245 + 12345) & 0x7FFFFFFF
            values.append(state / 0x7FFFFFFF)
        spots.append((values[0] * size, values[1] * size, 6 + values[2] * 26))
    pixels: list[tuple[int, int, int, int]] = []
    for row in range(size):
        for column in range(size):
            edge = max(abs(column / (size - 1) - 0.5), abs(row / (size - 1) - 0.5)) * 2.0
            value = 0.0
            for spotX, spotY, radius in spots:
                distance = ((column - spotX) ** 2 + (row - spotY) ** 2) ** 0.5
                value += max(0.0, 1.0 - distance / radius) ** 2
            level = round(min(1.0, value * (0.4 + 0.6 * edge)) * 255)
            pixels.append((level, level, level, 255))
    return makePng(size, size, pixels)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="저장소 파일과 바이트가 같은지만 본다")
    arguments = parser.parse_args()
    outputs = {_OUTPUT_DIR / "lut_sepia.png": makeSepiaLUT(), _OUTPUT_DIR / "bloomdirt.png": makeBloomDirt()}
    stale = [path for path, data in outputs.items() if not path.exists() or path.read_bytes() != data]
    if arguments.check:
        for path in stale:
            print(f"stale: {path.relative_to(_ROOT)}")
        return 1 if stale else 0
    _OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for path, data in outputs.items():
        path.write_bytes(data)
        print(f"wrote {path.relative_to(_ROOT)} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
