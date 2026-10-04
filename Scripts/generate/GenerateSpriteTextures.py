"""엔진이 들고 다니는 작은 스프라이트 텍스처 셋과 그 클립을 만듭니다(DDS, RGBA8, 밉 하나).

  - `engine/textures/ui/digits.dds` + `digits.sprite.json` — 데미지 숫자(`DamageNumberComponent`)의 글리프 아틀라스입니다.
    프레임 0..9 가 숫자, 10 이 '-' 입니다. 5x7 비트맵 글꼴을 세 배로 키우고 검은 테두리를 둘렀습니다(흰 글자라 색은 스프라이트 색이 정합니다).
    **배치 규칙(칸 크기 · 순서)은 이 파일에만 있습니다** — 컴포넌트는 클립의 프레임 번호만 압니다.
  - `engine/textures/test/quadrants.dds` + `quadrants.sprite.json` — 네 칸(왼쪽 위 빨강 · 오른쪽 위 초록 · 왼쪽 아래 파랑 · 오른쪽 아래 흰색)
    시험 텍스처입니다. 인스턴스마다 다른 UV 사각형 · 색을 픽셀로 확인하는 시험(RenderPassGpuTest)과 확인용 씬의 애니메이션 아틀라스가 씁니다.

엔진은 실행 중에 DDS 만 읽으므로(`DdsLoader`) PNG 가 아니라 DDS 를 씁니다. 같은 입력이면 바이트까지 같은 파일이 나옵니다.

사용법: py -3 Scripts/generate/GenerateSpriteTextures.py [--root <repo>]
"""
import argparse
import json
import os
import struct
import sys

# 5x7 비트맵 글꼴. 프레임 순서가 곧 글리프 번호다(DamageNumberComponent::kMinusGlyphFrame = 10).
kGlyphRows = (
    (".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."),  # 0
    ("..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."),  # 1
    (".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"),  # 2
    ("#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."),  # 3
    ("...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."),  # 4
    ("#####", "#....", "####.", "....#", "....#", "#...#", ".###."),  # 5
    ("..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."),  # 6
    ("#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."),  # 7
    (".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."),  # 8
    (".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."),  # 9
    (".....", ".....", ".....", "#####", ".....", ".....", "....."),  # -
)
kGlyphScale = 3          # 글꼴 한 점 = 3x3 텍셀
kOutlineTexels = 2       # 글자 둘레의 검은 테두리 두께
kCellWidth = 32          # 아틀라스의 칸(텍셀). 칸 사이에 빈 텍셀이 있어 선형 필터가 이웃 칸을 끌어오지 않는다
kCellHeight = 32
kAtlasColumns = 16       # 512 x 32 — 2 의 거듭제곱
kFrameWidthTexels = 24   # 프레임(UV 사각형) 폭. 칸 가운데 24 텍셀 — 글자 비율 3:4 가 DamageNumberComponent 의 기본 글자 크기(0.3, 0.4)와 같다

kQuadrantSize = 64       # 시험 텍스처 한 변(텍셀). 칸 하나가 32x32 — 선형 필터가 이웃 칸을 끌어오는 띠가 프레임 폭의 1/32 에 그친다
kQuadrantColors = ((255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 255))  # 왼위 · 오위 · 왼아래 · 오아래

kDigitsTexturePath = "engine/textures/ui/digits.dds"
kQuadrantsTexturePath = "engine/textures/test/quadrants.dds"


def makeDdsBytes(width, height, rgbaBytes):
    """RGBA8 픽셀(행 우선, 위에서 아래)로 DDS 파일 바이트를 만듭니다. 레거시 헤더(DDPF_RGB | ALPHAPIXELS, R 마스크 0xFF) — 엔진은 R8G8B8A8_UNORM 으로 읽습니다."""
    if len(rgbaBytes) != width * height * 4:
        raise ValueError("pixel count does not match the size")
    kFlags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000  # CAPS · HEIGHT · WIDTH · PITCH · PIXELFORMAT
    pixelFormat = struct.pack("<8I", 32, 0x41, 0, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000)
    header = struct.pack("<7I", 124, kFlags, height, width, width * 4, 0, 1) + b"\0" * 44 + pixelFormat + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    return b"DDS " + header + bytes(rgbaBytes)


