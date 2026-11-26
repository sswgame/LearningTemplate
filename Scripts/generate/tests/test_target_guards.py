"""
TargetGuards 생성기 회귀 검사 — 디버그 그리기 헤더는 GameFramework · 키트 · 게임 가드가 막고, 게임의 개발 전용 `Dev/` 소스에만 열린다(`[devallow]`). 낡은 `[devallow]` 줄은 멈춘다.
없는 폴더를 가리키는 `[guard]` 줄, 없는 헤더를 연 `[allow]` 줄, 헤더가 없는 `[guardfolder]` 이름도 configure 를 멈춘다(옮긴 파일의 줄이 남는 꼴).

실행: python -X utf8 Scripts/generate/tests/test_target_guards.py (CTest `Scripts.TargetGuards`). 저장소의 실제 표를 읽고 파일은 쓰지 않는다.
"""

from __future__ import annotations

import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.paths import get_project_root  # noqa: E402
from generate.generator_output import GeneratorError  # noqa: E402
from generate.target_guards import HEADER_TOKEN, RULE_FILE, collect_dev_allow_headers, collect_guards, write_guard_files  # noqa: E402

DEBUG_DRAW_HEADER = "Engine/Graphics/DebugDraw/IDebugDrawQueue.h"


class TargetGuardsDebugDrawTest(unittest.TestCase):
    def test_gameplay_targets_deny_debug_draw(self) -> None:
        table = tomllib.loads(RULE_FILE.read_text(encoding="utf-8"))
        map_guard = collect_guards(get_project_root() / "Source", table["guard"], table["allow"], table["guardfolder"])
        for guard_name in ("gameframework", "kit", "game"):
            self.assertIn(DEBUG_DRAW_HEADER, map_guard[guard_name][0])
        map_allow_dev = collect_dev_allow_headers(table["guard"], table["devallow"], map_guard)
        self.assertEqual(["game"], list(map_allow_dev))
        self.assertIn(DEBUG_DRAW_HEADER, map_allow_dev["game"])

    def test_stale_rule_allow_and_folder_rows_stop(self) -> None:
        table = tomllib.loads(RULE_FILE.read_text(encoding="utf-8"))
        source_dir = get_project_root() / "Source"
        guard_name = next(iter(table["guard"]))
        cases = (
            ("Engine/NoSuchMovedFolder", {**table["guard"], guard_name: [*table["guard"][guard_name], "Engine/NoSuchMovedFolder"]}, table["allow"], table["guardfolder"]),
            ("Engine/NoSuchMovedHeader.h", table["guard"], {**table["allow"], guard_name: [*table["allow"].get(guard_name, []), "Engine/NoSuchMovedHeader.h"]}, table["guardfolder"]),
            ("NoSuchFolderName", table["guard"], table["allow"], {**table["guardfolder"], guard_name: [*table["guardfolder"].get(guard_name, []), "NoSuchFolderName"]}),
        )
        for stale, map_rule, map_allow, map_folder in cases:
            with self.subTest(stale=stale), self.assertRaises(GeneratorError) as context:
                collect_guards(source_dir, map_rule, map_allow, map_folder)
            self.assertIn(stale, str(context.exception))

    def test_stale_dev_open_prefix_stops(self) -> None:
        with self.assertRaises(GeneratorError):
            collect_dev_allow_headers({"game": ["Editor"]}, {"game": ["Engine/Graphics/DebugDraw"]}, {"game": ([], "")})

    def test_guard_content_names_its_header(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            write_guard_files(Path(folder), "game", ["A/B.h"], f"#include \"S/{HEADER_TOKEN}\"\n")
            self.assertEqual('#include "S/A/B.h"\n', (Path(folder) / "game" / "A" / "B.h").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
