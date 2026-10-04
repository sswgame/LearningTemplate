"""시험 게임들이 쓰는 텍스처를 만듭니다(DDS, RGBA8, 밉 하나).

  - `game/voxelcraft/textures/blocks.dds` — 복셀 블록 아틀라스(4 x 4 칸). **직접 그린 픽셀 아트**입니다: 칸마다 16 x 16 텍셀을
    씨앗 고정 해시 노이즈로 칠하고(풀 · 흙 · 돌 · 모래 · 물 · 기반암 · 통나무 · 잎 · 판자 · 조약돌 · 벽돌 · 눈 · 석탄 · 철), 네 배로
    늘려(가장 가까운 텍셀) 64 x 64 로 넣습니다. 머티리얼 샘플러가 선형이라 16 텍셀 그대로면 블록 하나에 번져 흐려집니다 — 늘려 두면
    번지는 띠가 텍셀의 1/4 에 그칩니다. 칸 번호는 `Resource/game/voxelcraft/data/blocks.xml` 의 `tile` · `top` · `side` 가 가리킵니다.
  - `game/shooter3d/textures/crosshair.dds` · `hitmarker.dds` — Kenney "Starter Kit FPS" 의 스프라이트(CC0)를 그대로 옮긴 것입니다.
    원본 PNG 는 저장소에 두지 않습니다(`textures_raw/` 는 에디터의 굽기 경로라 여기서 만든 DDS 와 도장이 맞지 않는다). 다시 만들려면
    `git clone https://github.com/KenneyNL/Starter-Kit-FPS` 를 받아 `--kenney-fps <폴더>` 로 넘깁니다. 넘기지 않으면 그 둘은 건너뜁니다.

같은 입력이면 바이트까지 같은 파일이 나옵니다. 출처와 라이선스는 각 게임 폴더의 `credits.md` 에 있습니다.

사용법: py -3 Scripts/generate/GenerateGameTextures.py [--root <repo>] [--kenney-fps <Starter-Kit-FPS 폴더>]
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from GenerateSpriteTextures import makeDdsBytes, writeFileInternal  # noqa: E402

kTileTexels = 16          # 그리는 칸 한 변(텍셀)
kTileUpscale = 4          # 아틀라스에 넣을 때 늘리는 배수 — 칸 한 변은 64 텍셀
kAtlasColumns = 4
kAtlasRows = 4
kBlockAtlasPath = "game/voxelcraft/textures/blocks.dds"

# Kenney Starter Kit FPS (CC0 에셋 · MIT 코드). 옮긴 커밋을 적어 둔다.
kKenneyFpsRepository = "https://github.com/KenneyNL/Starter-Kit-FPS"
kKenneyFpsCommit = "185fd2326d74a5cf858cffc616f87cf9696f9cc0"
kKenneySprites = (
    ("sprites/crosshair.png", "game/shooter3d/textures/crosshair.dds"),
    ("sprites/hit.png", "game/shooter3d/textures/hitmarker.dds"),
)


def hashNoise(x, y, salt):
    """칸 안 텍셀 좌표의 씨앗 고정 노이즈(0..1)입니다. 플랫폼마다 같은 값이 나오도록 32 비트 정수만 씁니다."""
    value = (x * 374761393 + y * 668265263 + salt * 2246822519) & 0xFFFFFFFF
    value = ((value ^ (value >> 13)) * 1274126177) & 0xFFFFFFFF
    value ^= value >> 16
    return (value & 0xFFFF) / 65535.0


def shadeColor(color, factor):
    """색에 밝기 배수를 곱합니다(0..255 로 자른다)."""
    return tuple(max(0, min(255, int(round(channel * factor)))) for channel in color[:3]) + (255,)


def speckleInternal(base, salt, spread):
    """밑색에 텍셀마다 다른 밝기를 섞은 칸입니다."""
    return [[shadeColor(base, 1.0 - spread + 2.0 * spread * hashNoise(x, y, salt)) for x in range(kTileTexels)] for y in range(kTileTexels)]


def paintGrassTop():
    return speckleInternal((96, 158, 56), 1, 0.16)


def paintDirt():
    tile = speckleInternal((134, 96, 67), 3, 0.14)
    for y in range(kTileTexels):
        for x in range(kTileTexels):
            if hashNoise(x, y, 33) > 0.92:
                tile[y][x] = shadeColor((150, 150, 140), 0.9)  # 작은 돌
    return tile


def paintGrassSide():
    """흙 위에 풀이 들쭉날쭉 덮인 옆면입니다(v 0 이 블록 위)."""
    tile = paintDirt()
    for x in range(kTileTexels):
        depth = 3 + int(hashNoise(x, 0, 2) * 3.0)
        for y in range(depth):
            tile[y][x] = shadeColor((96, 158, 56), 0.85 + 0.3 * hashNoise(x, y, 4))
    return tile


def paintStone():
    tile = speckleInternal((128, 128, 128), 5, 0.10)
    for y in range(kTileTexels):
        for x in range(kTileTexels):
            if hashNoise(x // 2, y, 55) > 0.85:
                tile[y][x] = shadeColor((128, 128, 128), 0.72)  # 결
    return tile


def paintSand():
    return speckleInternal((220, 208, 160), 6, 0.07)


def paintWater():
    tile = speckleInternal((52, 98, 196), 7, 0.06)
    for y in range(kTileTexels):
        for x in range(kTileTexels):
            if (x + 2 * y + int(hashNoise(0, y, 77) * 4.0)) % 9 == 0:
                tile[y][x] = shadeColor((52, 98, 196), 1.35)  # 물결
    return tile


def paintBedrock():
    return [[shadeColor((90, 90, 90), 0.35 + 0.9 * hashNoise(x // 2, y // 2, 8)) for x in range(kTileTexels)] for y in range(kTileTexels)]


def paintLogSide():
    tile = []
    for y in range(kTileTexels):
        row = []
        for x in range(kTileTexels):
            stripe = 0.8 if (x + int(hashNoise(x, y // 4, 9) * 2.0)) % 4 == 0 else 1.0
            row.append(shadeColor((104, 80, 48), stripe * (0.9 + 0.2 * hashNoise(x, y, 10))))
        tile.append(row)
    return tile


def paintLogTop():
    tile = []
    center = (kTileTexels - 1) / 2.0
    for y in range(kTileTexels):
        row = []
        for x in range(kTileTexels):
            radius = max(abs(x - center), abs(y - center))
            if radius > 6.5:
                row.append(shadeColor((104, 80, 48), 0.9))  # 껍질
            else:
                ring = 0.85 if int(radius) % 2 == 0 else 1.0
                row.append(shadeColor((182, 148, 94), ring * (0.95 + 0.1 * hashNoise(x, y, 11))))
        tile.append(row)
    return tile


def paintLeaves():
    return [[shadeColor((58, 124, 40), 0.55 + 0.7 * hashNoise(x, y, 12)) for x in range(kTileTexels)] for y in range(kTileTexels)]


def paintPlanks():
    tile = []
    for y in range(kTileTexels):
        row = []
        board = y // 4
        for x in range(kTileTexels):
            seam = y % 4 == 3 or (x + board * 5) % 16 == 0
            factor = 0.7 if seam else 0.92 + 0.12 * hashNoise(x // 3, y, 13 + board)
            row.append(shadeColor((168, 134, 80), factor))
        tile.append(row)
    return tile


def paintCobblestone():
    """조약돌 — 씨앗 점 여덟 개의 보로노이 칸, 경계를 어둡게."""
    seeds = [(int(hashNoise(i, 0, 14) * kTileTexels), int(hashNoise(i, 1, 14) * kTileTexels)) for i in range(8)]
    tile = []
    for y in range(kTileTexels):
        row = []
        for x in range(kTileTexels):
            distances = sorted(min(abs(x - sx), kTileTexels - abs(x - sx)) ** 2 + min(abs(y - sy), kTileTexels - abs(y - sy)) ** 2 for sx, sy in seeds)
            edge = distances[1] - distances[0] < 6
            row.append(shadeColor((120, 120, 120), 0.6 if edge else 0.95 + 0.2 * hashNoise(x, y, 15)))
        tile.append(row)
    return tile


def paintBrick():
    tile = []
    for y in range(kTileTexels):
        row = []
        course = y // 4
        for x in range(kTileTexels):
            mortar = y % 4 == 3 or (x + (course % 2) * 4) % 8 == 7
            row.append((198, 194, 182, 255) if mortar else shadeColor((152, 72, 56), 0.9 + 0.2 * hashNoise(x, y, 16)))
        tile.append(row)
    return tile


def paintSnow():
    return speckleInternal((240, 244, 250), 17, 0.04)


def paintOre(oreColor, salt):
    tile = paintStone()
    for y in range(kTileTexels):
        for x in range(kTileTexels):
            if hashNoise(x // 2, y // 2, salt) > 0.78:
                tile[y][x] = shadeColor(oreColor, 0.85 + 0.3 * hashNoise(x, y, salt + 1))
    return tile


# 칸 번호 = 이 목록의 자리(blocks.xml 이 가리킨다).
kTilePainters = (
    paintGrassTop,                         # 0
    paintGrassSide,                        # 1
    paintDirt,                             # 2
    paintStone,                            # 3
    paintSand,                             # 4
    paintWater,                            # 5
    paintBedrock,                          # 6
    paintLogSide,                          # 7
    paintLogTop,                           # 8
    paintLeaves,                           # 9
    paintPlanks,                           # 10
    paintCobblestone,                      # 11
    paintBrick,                            # 12
    paintSnow,                             # 13
    lambda: paintOre((30, 30, 30), 18),    # 14 석탄
    lambda: paintOre((214, 170, 130), 20), # 15 철
)


def makeBlockAtlasInternal():
    """아틀라스 픽셀(RGBA8, 위에서 아래)을 만듭니다."""
    cell = kTileTexels * kTileUpscale
    width = cell * kAtlasColumns
    height = cell * kAtlasRows
    pixels = bytearray(width * height * 4)
    for tileIndex, painter in enumerate(kTilePainters):
        tile = painter()
        originX = (tileIndex % kAtlasColumns) * cell
        originY = (tileIndex // kAtlasColumns) * cell
        for y in range(cell):
            for x in range(cell):
                offset = ((originY + y) * width + originX + x) * 4
                pixels[offset:offset + 4] = bytes(tile[y // kTileUpscale][x // kTileUpscale])
    return width, height, bytes(pixels)


def decodePngInternal(path):
    """8 비트 RGBA · RGB, 비월 없는 PNG 를 RGBA8 로 읽습니다. 표준 라이브러리만 씁니다(PIL 이 없는 기계에서도 돈다)."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    position = 8
    width = height = colorType = 0
    compressed = bytearray()
    while position < len(data):
        length, chunkType = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        position += 12 + length
        if chunkType == b"IHDR":
            width, height, bitDepth, colorType, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if bitDepth != 8 or colorType not in (2, 6) or interlace != 0:
                raise ValueError(f"{path}: only 8-bit RGB/RGBA non-interlaced PNG is supported")
        elif chunkType == b"IDAT":
            compressed += body
        elif chunkType == b"IEND":
            break
    channels = 4 if colorType == 6 else 3
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    previous = bytearray(stride)
    rgba = bytearray()
    for row in range(height):
        filterType = raw[row * (stride + 1)]
        line = bytearray(raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)])
        for index in range(stride):
            left = line[index - channels] if index >= channels else 0
            up = previous[index]
            upLeft = previous[index - channels] if index >= channels else 0
            if filterType == 1:
                line[index] = (line[index] + left) & 0xFF
            elif filterType == 2:
                line[index] = (line[index] + up) & 0xFF
            elif filterType == 3:
                line[index] = (line[index] + ((left + up) >> 1)) & 0xFF
            elif filterType == 4:
                estimate = left + up - upLeft
                distanceLeft, distanceUp, distanceUpLeft = abs(estimate - left), abs(estimate - up), abs(estimate - upLeft)
                predictor = left if distanceLeft <= distanceUp and distanceLeft <= distanceUpLeft else (up if distanceUp <= distanceUpLeft else upLeft)
                line[index] = (line[index] + predictor) & 0xFF
        previous = line
        if channels == 4:
            rgba += line
        else:
            for index in range(0, stride, 3):
                rgba += line[index:index + 3] + b"\xff"
    return width, height, bytes(rgba)


def generate(repositoryRoot, kenneyFpsRoot):
    """텍스처를 씁니다. 리소스 경로는 소문자입니다(CheckResourceCasing)."""
    resourceRoot = os.path.join(repositoryRoot, "Resource")
    width, height, pixels = makeBlockAtlasInternal()
    writeFileInternal(os.path.join(resourceRoot, kBlockAtlasPath), makeDdsBytes(width, height, pixels))

    if not kenneyFpsRoot:
        print(f"skipped Kenney sprites (pass --kenney-fps <clone of {kKenneyFpsRepository} @ {kKenneyFpsCommit[:8]}>)")
        return
    for sourcePath, targetPath in kKenneySprites:
        width, height, pixels = decodePngInternal(os.path.join(kenneyFpsRoot, sourcePath))
        writeFileInternal(os.path.join(resourceRoot, targetPath), makeDdsBytes(width, height, pixels))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")), help="repository root")
    parser.add_argument("--kenney-fps", default="", help=f"clone of {kKenneyFpsRepository}")
    args = parser.parse_args()
    generate(args.root, args.kenney_fps)
    return 0


if __name__ == "__main__":
    sys.exit(main())
