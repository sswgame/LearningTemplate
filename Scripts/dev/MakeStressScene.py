#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
로드 경로를 재기 위한 **큰 씬**을 만듭니다.

왜 필요한가: 저장소의 씬은 작아서 씬 로드 시간을 측정할 수 없다.
`GT.*` 프로파일은 프레임 안만 보고, 로드는 프레임 밖에서 한 번 일어난다 — 측정하려면 먼저
**측정할 만한 워크로드**가 있어야 한다.

무엇을 만드는가: 메시가 섞인 격자다. 도형을 여러 종류 섞는 이유는 `_meshID` 마다
`MeshUtil::createPrimitive` 가 다른 정점 수를 만들기 때문이다 — 큐브만 N 개면 로드 시간의
메시 생성 몫이 한 종류에만 쏠려 실제 씬과 다른 그림이 나온다.

사용법:
  py -3 Scripts/dev/MakeStressScene.py --count 4000
  py -3 Scripts/dev/MakeStressScene.py --count 8000 --out Resource/game/empty/maps/stress8000.scene.xml
  py -3 Scripts/dev/MakeStressScene.py --count 4000 --point-lights 1024   # 클러스터드 라이팅 측정(ClusteredLighting.md S3)

만들어진 씬은 **커밋하지 않는다**(`Resource/` 는 배포되는 콘텐츠 트리다). 재고 나서 지운다.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common.Paths import getProjectRoot

# `MeshUtil::createPrimitive` 가 아는 이름들. 정점 수가 서로 달라야 섞는 의미가 있다.
_kListShape = ("Cube", "Sphere", "Cylinder", "Capsule", "Cone", "Quad")

# 한 프레임에 GPU 로 보내는 라이트 수 상한(bindingslots.hlsli SW_MAX_FRAME_LIGHT). 넘으면 엔진이 잘라 보내 측정이 그 수를 재지 않는다.
_kMaxFrameLight = 1024

# 엔티티마다 0 이 아닌 파일 id(`id`)를 적는다 — 씬 로더는 id 없는 엔티티를 받지 않는다. 카메라 1, 빛 2, 메시는 3 부터.
_kCameraEntity = """\t\t<entity id="1" name="GameCamera">
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

_kLightEntity = """\t\t<entity id="2" name="KeyLight">
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
        '\t\t<entity id="%d" name="Mesh_%d">\n'
        '\t\t\t<GameObject _schemaVersion="0" _name="Mesh_%d" _bActive="true">\n'
        "\t\t\t\t<_listComponent>\n"
        '\t\t\t\t\t<MeshComponent _localPosition="%g,%g,%g"\n'
        '\t\t\t\t\t               _localRotation="0,%g,0"\n'
        '\t\t\t\t\t               _localScale="1,1,1"\n'
        '\t\t\t\t\t               _attachOwner="None"\n'
        '\t\t\t\t\t               _attachComponent="None"\n'
        '\t\t\t\t\t               _meshID="%s"\n'
        '\t\t\t\t\t               _boundsRadius="0.866"\n'
        '\t\t\t\t\t               _blendMode="null"\n'
        '\t\t\t\t\t               _gpuSpinSeed="%d" />\n'
        "\t\t\t\t</_listComponent>\n"
        "\t\t\t</GameObject>\n"
        "\t\t</entity>\n" % (index + 3, index, index, x, y, z, (index % 8) * 0.39, shape, index + 1)
    )


def buildPointLightEntity(index: int, entityId: int, x: float, z: float, radius: float) -> str:
    """점광 엔티티 하나를 만듭니다. 색은 번호로 돌려 클러스터 경계가 그림에 보이게 한다."""
    hue = index % 3
    color = ("1,0.6,0.4", "0.4,1,0.6", "0.5,0.6,1")[hue]
    return (
        '\t\t<entity id="%d" name="PointLight_%d">\n'
        '\t\t\t<GameObject _schemaVersion="0" _name="PointLight_%d" _bActive="true">\n'
        "\t\t\t\t<_listComponent>\n"
        '\t\t\t\t\t<PointLightComponent _localPosition="%g,1.5,%g"\n'
        '\t\t\t\t\t                     _localRotation="0,0,0"\n'
        '\t\t\t\t\t                     _localScale="1,1,1"\n'
        '\t\t\t\t\t                     _attachOwner="None"\n'
        '\t\t\t\t\t                     _attachComponent="None"\n'
        '\t\t\t\t\t                     _color="%s"\n'
        '\t\t\t\t\t                     _intensity="2"\n'
        '\t\t\t\t\t                     _radius="%g" />\n'
        "\t\t\t\t</_listComponent>\n"
        "\t\t\t</GameObject>\n"
        "\t\t</entity>\n" % (entityId, index, index, x, z, color, radius)
    )


