#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/client_only_types.py

전용 서버가 짓지 않는 소스(`SW_EXCLUDED_ENGINE_FOLDERS` 의 엔진 폴더 — 오디오 · 렌더러 · UI, `…/Client/`)의 리플렉션 형식 이름을 표로 모은다.

- 왜 표인가: 서버는 그 소스를 짓지 않아 형식을 모른다. 씬 · 프리팹에 HUD · 마커 · 월드 위젯 · 소리(에미터 · 리스너 · 잔향 존) 컴포넌트가 있으면 서버는 모르는 형식을 만나는데,
  이름 바꾸기 · 빠진 모듈(오류)과 클라이언트 전용(정상)을 가르려면 이 이름들을 서버도 알아야 한다. 서버의 직렬화기는 표의 형식이면 만들지 않고 조용히
  건너뛴다(`ClientOnlyTypeTable`. 언리얼은 클래스를 서버도 지어 두고 액터를 띄우지 않는다. 여기는 클래스를 짓지 않으니 이름만 싣는다).
- 누가 쓰는가: 전용 서버 실행 파일을 짓는 빌드(`Source/Server/CMakeLists.txt` — 서버 구성 · 개발 구성)가 헤더 목록을 넘겨 실행 파일 옆(`Bin/ClientOnlyTypes.json`)에 쓴다. 헤더가 바뀌면 다시 쓰고,
  패키지(`Scripts/package/pack.py`)가 서버 실행 파일과 함께 싣는다. 저장소에는 두지 않는다(낡을 사본이 없다).

  python -X utf8 Scripts/generate/client_only_types.py --headers <헤더 목록 파일> --output <Bin>/ClientOnlyTypes.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.command import Command, ScriptContext  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402

#: 실행 파일 옆 표 이름 — `ClientOnlyTypeTable::kTableFile` · `pack.py` 의 `CLIENT_ONLY_TYPE_TABLE` 과 같은 이름이다.
TABLE_FILE_NAME = "ClientOnlyTypes.json"
#: 인자가 여러 줄에 걸친 `REFLECT( …,\n … )` 도 잡는다(괄호 한 겹 안의 괄호까지) — 한 줄만 보던 꼴은 두 줄 REFLECT 아바타를 표에서 빠뜨려 서버 쿠킹이 졌다.
REFLECTED_TYPE = re.compile(r"^\s*REFLECT\s*\((?:[^()]|\([^()]*\))*\)\s*\n\s*(?:class|struct)\s+(?:SW_\w+\s+)?(\w+)", re.M)


def collect_type_names(list_header: Sequence[Path]) -> list[str]:
    """헤더들의 리플렉션 형식 이름(글자 순, 겹침 없음)입니다. 읽지 못하는 헤더는 예외로 멈춘다(빈 표로 대신하지 않는다)."""
    return sorted({name for header in list_header for name in REFLECTED_TYPE.findall(header.read_text(encoding="utf-8"))})


def make_table_text(list_name: list[str]) -> str:
    table = {
        "_comment": "빌드 산출물 — Scripts/generate/client_only_types.py 가 서버 구성 빌드에서 쓴다. 전용 서버가 짓지 않는 소스의 리플렉션 형식 이름이다. 서버는 씬에서 이 형식을 만나면 만들지 않고 건너뛴다.",
        "listType": list_name,
    }
    return json.dumps(table, ensure_ascii=False, indent=4) + "\n"


def write_table(list_header: Sequence[Path], output: Path) -> int:
    """표를 쓴다 — 글이 같으면 건드리지 않는다(시각이 바뀌지 않아 뒤 단계가 다시 돌지 않는다). 돌려주는 것은 형식 수."""
    list_name = collect_type_names(list_header)
    text = make_table_text(list_name)
    if not output.is_file() or output.read_text(encoding="utf-8") != text:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8", newline="\n")
    return len(list_name)


class ClientOnlyTypesCommand(Command):
    name = "client-only-types"
    help = "전용 서버가 짓지 않는 소스의 리플렉션 형식 이름 표를 쓴다(서버 구성 빌드가 부른다)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--headers", type=Path, required=True, help="헤더 절대 경로를 한 줄에 하나씩 적은 파일")
        parser.add_argument("--output", type=Path, required=True, help=f"쓸 표(실행 파일 옆 {TABLE_FILE_NAME})")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        list_header = [Path(line) for line in args.headers.read_text(encoding="utf-8").splitlines() if line.strip()]
        count = write_table(list_header, args.output)
        print(f"[ClientOnlyTypes] {args.output} ({count} 형식)")
        return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(ClientOnlyTypesCommand.main())
