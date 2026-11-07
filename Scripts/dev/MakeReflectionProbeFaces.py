"""반사 프로브 시험 데이터를 짓는다 — 면마다 색이 다른 고정 `.reflprobe`(Content/game/empty/maps/reflectionprobe.reflprobe).

시나리오 game/empty/automation/reflectionprobe 의 무대가 읽는다. 에디터 굽기(lighting.bakeReflectionProbes, GPU)는 씬 옆 파일을 다시 쓰고
결과가 GPU · 드라이버마다 조금씩 달라 체인에 두지 않는다 — 대신 판이 반사하는 방향이 무슨 색을 받는지 뚜렷한 데이터를 이 스크립트로 짓는다.
  +X 빨강 · -X 파랑 · +Y 흰색 · -Y 어두운 회색 · +Z 초록 · -Z 노랑(텍셀 방향의 가장 큰 축). 단계 1(거칠기 1)은 `0.5 · 면 색 + 0.6`(흐리고 밝다).
파일 배치 · 옥타헤드럴 식은 Engine/Graphics/Lighting/ReflectionProbeAsset.h(makeBytes · decodeOctahedral)와 같다 — 식을 고치면 이 파일도 고친다.

  py -3 Scripts/dev/MakeReflectionProbeFaces.py           # 쓴다
  py -3 Scripts/dev/MakeReflectionProbeFaces.py --check   # 저장소 파일과 바이트가 같은지만 본다(다르면 1)
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[2]
_OUTPUT = _ROOT / "Content/game/empty/maps/reflectionprobe.reflprobe"

_MAGIC = b"SWRP"
_VERSION = 1
_CAPTURE_POSITION = (0.0, 2.0, 0.0)  # 씬의 ReflectionProbeComponent 위치와 같아야 읽힌다
_RESOLUTION = 16
_LEVEL_COUNT = 2
_FACE_COLOR = {
    "+X": (3.0, 0.2, 0.2),
    "-X": (0.2, 0.2, 3.0),
    "+Y": (2.0, 2.0, 2.0),
    "-Y": (0.2, 0.2, 0.2),
    "+Z": (0.2, 3.0, 0.2),
    "-Z": (2.0, 2.0, 0.2),
}


def decodeOctahedral(u: float, v: float) -> tuple[float, float, float]:
    """옥타헤드럴 UV([0, 1]²)를 길이 1 의 방향으로 편다(+Y 가 가운데) — ReflectionProbeAsset::decodeOctahedral 과 같다."""
    u = u * 2.0 - 1.0
    v = v * 2.0 - 1.0
    y = 1.0 - abs(u) - abs(v)
    fold = max(-y, 0.0)
    x = u + (-fold if u >= 0.0 else fold)
    z = v + (-fold if v >= 0.0 else fold)
    length = math.sqrt(x * x + y * y + z * z)
    return (x / length, y / length, z / length)


def findFaceColor(direction: tuple[float, float, float]) -> tuple[float, float, float]:
    """방향의 가장 큰 축이 고르는 면의 색이다."""
    axis = max(range(3), key=lambda index: abs(direction[index]))
    return _FACE_COLOR[("+" if direction[axis] >= 0.0 else "-") + "XYZ"[axis]]


def makeBytes() -> bytes:
    """`.reflprobe` 바이트(리틀 엔디언): 매직 · 버전 · 찍은 위치 · 한 변 텍셀 수 · 단계 수 · 텍셀(단계 → 행 → 열) float32 RGB."""
    listChunk = [_MAGIC, struct.pack("<I", _VERSION), struct.pack("<3f", *_CAPTURE_POSITION), struct.pack("<II", _RESOLUTION, _LEVEL_COUNT)]
    for level in range(_LEVEL_COUNT):
        for row in range(_RESOLUTION):
            for column in range(_RESOLUTION):
                color = findFaceColor(decodeOctahedral((column + 0.5) / _RESOLUTION, (row + 0.5) / _RESOLUTION))
                if level > 0:
                    color = tuple(0.5 * channel + 0.6 for channel in color)
                listChunk.append(struct.pack("<3f", *color))
    return b"".join(listChunk)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="반사 프로브 시험 데이터(면마다 색이 다른 .reflprobe)를 짓는다")
    parser.add_argument("--check", action="store_true", help="쓰지 않고 저장소 파일과 같은지만 본다")
    arguments = parser.parse_args(argv)
    data = makeBytes()
    if arguments.check:
        bSame = _OUTPUT.is_file() and _OUTPUT.read_bytes() == data
        print(f"{_OUTPUT.relative_to(_ROOT)}: {'같다' if bSame else '다르다 — 스크립트를 돌려 다시 쓴다'}")
        return 0 if bSame else 1
    _OUTPUT.write_bytes(data)
    print(f"{_OUTPUT.relative_to(_ROOT)}: {len(data)} B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