def buildScene(count: int, shapeCount: int, pointLightCount: int) -> str:
    """엔티티 count 개짜리 씬 XML 을 만듭니다. 점광은 메시 격자 위에 고르게 흩는다(반경은 격자 간격의 세 배 — 클러스터 몇 개씩 덮는다)."""
    side = 1
    while side * side < count:
        side += 1
    spacing = 2.0
    origin = -0.5 * float(side - 1) * spacing

    # 월드 설정은 씬 형식 판 2부터 필수다(`WorldSettings`) — 측정 씬은 스트리밍하지 않는다.
    listPart = [
        '<Scene formatVersion="2" name="StressScene">\n'
        '\t<WorldSettings _bStreaming="false" _unloadMargin="0" _maxCellLoadsPerFrame="0">\n'
        '\t\t<_cellGrid _cellSize="0" _loadingRadius="0" />\n'
        "\t</WorldSettings>\n"
        "\t<entities>\n",
        _kCameraEntity,
        _kLightEntity,
    ]
    for index in range(count):
        col = index % side
        row = index // side
        shape = _kListShape[index % shapeCount]
        listPart.append(
            buildMeshEntity(index, origin + col * spacing, 0.0, origin + row * spacing, shape)
        )
    lightSide = 1
    while lightSide * lightSide < pointLightCount:
        lightSide += 1
    lightSpacing = float(side) * spacing / float(lightSide)
    lightOrigin = origin + 0.5 * lightSpacing
    for lightIndex in range(pointLightCount):
        lightX = lightOrigin + (lightIndex % lightSide) * lightSpacing
        lightZ = lightOrigin + (lightIndex // lightSide) * lightSpacing
        listPart.append(buildPointLightEntity(lightIndex, count + 3 + lightIndex, lightX, lightZ, spacing * 3.0))
    listPart.append("\t</entities>\n</Scene>\n")
    return "".join(listPart)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="로드 측정용 큰 씬을 만듭니다.")
    parser.add_argument("--count", type=int, default=4000, help="메시 엔티티 수")
    parser.add_argument(
        "--shapes",
        type=int,
        default=len(_kListShape),
        help="섞을 도형 종류 수 (1 이면 큐브만)",
    )
    parser.add_argument("--point-lights", type=int, default=0, help="메시 격자 위에 흩을 점광 수(클러스터드 라이팅 측정, 상한 1024)")
    parser.add_argument("--out", default="", help="쓸 경로 (기본: Resource/game/empty/maps/stress<count>.scene.xml)")
    args = parser.parse_args(argv)

    if args.count <= 0:
        print("--count 는 1 이상이어야 합니다.")
        return 2
    shapeCount = max(1, min(args.shapes, len(_kListShape)))
    if not 0 <= args.point_lights <= _kMaxFrameLight:
        print("--point-lights 는 0 이상 %d 이하여야 합니다(SW_MAX_FRAME_LIGHT)." % _kMaxFrameLight)
        return 2

    root = getProjectRoot()
    outPath = (
        pathlib.Path(args.out)
        if args.out
        else root / "Resource" / "game" / "empty" / "maps" / ("stress%d.scene.xml" % args.count)
    )
    if outPath.is_absolute() is False:
        outPath = root / outPath

    text = buildScene(args.count, shapeCount, args.point_lights)
    outPath.parent.mkdir(parents=True, exist_ok=True)
    outPath.write_text(text, encoding="utf-8", newline="\n")

    print("씬을 만들었습니다: %s" % outPath)
    print("  엔티티 %d개 (메시 %d + 카메라 1 + 방향광 1 + 점광 %d), 도형 %d종, %.1f KB"
          % (args.count + 2 + args.point_lights, args.count, args.point_lights, shapeCount, len(text) / 1024.0))
    print("  이 파일은 커밋하지 마세요 — 재고 나서 지웁니다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
