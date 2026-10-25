"""게임 씬이 들여온 CC0 모델(`models/*.mesh`)을 부르게 합니다 — 자리 표시 큐브를 바꾸거나 전시 줄에 놓고, 아무도 부르지 않는 메시가 남지 않게 합니다.

배치는 데이터(`Config/Editor/Cc0PropPlacement.json`)가 정합니다. 게임마다 씬 · 머티리얼과 두 가지 규칙을 적습니다.
  - `replace`: 이름이 `entityPrefix` 로 시작하는 엔티티의 컴포넌트 목록을 메시 하나로 바꿉니다. 자리는 첫 큐브의 바닥(중심 y − 배율 y / 2)이고
    메시는 `meshes` 를 차례로 돌려 씁니다(선인장 여덟 개에 선인장 메시 둘이면 번갈아). 배율은 `scale` 입니다.
  - `showcases`: 줄마다 남은 메시를 `origin` 에서 `step` 간격으로 한 줄에 놓습니다(엔티티 이름 `Cc0.<메시 이름>`, 머티리얼은 줄마다). 게임 영역 밖에 두어 규칙에 끼지 않습니다.
같은 엔티티 이름이 이미 있으면 다시 놓지 않습니다(여러 번 돌려도 같은 씬). 다 놓은 뒤 `models/` 의 메시 가운데 씬 · 프리팹 · 데이터 어디에서도 부르지 않는 것이
있으면 그 목록을 알리고 실패합니다(에셋 검증 `orphans` 와 같은 판정). 머티리얼 · 메시 파일이 없으면 씬을 고치지 않고 실패합니다.

화면은 이 스크립트가 보지 않습니다 — 놓은 뒤 게임을 띄워 크기 · 머티리얼 색을 눈으로 확인합니다(계획 `GameSceneCc0Meshes`).

사용법: py -3 Scripts/dev/MakeCc0SceneProps.py [--root <repo>] [--game <이름>] [--check]
  --check  씬을 고치지 않고, 놓을 것이 남았거나 부르지 않는 메시가 있으면 실패합니다.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402

kPlacementConfig = "Config/Editor/Cc0PropPlacement.json"
kEntityPattern = re.compile(r'\t\t<entity id="(\d+)" name="([^"]*)">.*?\n\t\t</entity>\n', re.S)


def formatNumberInternal(value: float) -> str:
    """씬의 숫자 칸 글(가장 짧은 표기, 정수는 소수점 없이)입니다."""
    return str(int(value)) if float(value).is_integer() else repr(round(float(value), 6))


def makeMeshComponentInternal(mesh: str, material: str, position: Sequence[float], scale: Sequence[float], rotation: Sequence[float] = (0, 0, 0)) -> str:
    """MeshComponent 한 줄 묶음입니다(씬 파일의 들여쓰기 그대로)."""
    pad = "\t\t\t\t\t               "
    return ("\t\t\t\t\t<MeshComponent _componentName=\"MeshComponent\"\n"
            f"{pad}_bActive=\"true\"\n"
            f"{pad}_localPosition=\"{','.join(formatNumberInternal(v) for v in position)}\"\n"
            f"{pad}_localRotation=\"{','.join(formatNumberInternal(v) for v in rotation)}\"\n"
            f"{pad}_localScale=\"{','.join(formatNumberInternal(v) for v in scale)}\"\n"
            f"{pad}_attachOwner=\"None\"\n"
            f"{pad}_attachOwnerID=\"0\"\n"
            f"{pad}_attachComponent=\"None\"\n"
            f"{pad}_meshID=\"{mesh}\"\n"
            f"{pad}_materialPath=\"{material}\"\n"
            f"{pad}_boundsRadius=\"0.866\"\n"
            f"{pad}_blendMode=\"Opaque\"\n"
            f"{pad}_gpuSpinSeed=\"0\"\n"
            f"{pad}_bVisible=\"true\" />\n")


def makeEntityInternal(entityID: int, name: str, componentText: str) -> str:
    return (f"\t\t<entity id=\"{entityID}\" name=\"{name}\">\n"
            f"\t\t\t<GameObject _schemaVersion=\"0\" _name=\"{name}\" _bActive=\"true\">\n"
            "\t\t\t\t<_listComponent>\n"
            f"{componentText}"
            "\t\t\t\t</_listComponent>\n"
            "\t\t\t</GameObject>\n"
            "\t\t</entity>\n")


def parseVectorInternal(text: str) -> list[float]:
    return [float(part) for part in text.split(",")]


def applyReplaceInternal(sceneText: str, rule: dict, material: str, game: str) -> tuple[str, int]:
    """이름이 접두어로 시작하는 엔티티를 메시 하나로 바꿉니다. 바꾼 수를 돌려줍니다(이미 바뀐 것은 세지 않습니다)."""
    meshes = [f"game/{game}/models/{mesh}" for mesh in rule["meshes"]]
    scale = rule["scale"]
    replacedCount = 0
    meshIndex = 0
    result = []
    lastEnd = 0
    for match in kEntityPattern.finditer(sceneText):
        entityID, name = int(match.group(1)), match.group(2)
        block = match.group(0)
        if name.startswith(rule["entityPrefix"]) is False or '_meshID="Cube"' not in block:
            continue
        # 메시만 든 자리 표시만 바꾼다 — 콜라이더 · 게임 컴포넌트가 든 엔티티를 통째로 바꾸면 규칙이 사라진다.
        if any(kind != "MeshComponent" for kind in re.findall(r"<(\w+) _componentName=", block)):
            raise ValueError(f"{game}: '{name}' has components other than MeshComponent - replace it by hand")
        position = parseVectorInternal(re.search(r'_localPosition="([^"]*)"', block).group(1))
        cubeScale = parseVectorInternal(re.search(r'_localScale="([^"]*)"', block).group(1))
        base = [position[0], position[1] - cubeScale[1] * 0.5, position[2]]
        rotation = parseVectorInternal(re.search(r'_localRotation="([^"]*)"', block).group(1))
        component = makeMeshComponentInternal(meshes[meshIndex % len(meshes)], rule.get("material", material), base, scale, rotation)
        meshIndex += 1
        result.append(sceneText[lastEnd:match.start()])
        result.append(makeEntityInternal(entityID, name, component))
        lastEnd = match.end()
        replacedCount += 1
    result.append(sceneText[lastEnd:])
    return "".join(result), replacedCount


def collectUsedReferencesInternal(gameRoot: Path) -> str:
    text = []
    for path in list(gameRoot.rglob("*.xml")) + list(gameRoot.rglob("*.json")):
        text.append(path.read_text(encoding="utf-8", errors="ignore"))
    return "\n".join(text)


def placeGame(repositoryRoot: Path, entry: dict, bCheckOnly: bool) -> list[str]:
    """게임 하나를 놓습니다. 문제 목록을 돌려줍니다(비면 성공)."""
    game = entry["game"]
    resourceRoot = repositoryRoot / "Resource"
    gameRoot = resourceRoot / "game" / game
    scenePath = resourceRoot / entry["scene"]
    material = entry["material"]
    problems: list[str] = []
    for requiredPath in [scenePath, resourceRoot / material] + [resourceRoot / rule.get("material", material) for rule in entry.get("replace", [])] + [resourceRoot / showcase.get("material", material) for showcase in entry.get("showcases", [])]:
        if requiredPath.is_file() is False:
            problems.append(f"{game}: missing file {requiredPath.relative_to(repositoryRoot).as_posix()}")
    for mesh in [mesh for rule in entry.get("replace", []) for mesh in rule["meshes"]] + [mesh for showcase in entry.get("showcases", []) for mesh in showcase["meshes"]]:
        if (gameRoot / "models" / mesh).is_file() is False:
            problems.append(f"{game}: missing mesh models/{mesh}")
    if problems:
        return problems

    # 줄 끝은 파일의 것을 지킨다 — 안에서는 LF 로 다루고 쓸 때 되돌린다.
    rawText = scenePath.read_bytes().decode("utf-8")
    bCrlf = "\r\n" in rawText
    original = rawText.replace("\r\n", "\n")
    sceneText = original
    for rule in entry.get("replace", []):
        sceneText, _ = applyReplaceInternal(sceneText, rule, material, game)
    for showcase in entry.get("showcases", []):
        existingNames = {match.group(2) for match in kEntityPattern.finditer(sceneText)}
        nextID = max([int(match.group(1)) for match in kEntityPattern.finditer(sceneText)] + [0]) + 1
        origin, step = showcase["origin"], showcase["step"]
        newEntities = []
        for index, mesh in enumerate(showcase["meshes"]):
            name = "Cc0." + Path(mesh).stem
            if name in existingNames:
                continue
            position = [origin[axis] + step[axis] * index for axis in range(3)]
            component = makeMeshComponentInternal(f"game/{game}/models/{mesh}", showcase.get("material", material), position, showcase.get("scale", [1, 1, 1]))
            newEntities.append(makeEntityInternal(nextID, name, component))
            nextID += 1
        if newEntities:
            closing = sceneText.rfind("\t</entities>")
            if closing < 0:
                return [f"{game}: scene has no </entities>"]
            sceneText = sceneText[:closing] + "".join(newEntities) + sceneText[closing:]

    if bCheckOnly and sceneText != original:
        problems.append(f"{game}: {entry['scene']} still has props to place (run without --check)")
    if bCheckOnly is False and sceneText != original:
        scenePath.write_bytes((sceneText.replace("\n", "\r\n") if bCrlf else sceneText).encode("utf-8"))
        print(f"[MakeCc0SceneProps] {game}: updated {entry['scene']}")

    used = collectUsedReferencesInternal(gameRoot) if bCheckOnly is False else collectUsedReferencesInternal(gameRoot) + sceneText
    for mesh in sorted((gameRoot / "models").rglob("*.mesh")):
        key = f"game/{game}/{mesh.relative_to(gameRoot).as_posix()}"
        if key not in used and mesh.relative_to(gameRoot / "models").as_posix() not in entry.get("ignore", []):
            problems.append(f"{game}: nothing references {key}")
    return problems


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]), help="저장소 루트")
    parser.add_argument("--game", default="", help="이 게임만(비우면 모두)")
    parser.add_argument("--check", action="store_true", help="고치지 않고 확인만")
    args = parser.parse_args(argv)
    repositoryRoot = Path(args.root)
    placement = json.loads((repositoryRoot / kPlacementConfig).read_text(encoding="utf-8"))
    problems: list[str] = []
    for entry in placement["games"]:
        if args.game and entry["game"] != args.game:
            continue
        problems += placeGame(repositoryRoot, entry, args.check)
    for problem in problems:
        print(f"[MakeCc0SceneProps] {problem}", file=sys.stderr)
    return ExitCode.Violation if problems else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
