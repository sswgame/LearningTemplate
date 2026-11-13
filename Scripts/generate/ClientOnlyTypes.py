#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/ClientOnlyTypes.py

전용 서버가 짓지 않는 소스(`SW_EXCLUDED_ENGINE_FOLDERS` 의 엔진 폴더 — 오디오 · 렌더러 · UI, `…/Client/`)의 리플렉션 형식 이름을 표 `Config/Engine/ClientOnlyTypes.generated.json` 으로 모은다.

- 왜 표인가: 서버는 그 소스를 짓지 않아 형식을 모른다. 씬 · 프리팹에 HUD · 마커 · 월드 위젯 · 소리(에미터 · 리스너 · 잔향 존) 컴포넌트가 있으면 서버는 모르는 형식을 만나는데,
  이름 바꾸기 · 빠진 모듈(오류)과 클라이언트 전용(정상)을 가르려면 이 이름들을 서버도 알아야 한다. 서버의 직렬화기는 표의 형식이면 만들지 않고 조용히
  건너뛴다(`ClientOnlyTypeTable` — 언리얼은 클래스를 서버도 지어 두고 액터를 띄우지 않는다. 여기는 클래스를 짓지 않으니 이름만 싣는다 — 기억).
- 표를 저장소에 두는 까닭: 배포 서버가 그대로 싣고, 커밋 훅이 빌드 없이 낡음을 잡는다(`--check`).

`py -3 -m Scripts client-only-types [--check]`.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.FactCache import openFacts  # noqa: E402
from lint.LintScope import LintScope, listRepoFiles, makeWholeScope  # noqa: E402
from generate.TargetLayers import readServerUnbuiltFolders  # noqa: E402 — 짓지 않는 엔진 폴더의 정본은 BuildLayout.cmake 한 곳이다

kTablePath = "Config/Engine/ClientOnlyTypes.generated.json"
kReflectedType = re.compile(r"^\s*REFLECT\s*\([^\n]*\)\s*\n\s*(?:class|struct)\s+(?:SW_\w+\s+)?(\w+)", re.M)


def isClientOnlySource(relativePath: str, listUnbuiltFolder: list[str]) -> bool:
    """전용 서버가 짓지 않는 소스인가(`SW_EXCLUDED_ENGINE_FOLDERS` 의 엔진 폴더 · `SW_CLIENT_ONLY_SOURCE_PATTERNS` 의 `/Client/`)."""
    return any(relativePath.startswith(f"Source/Engine/{folder}/") for folder in listUnbuiltFolder) or "/Client/" in relativePath


def collectTypeNames(projectRoot: Path) -> list[str]:
    """클라이언트 전용 헤더의 리플렉션 형식 이름 — 내용이 바뀐 헤더만 다시 읽는다(`lint/FactCache.py`, 못 읽는 글은 include-order 가 막는다)."""
    listUnbuiltFolder = readServerUnbuiltFolders(projectRoot)
    listPath = [path for path in listRepoFiles(projectRoot)
                if path.startswith("Source/") and path.endswith(".h") and "/Tests/" not in path and isClientOnlySource(path, listUnbuiltFolder)]
    facts = openFacts(projectRoot, "client-only-types", [__file__], listPath, lambda _, text: kReflectedType.findall(text), contribute=lambda _, listName: listName)
    return sorted(facts.summary)   # 요약(이름 → 그 이름을 정의한 파일 수)만 읽는다


def makeTableText(listName: list[str]) -> str:
    table = {
        "_comment": "생성 파일 — Scripts/generate/ClientOnlyTypes.py 가 쓴다(고치지 않는다). 전용 서버가 짓지 않는 소스의 리플렉션 형식 이름이다. 서버는 씬에서 이 형식을 만나면 만들지 않고 건너뛴다.",
        "listType": listName,
    }
    return json.dumps(table, ensure_ascii=False, indent=4) + "\n"


def checkClientOnlyTypes(projectRoot: Path, scope: LintScope) -> list[str]:
    """표가 소스와 같으면 빈 목록입니다. 표는 저장소 전체로 짓고(색인), 커밋 훅은 헤더가 바뀌었을 때만 돈다."""
    if not scope.bWholeRepo and not scope.touches(("Source",), (".h",)):
        return []
    tablePath = projectRoot / kTablePath
    expected = makeTableText(collectTypeNames(projectRoot))
    actual = tablePath.read_text(encoding="utf-8") if tablePath.is_file() else ""
    if actual == expected:
        return []
    return [f"{kTablePath} 가 소스와 다르다 — `py -3 -m Scripts client-only-types` 로 다시 쓴다"]


def runSelfCheck() -> None:
    listFolder = ["Audio", "Renderer"]
    assert isClientOnlySource("Source/Engine/Audio/Mixer.h", listFolder) and isClientOnlySource("Source/Games/X/Client/Hud.h", listFolder)
    assert not isClientOnlySource("Source/Engine/AudioShared/A.h", listFolder), "폴더 이름의 앞부분만 같으면 아니다"
    header = "REFLECT( Component )\nclass SW_API HudComponent : public Component\n{\n};\nREFLECT()\nstruct Marker\n{\n};\nclass NotReflected\n{\n};\n"
    assert kReflectedType.findall(header) == ["HudComponent", "Marker"], kReflectedType.findall(header)
    assert json.loads(makeTableText(["A", "B"]))["listType"] == ["A", "B"]


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="전용 서버가 짓지 않는 소스의 리플렉션 형식 이름 표를 쓴다")
    parser.add_argument("--check", action="store_true", help="쓰지 않고 낡았는지만 본다")
    parser.add_argument("--self-check", action="store_true", help="고르기 · 형식 이름 뽑기만 본다")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[ClientOnlyTypes] self-check OK")
        return ExitCode.Passed
    projectRoot = getProjectRoot()
    if arguments.check:
        listProblem = checkClientOnlyTypes(projectRoot, makeWholeScope(projectRoot))
        for problem in listProblem:
            print(f"  - {problem}")
        print(f"[ClientOnlyTypes] {'FAIL' if listProblem else 'OK'}")
        return ExitCode.Violation if listProblem else ExitCode.Passed
    text = makeTableText(collectTypeNames(projectRoot))
    tablePath = projectRoot / kTablePath
    if not tablePath.is_file() or tablePath.read_text(encoding="utf-8") != text:
        tablePath.write_text(text, encoding="utf-8", newline="\n")
    print(f"[ClientOnlyTypes] {kTablePath} ({len(json.loads(text)['listType'])} 형식)")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
