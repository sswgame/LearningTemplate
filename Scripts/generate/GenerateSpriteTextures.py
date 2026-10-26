"""엔진이 들고 다니는 작은 스프라이트 텍스처와 그 클립을 만듭니다(DDS, RGBA8, 밉 하나).

  - `engine/textures/test/quadrants.dds` + `quadrants.sprite.xml` — 네 칸(왼쪽 위 빨강 · 오른쪽 위 초록 · 왼쪽 아래 파랑 · 오른쪽 아래 흰색)
    테스트 텍스처입니다. 인스턴스마다 다른 UV 사각형 · 색을 픽셀로 확인하는 테스트(RenderPassGPUTest)과 확인용 씬의 애니메이션 아틀라스가 씁니다.
  - `engine/textures/missing.dds` — 마젠타 · 검정 체커(64x64, 16 텍셀 칸). 못 읽은 텍스처 · 머티리얼 대신 샘플합니다
    (`EngineDefaultAssets::_missingTexture` · `missingmaterial.material`).
  - `engine/textures/test/mask_r8.dds` — 1 채널 `R8_UNORM` 마스크(DX10 머리, DXGI 61 — 임포트 규칙 `format: R8_UNORM` 이 내는 형식). 왼쪽 반 255 ·
    오른쪽 반 96 이다. 네 백엔드가 R8 을 (r, 0, 0, 1) 로 샘플하는지 RenderPassGPUTest 가 본다(매니페스트 `r8-texture-sampling` — 외부 `*_mask.*` 원본 없이).
  - `engine/textures/loading.dds` — 고른 중간 회색(4x4). 비동기로 읽는 텍스처가 도착하기 전에 샘플합니다(`EngineDefaultAssets::_loadingTexture`).
    실패 표시(마젠타)와 달리 로드 중은 정해진 상태라 눈에 띄지 않는 색입니다.

엔진은 실행 중에 DDS 만 읽으므로(`DDSLoader`) PNG 가 아니라 DDS 를 씁니다. 같은 입력이면 바이트까지 같은 파일이 나옵니다.

사용법: py -3 Scripts/generate/GenerateSpriteTextures.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402

kQuadrantSize = 64       # 테스트 텍스처 한 변(텍셀). 칸 하나가 32x32 — 선형 필터가 이웃 칸을 끌어오는 띠가 프레임 폭의 1/32 에 그친다
kQuadrantColors = ((255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 255))  # 왼위 · 오위 · 왼아래 · 오아래

kQuadrantsTexturePath = "engine/textures/test/quadrants.dds"

kMissingTextureSize = 64        # 누락 텍스처 한 변(텍셀)
kMissingCellSize = 16           # 체커 칸 한 변 — 4x4 칸. 칸이 작으면 멀리서 회색으로 뭉개져 눈에 안 띈다
kMissingColors = ((255, 0, 255, 255), (0, 0, 0, 255))  # 마젠타 · 검정 — 유니티 · 소스 엔진의 누락 표시 색
kMissingTexturePath = "engine/textures/missing.dds"

kMaskSize = 64                  # R8 마스크 한 변(텍셀). 두 반쪽이 32 텍셀이라 선형 필터의 경계 띠가 좁다
kMaskValues = (255, 96)         # 왼쪽 반 · 오른쪽 반 — 둘 다 배경(지운 색)과 다르고 서로 확실히 갈린다
kMaskTexturePath = "engine/textures/test/mask_r8.dds"
kDxgiFormatR8Unorm = 61

kLoadingTextureSize = 4                 # 로드 중 텍스처 한 변(텍셀) — 고른 색이라 크기가 뜻이 없다
kLoadingColor = (128, 128, 128, 255)     # 중간 회색
kLoadingTexturePath = "engine/textures/loading.dds"


def makeDdsBytes(width: int, height: int, rgbaBytes: bytes | bytearray) -> bytes:
    """RGBA8 픽셀(행 우선, 위에서 아래)로 DDS 파일 바이트를 만듭니다. 레거시 헤더(DDPF_RGB | ALPHAPIXELS, R 마스크 0xFF) — 엔진은 R8G8B8A8_UNORM 으로 읽습니다."""
    if len(rgbaBytes) != width * height * 4:
        raise ValueError("pixel count does not match the size")
    kFlags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000  # CAPS · HEIGHT · WIDTH · PITCH · PIXELFORMAT
    pixelFormat = struct.pack("<8I", 32, 0x41, 0, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000)
    header = struct.pack("<7I", 124, kFlags, height, width, width * 4, 0, 1) + b"\0" * 44 + pixelFormat + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    return b"DDS " + header + bytes(rgbaBytes)


def makeR8DdsBytes(width: int, height: int, redBytes: bytes | bytearray) -> bytes:
    """1 채널 픽셀(행 우선, 위에서 아래)로 DX10 머리 DDS(DXGI_FORMAT_R8_UNORM, 밉 하나)를 만듭니다 — 옛 머리의 L8 은 로더가 모른다(DDSLoader)."""
    if len(redBytes) != width * height:
        raise ValueError("pixel count does not match the size")
    kFlags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000  # CAPS · HEIGHT · WIDTH · PITCH · PIXELFORMAT
    pixelFormat = struct.pack("<2I4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)  # DDPF_FOURCC + 'DX10'
    header = struct.pack("<7I", 124, kFlags, height, width, width, 0, 1) + b"\0" * 44 + pixelFormat + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    dx10 = struct.pack("<5I", kDxgiFormatR8Unorm, 3, 0, 1, 0)  # 형식 · TEXTURE2D · misc · 배열 1 · misc2
    return b"DDS " + header + dx10 + bytes(redBytes)


def makeMaskTextureInternal() -> tuple[int, int, bytearray]:
    """R8 마스크(왼쪽 반 · 오른쪽 반)의 픽셀을 만듭니다."""
    half = kMaskSize // 2
    pixels = bytearray()
    for _ in range(kMaskSize):
        for x in range(kMaskSize):
            pixels.append(kMaskValues[0] if x < half else kMaskValues[1])
    return kMaskSize, kMaskSize, pixels


def formatFloat2Internal(*listValue: float) -> str:
    """엔진의 벡터 칸 글(`"0,0.5"`)을 만듭니다 — 숫자는 가장 짧은 표기이고 정수 값은 소수점이 없습니다(`std::to_chars`)."""
    return ",".join(str(int(value)) if float(value).is_integer() else repr(float(value)) for value in listValue)


def makeFrameInternal(u: float, v: float) -> dict:
    """칸 하나(반 폭 · 반 높이, 250 ms, 테두리 없음)인 프레임입니다. 키 순서는 `SpriteClipFrame` 의 PROPERTY 순서입니다."""
    return {"_uvRect": formatFloat2Internal(u, v, 0.5, 0.5), "_borderPixels": formatFloat2Internal(0, 0, 0, 0), "_durationMs": 250}


def makeQuadrantTextureInternal() -> tuple[int, int, bytearray, list[dict]]:
    """네 칸 테스트 텍스처의 픽셀과, 칸마다 한 프레임(250 ms)인 클립 프레임 목록을 만듭니다."""
    half = kQuadrantSize // 2
    pixels = bytearray()
    for y in range(kQuadrantSize):
        for x in range(kQuadrantSize):
            quadrant = (1 if x >= half else 0) + (2 if y >= half else 0)
            pixels += bytes(kQuadrantColors[quadrant])
    listFrame = [makeFrameInternal(0.0, 0.0), makeFrameInternal(0.5, 0.0), makeFrameInternal(0.5, 0.5), makeFrameInternal(0.0, 0.5)]
    return kQuadrantSize, kQuadrantSize, pixels, listFrame


def makeMissingTextureInternal() -> tuple[int, int, bytearray]:
    """누락 텍스처(마젠타 · 검정 체커)의 픽셀을 만듭니다."""
    pixels = bytearray()
    for y in range(kMissingTextureSize):
        for x in range(kMissingTextureSize):
            cell = (x // kMissingCellSize + y // kMissingCellSize) % 2
            pixels += bytes(kMissingColors[cell])
    return kMissingTextureSize, kMissingTextureSize, pixels


#: 목록 칸 이름 → 원소 요소 이름(`SpriteClipAsset` 의 PROPERTY 목록 원소 타입).
kListItemTag: dict[str, str] = {"_listFrame": "SpriteClipFrame", "_listAnimation": "SpriteClipAnimation"}


def appendXmlElementInternal(listLine: list[str], tag: str, fields: dict, depth: int) -> None:
    """엔진 XML 직렬화와 같은 모양의 요소 하나를 줄로 더합니다 — 스칼라 칸은 속성(PROPERTY 순서), 목록 칸은 자식 요소, 들여쓰기는 탭, 빈 요소는 ` />`."""
    indent = "\t" * depth
    attributes = "".join(f' {key}="{value}"' for key, value in fields.items() if not isinstance(value, list))
    listChild = [(key, value) for key, value in fields.items() if isinstance(value, list)]
    if not listChild:
        listLine.append(f"{indent}<{tag}{attributes} />")
        return
    listLine.append(f"{indent}<{tag}{attributes}>")
    for key, listItem in listChild:
        if not listItem:
            listLine.append(f"{indent}\t<{key} />")
            continue
        listLine.append(f"{indent}\t<{key}>")
        for item in listItem:
            appendXmlElementInternal(listLine, kListItemTag[key], item, depth + 2)
        listLine.append(f"{indent}\t</{key}>")
    listLine.append(f"{indent}</{tag}>")


def makeClipText(atlasPath: str, listFrame: list, listAnimation: list) -> str:
    """`SpriteClipAsset` 의 XML 직렬화와 같은 칸 · 순서의 클립 글(`.sprite.xml`)을 만듭니다. 아틀라스 크기는 테두리가 없어 0 입니다. 끝은 줄바꿈 하나다."""
    clip = {"_atlasPath": atlasPath, "_atlasPixelSize": formatFloat2Internal(0, 0), "_listFrame": listFrame, "_listKey": [], "_listAnimation": listAnimation}
    listLine: list[str] = []
    appendXmlElementInternal(listLine, "SpriteClipAsset", clip, 0)
    return "\n".join(listLine) + "\n"


def writeFileInternal(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print(f"wrote {path} ({len(data)} bytes)")


def generate(repositoryRoot: str) -> None:
    """다섯 파일을 씁니다. 리소스 경로는 소문자입니다(CheckResourceCasing)."""
    resourceRoot = Path(repositoryRoot) / "Resource"

    width, height, pixels, listFrame = makeQuadrantTextureInternal()
    writeFileInternal(resourceRoot / kQuadrantsTexturePath, makeDdsBytes(width, height, pixels))
    listAnimation = [{"_name": "cycle", "_firstFrame": 0, "_frameCount": 4, "_bLoop": 1, "_listNotify": []}]
    writeFileInternal(resourceRoot / "engine/textures/test/quadrants.sprite.xml",
                      makeClipText(kQuadrantsTexturePath, listFrame, listAnimation).encode("utf-8"))
    width, height, maskPixels = makeMaskTextureInternal()
    writeFileInternal(resourceRoot / kMaskTexturePath, makeR8DdsBytes(width, height, maskPixels))
    width, height, pixels = makeMissingTextureInternal()
    writeFileInternal(resourceRoot / kMissingTexturePath, makeDdsBytes(width, height, pixels))
    loadingPixels = bytes(kLoadingColor) * (kLoadingTextureSize * kLoadingTextureSize)
    writeFileInternal(resourceRoot / kLoadingTexturePath, makeDdsBytes(kLoadingTextureSize, kLoadingTextureSize, loadingPixels))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]), help="repository root")
    args = parser.parse_args(argv)
    generate(args.root)
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
