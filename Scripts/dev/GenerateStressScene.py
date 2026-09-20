#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
로드 경로를 재기 위한 **큰 씬**을 만듭니다.

왜 필요한가: 이 저장소에 있던 씬은 5 KB 짜리 하나뿐이라 씬 로드 시간을 잴 수가 없었다.
`GT.*` 프로파일은 프레임 안만 보고, 로드는 프레임 밖에서 한 번 일어난다 — 재려면 먼저
**잴 만한 워크로드**가 있어야 한다.

무엇을 만드는가: 메시가 섞인 격자다. 도형을 여러 종류 섞는 이유는 `_meshId` 마다
`MeshUtil::createPrimitive` 가 다른 정점 수를 만들기 때문이다 — 큐브만 N 개면 로드 시간의
메시 생성 몫이 한 종류에만 쏠려 실제 씬과 다른 그림이 나온다.

사용법:
  py -3 Scripts/dev/GenerateStressScene.py --count 4000
  py -3 Scripts/dev/GenerateStressScene.py --count 8000 --out Resource/game/empty/maps/stress8000.scene.xml

만들어진 씬은 **커밋하지 않는다**(`Resource/` 는 배포되는 콘텐츠 트리다). 재고 나서 지운다.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common import getProjectRoot, useUtf8Stdout

# `MeshUtil::createPrimitive` 가 아는 이름들. 정점 수가 서로 달라야 섞는 의미가 있다.
_kListShape = ("Cube", "Sphere", "Cylinder", "Capsule", "Cone", "Quad")

_kCameraEntity = """\t\t<entity name="GameCamera">
\t\t\t<GameObject _schemaVersion="0" _name="GameCamera" _bActive="true">
\t\t\t\t<_listComponent>
\t\t\t\t\t<CameraComponent _localPosition="0,40,-90"
\t\t\t\t\t                 _localRotation="0.35877067,0,0"
\t\t\t\t\t                 _localScale="1,1,1"
\t\t\t\t\t                 _attachOwner="None"
\t\t\t\t\t                 _attachComponent="None"
\t\t\t\t\t                 _fovY="0.7"
\t\t\t\t\t                 _nearZ="0.1"
\t\t\t\t\t                 _farZ="1000"
\t\t\t\t\t                 _orthoHeight="10"
\t\t\t\t\t                 _priority="0"
\t\t\t\t\t                 _role="Game"
\t\t\t\t\t                 _bOrthographic="false" />
\t\t\t\t</_listComponent>
\t\t\t</GameObject>
\t\t</entity>
"""

_kLightEntity = """\t\t<entity name="KeyLight">
\t\t\t<GameObject _schemaVersion="0" _name="KeyLight" _bActive="true">
\t\t\t\t<_listComponent>
\t\t\t\t\t<DirectionalLightComponent _localPosition="0,10,0"
\t\t\t\t\t                           _localRotation="0.9,0.4,0"
\t\t\t\t\t                           _localScale="1,1,1"
\t\t\t\t\t                           _attachOwner="None"
\t\t\t\t\t                           _attachComponent="None"
\t\t\t\t\t                           _intensity="1.6" />
\t\t\t\t</_listComponent>
\t\t\t</GameObject>
\t\t</entity>
"""


def buildMeshEntity(index: int, x: float, y: float, z: float, shape: str) -> str:
    """메시 엔티티 하나를 만듭니다."""
    return (
        '\t\t<entity name="Mesh_%d">\n'
        '\t\t\t<GameObject _schemaVersion="0" _name="Mesh_%d" _bActive="true">\n'
        "\t\t\t\t<_listComponent>\n"
        '\t\t\t\t\t<MeshComponent _localPosition="%g,%g,%g"\n'
        '\t\t\t\t\t               _localRotation="0,%g,0"\n'
        '\t\t\t\t\t               _localScale="1,1,1"\n'
        '\t\t\t\t\t               _attachOwner="None"\n'
        '\t\t\t\t\t               _attachComponent="None"\n'
        '\t\t\t\t\t               _meshId="%s"\n'
        '\t\t\t\t\t               _boundsRadius="0.866"\n'
        '\t\t\t\t\t               _blendMode="null"\n'
        '\t\t\t\t\t               _gpuSpinSeed="%d" />\n'
        "\t\t\t\t</_listComponent>\n"
        "\t\t\t</GameObject>\n"
        "\t\t</entity>\n" % (index, index, x, y, z, (index % 8) * 0.39, shape, index + 1)
    )


def buildScene(count: int, shapeCount: int) -> str:
    """엔티티 count 개짜리 씬 XML 을 만듭니다."""
    side = 1
    while side * side < count:
        side += 1
    spacing = 2.0
    origin = -0.5 * float(side - 1) * spacing

    listPart = ['<Scene formatVersion="0" name="StressScene">\n\t<entities>\n', _kCameraEntity, _kLightEntity]
    for index in range(count):
        col = index % side
        row = index // side
        shape = _kListShape[index % shapeCount]
        listPart.append(
            buildMeshEntity(index, origin + col * spacing, 0.0, origin + row * spacing, shape)
        )
    listPart.append("\t</entities>\n</Scene>\n")
    return "".join(listPart)


def main(argv: list[str] | None = None) -> int:
    useUtf8Stdout()
    parser = argparse.ArgumentParser(description="로드 측정용 큰 씬을 만듭니다.")
    parser.add_argument("--count", type=int, default=4000, help="메시 엔티티 수")
    parser.add_argument(
        "--shapes",
        type=int,
        default=len(_kListShape),
        help="섞을 도형 종류 수 (1 이면 큐브만)",
    )
    parser.add_argument("--out", default="", help="쓸 경로 (기본: Resource/game/empty/maps/stress<count>.scene.xml)")
    args = parser.parse_args(argv)

    if args.count <= 0:
        print("--count 는 1 이상이어야 합니다.")
        return 2
    shapeCount = max(1, min(args.shapes, len(_kListShape)))

    root = getProjectRoot()
    outPath = (
        pathlib.Path(args.out)
        if args.out
        else root / "Resource" / "game" / "empty" / "maps" / ("stress%d.scene.xml" % args.count)
    )
    if outPath.is_absolute() is False:
        outPath = root / outPath

    text = buildScene(args.count, shapeCount)
    outPath.parent.mkdir(parents=True, exist_ok=True)
    outPath.write_text(text, encoding="utf-8", newline="\n")

    print("씬을 만들었습니다: %s" % outPath)
    print("  엔티티 %d개 (메시 %d + 카메라 1 + 라이트 1), 도형 %d종, %.1f KB"
          % (args.count + 2, args.count, shapeCount, len(text) / 1024.0))
    print("  이 파일은 커밋하지 마세요 — 재고 나서 지웁니다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
