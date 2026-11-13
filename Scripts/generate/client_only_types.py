#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/client_only_types.py

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
from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.index_cache import open_facts  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope  # noqa: E402
from generate.target_layers import read_server_excluded_folders  # noqa: E402 — 짓지 않는 엔진 폴더의 정본은 BuildLayout.cmake 한 곳이다

TABLE_PATH = "Config/Engine/ClientOnlyTypes.generated.json"
REFLECTED_TYPE = re.compile(r"^\s*REFLECT\s*\([^\n]*\)\s*\n\s*(?:class|struct)\s+(?:SW_\w+\s+)?(\w+)", re.M)


def is_client_only_source(relative_path: str, list_excluded_folder: list[str]) -> bool:
    """전용 서버가 짓지 않는 소스인가(`SW_EXCLUDED_ENGINE_FOLDERS` 의 엔진 폴더 · `SW_CLIENT_ONLY_SOURCE_PATTERNS` 의 `/Client/`)."""
    return any(relative_path.startswith(f"Source/Engine/{folder}/") for folder in list_excluded_folder) or "/Client/" in relative_path


def collect_type_names(project_root: Path) -> list[str]:
    """클라이언트 전용 헤더의 리플렉션 형식 이름 — 내용이 바뀐 헤더만 다시 읽는다(`lint/index_cache.py`, 못 읽는 글은 include-order 가 막는다)."""
    list_excluded_folder = read_server_excluded_folders(project_root)
    list_path = [path for path in list_repo_files(project_root)
                if path.startswith("Source/") and path.endswith(".h") and "/Tests/" not in path and is_client_only_source(path, list_excluded_folder)]
    facts = open_facts(project_root, "client-only-types", [__file__], list_path, lambda _, text: REFLECTED_TYPE.findall(text), contribute=lambda _, list_name: list_name)
    return sorted(facts.summary)   # 요약(이름 → 그 이름을 정의한 파일 수)만 읽는다


def make_table_text(list_name: list[str]) -> str:
    table = {
        "_comment": "생성 파일 — Scripts/generate/client_only_types.py 가 쓴다(고치지 않는다). 전용 서버가 짓지 않는 소스의 리플렉션 형식 이름이다. 서버는 씬에서 이 형식을 만나면 만들지 않고 건너뛴다.",
        "listType": list_name,
    }
    return json.dumps(table, ensure_ascii=False, indent=4) + "\n"


def check_client_only_types(project_root: Path, scope: LintScope) -> list[str]:
    """표가 소스와 같으면 빈 목록입니다. 표는 저장소 전체로 짓고(색인), 커밋 훅은 헤더가 바뀌었을 때만 돈다."""
    if not scope.whole_repo and not scope.touches(("Source",), (".h",)):
        return []
    table_path = project_root / TABLE_PATH
    expected = make_table_text(collect_type_names(project_root))
    actual = table_path.read_text(encoding="utf-8") if table_path.is_file() else ""
    if actual == expected:
        return []
    return [f"{TABLE_PATH} 가 소스와 다르다 — `py -3 -m Scripts client-only-types` 로 다시 쓴다"]


def run_self_check() -> None:
    list_folder = ["Audio", "Renderer"]
    assert is_client_only_source("Source/Engine/Audio/Mixer.h", list_folder) and is_client_only_source("Source/Games/X/Client/Hud.h", list_folder)
    assert not is_client_only_source("Source/Engine/AudioShared/A.h", list_folder), "폴더 이름의 앞부분만 같으면 아니다"
    header = "REFLECT( Component )\nclass SW_API HudComponent : public Component\n{\n};\nREFLECT()\nstruct Marker\n{\n};\nclass NotReflected\n{\n};\n"
    assert REFLECTED_TYPE.findall(header) == ["HudComponent", "Marker"], REFLECTED_TYPE.findall(header)
    assert json.loads(make_table_text(["A", "B"]))["listType"] == ["A", "B"]


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="전용 서버가 짓지 않는 소스의 리플렉션 형식 이름 표를 쓴다")
    parser.add_argument("--check", action="store_true", help="쓰지 않고 낡았는지만 본다")
    parser.add_argument("--self-check", action="store_true", help="고르기 · 형식 이름 뽑기만 본다")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[ClientOnlyTypes] self-check OK")
        return ExitCode.PASSED
    project_root = get_project_root()
    if arguments.check:
        list_problem = check_client_only_types(project_root, make_whole_scope(project_root))
        for problem in list_problem:
            print(f"  - {problem}")
        print(f"[ClientOnlyTypes] {'FAIL' if list_problem else 'OK'}")
        return ExitCode.VIOLATION if list_problem else ExitCode.PASSED
    text = make_table_text(collect_type_names(project_root))
    table_path = project_root / TABLE_PATH
    if not table_path.is_file() or table_path.read_text(encoding="utf-8") != text:
        table_path.write_text(text, encoding="utf-8", newline="\n")
    print(f"[ClientOnlyTypes] {TABLE_PATH} ({len(json.loads(text)['listType'])} 형식)")
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