def makeDigitAtlasInternal():
    """글리프 아틀라스 픽셀과 프레임 목록을 만듭니다. 글자는 흰색, 테두리는 검정, 나머지는 알파 0 인 흰색(가장자리가 검게 번지지 않게)."""
    atlasWidth = kCellWidth * kAtlasColumns
    atlasHeight = kCellHeight
    pixels = bytearray([255, 255, 255, 0] * (atlasWidth * atlasHeight))
    listFrame = []
    glyphWidth = 5 * kGlyphScale
    glyphHeight = 7 * kGlyphScale
    for glyphIndex, rows in enumerate(kGlyphRows):
        cellLeft = glyphIndex * kCellWidth
        originX = cellLeft + (kCellWidth - glyphWidth) // 2
        originY = (kCellHeight - glyphHeight) // 2
        filled = set()
        for rowIndex, row in enumerate(rows):
            for columnIndex, mark in enumerate(row):
                if mark != "#":
                    continue
                for dy in range(kGlyphScale):
                    for dx in range(kGlyphScale):
                        filled.add((originX + columnIndex * kGlyphScale + dx, originY + rowIndex * kGlyphScale + dy))
        outline = set()
        for (x, y) in filled:
            for dy in range(-kOutlineTexels, kOutlineTexels + 1):
                for dx in range(-kOutlineTexels, kOutlineTexels + 1):
                    neighbor = (x + dx, y + dy)
                    if neighbor not in filled:
                        outline.add(neighbor)
        for (x, y), color in [(texel, (0, 0, 0, 255)) for texel in outline] + [(texel, (255, 255, 255, 255)) for texel in filled]:
            offset = (y * atlasWidth + x) * 4
            pixels[offset:offset + 4] = bytes(color)
        frameLeft = cellLeft + (kCellWidth - kFrameWidthTexels) // 2
        listFrame.append({"u": frameLeft / atlasWidth, "v": 0.0, "w": kFrameWidthTexels / atlasWidth, "h": 1.0, "durationMs": 0})
    return atlasWidth, atlasHeight, pixels, listFrame


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


def makeClipText(atlasPath, listFrame, listAnimation):
    """`SpriteClipAsset::toJson` 과 같은 키 · 순서의 클립 글을 만듭니다(atlas · frames · transformKeys · 있으면 animations)."""
    clip = {"atlas": atlasPath, "frames": listFrame, "transformKeys": []}
    if listAnimation:
        clip["animations"] = listAnimation
    return json.dumps(clip, indent=2) + "\n"


def writeFileInternal(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)
    print(f"wrote {path} ({len(data)} bytes)")


def generate(repositoryRoot):
    """네 파일을 씁니다. 리소스 경로는 소문자입니다(CheckResourceCasing)."""
    resourceRoot = os.path.join(repositoryRoot, "Resource")

    width, height, pixels, listFrame = makeDigitAtlasInternal()
    writeFileInternal(os.path.join(resourceRoot, kDigitsTexturePath), makeDdsBytes(width, height, pixels))
    writeFileInternal(os.path.join(resourceRoot, "engine/textures/ui/digits.sprite.json"),
                      makeClipText(kDigitsTexturePath, listFrame, []).encode("utf-8"))

    width, height, pixels, listFrame = makeQuadrantTextureInternal()
    writeFileInternal(os.path.join(resourceRoot, kQuadrantsTexturePath), makeDdsBytes(width, height, pixels))
    listAnimation = [{"name": "cycle", "start": 0, "count": 4, "loop": True}]
    writeFileInternal(os.path.join(resourceRoot, "engine/textures/test/quadrants.sprite.json"),
                      makeClipText(kQuadrantsTexturePath, listFrame, listAnimation).encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")), help="repository root")
    args = parser.parse_args()
    generate(args.root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
