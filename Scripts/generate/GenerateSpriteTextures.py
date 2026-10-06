"""엔진이 들고 다니는 작은 스프라이트 텍스처와 그 클립을 만듭니다(DDS, RGBA8, 밉 하나).

  - `engine/textures/test/quadrants.dds` + `quadrants.sprite.json` — 네 칸(왼쪽 위 빨강 · 오른쪽 위 초록 · 왼쪽 아래 파랑 · 오른쪽 아래 흰색)
    시험 텍스처입니다. 인스턴스마다 다른 UV 사각형 · 색을 픽셀로 확인하는 시험(RenderPassGpuTest)과 확인용 씬의 애니메이션 아틀라스가 씁니다.
  - `engine/textures/missing.dds` — 마젠타 · 검정 체커(64x64, 16 텍셀 칸). 못 읽은 텍스처 · 머티리얼 대신 샘플합니다
    (`EngineDefaultAssets::_missingTexture` · `missingmaterial.material`).

엔진은 실행 중에 DDS 만 읽으므로(`DdsLoader`) PNG 가 아니라 DDS 를 씁니다. 같은 입력이면 바이트까지 같은 파일이 나옵니다.

사용법: py -3 Scripts/generate/GenerateSpriteTextures.py [--root <repo>]
"""
import argparse
import json
import os
import struct
import sys
from typing import Sequence

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)

kQuadrantSize = 64       # 시험 텍스처 한 변(텍셀). 칸 하나가 32x32 — 선형 필터가 이웃 칸을 끌어오는 띠가 프레임 폭의 1/32 에 그친다
kQuadrantColors = ((255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 255))  # 왼위 · 오위 · 왼아래 · 오아래

kQuadrantsTexturePath = "engine/textures/test/quadrants.dds"

kMissingTextureSize = 64        # 누락 텍스처 한 변(텍셀)
kMissingCellSize = 16           # 체커 칸 한 변 — 4x4 칸. 칸이 작으면 멀리서 회색으로 뭉개져 눈에 안 띈다
kMissingColors = ((255, 0, 255, 255), (0, 0, 0, 255))  # 마젠타 · 검정 — 유니티 · 소스 엔진의 누락 표시 색
kMissingTexturePath = "engine/textures/missing.dds"


def makeDdsBytes(width: int, height: int, rgbaBytes: bytes | bytearray) -> bytes:
    """RGBA8 픽셀(행 우선, 위에서 아래)로 DDS 파일 바이트를 만듭니다. 레거시 헤더(DDPF_RGB | ALPHAPIXELS, R 마스크 0xFF) — 엔진은 R8G8B8A8_UNORM 으로 읽습니다."""
    if len(rgbaBytes) != width * height * 4:
        raise ValueError("pixel count does not match the size")
    kFlags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000  # CAPS · HEIGHT · WIDTH · PITCH · PIXELFORMAT
    pixelFormat = struct.pack("<8I", 32, 0x41, 0, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000)
    header = struct.pack("<7I", 124, kFlags, height, width, width * 4, 0, 1) + b"\0" * 44 + pixelFormat + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    return b"DDS " + header + bytes(rgbaBytes)


def makeQuadrantTextureInternal():
    """네 칸 시험 텍스처의 픽셀과, 칸마다 한 프레임(250 ms)인 클립 프레임 목록을 만듭니다."""
    half = kQuadrantSize // 2
    pixels = bytearray()
    for y in range(kQuadrantSize):
        for x in range(kQuadrantSize):
            quadrant = (1 if x >= half else 0) + (2 if y >= half else 0)
            pixels += bytes(kQuadrantColors[quadrant])
    listFrame = [
        {"u": 0.0, "v": 0.0, "w": 0.5, "h": 0.5, "durationMs": 250},
        {"u": 0.5, "v": 0.0, "w": 0.5, "h": 0.5, "durationMs": 250},
        {"u": 0.5, "v": 0.5, "w": 0.5, "h": 0.5, "durationMs": 250},
        {"u": 0.0, "v": 0.5, "w": 0.5, "h": 0.5, "durationMs": 250},
    ]
    return kQuadrantSize, kQuadrantSize, pixels, listFrame


def makeMissingTextureInternal():
    """누락 텍스처(마젠타 · 검정 체커)의 픽셀을 만듭니다."""
    pixels = bytearray()
    for y in range(kMissingTextureSize):
        for x in range(kMissingTextureSize):
            cell = (x // kMissingCellSize + y // kMissingCellSize) % 2
            pixels += bytes(kMissingColors[cell])
    return kMissingTextureSize, kMissingTextureSize, pixels


def makeClipText(atlasPath: str, listFrame: list, listAnimation: list) -> str:
    """`SpriteClipAsset::toJson` 과 같은 키 · 순서의 클립 글을 만듭니다(atlas · frames · transformKeys · 있으면 animations)."""
    clip = {"atlas": atlasPath, "frames": listFrame, "transformKeys": []}
    if listAnimation:
        clip["animations"] = listAnimation
    return json.dumps(clip, indent=2) + "\n"


def writeFileInternal(path: str, data: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)
    print(f"wrote {path} ({len(data)} bytes)")


def generate(repositoryRoot: str) -> None:
    """세 파일을 씁니다. 리소스 경로는 소문자입니다(CheckResourceCasing)."""
    resourceRoot = os.path.join(repositoryRoot, "Resource")

    width, height, pixels, listFrame = makeQuadrantTextureInternal()
    writeFileInternal(os.path.join(resourceRoot, kQuadrantsTexturePath), makeDdsBytes(width, height, pixels))
    listAnimation = [{"name": "cycle", "start": 0, "count": 4, "loop": True}]
    writeFileInternal(os.path.join(resourceRoot, "engine/textures/test/quadrants.sprite.json"),
                      makeClipText(kQuadrantsTexturePath, listFrame, listAnimation).encode("utf-8"))
    width, height, pixels = makeMissingTextureInternal()
    writeFileInternal(os.path.join(resourceRoot, kMissingTexturePath), makeDdsBytes(width, height, pixels))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")), help="repository root")
    args = parser.parse_args(argv)
    generate(args.root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
