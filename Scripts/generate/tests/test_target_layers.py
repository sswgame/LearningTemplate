"""
TargetLayers 생성기 회귀 검사 — 디버그 그리기 헤더는 GameFramework · 키트 · 게임 게이트가 막고, 게임의 개발 전용 `Dev/` 소스에만 열린다(`[devopen]`). 낡은 `[devopen]` 줄은 멈춘다.
없는 폴더를 가리키는 `[gate]` 줄, 없는 헤더를 연 `[allow]` 줄, 헤더가 없는 `[gatefolder]` 이름도 configure 를 멈춘다(옮긴 파일의 줄이 남는 꼴).

실행: python -X utf8 Scripts/generate/tests/test_target_layers.py (CTest `Scripts.TargetLayers`). 저장소의 실제 표를 읽고 파일은 쓰지 않는다.
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
from generate.target_layers import HEADER_TOKEN, RULE_FILE, collect_dev_open_headers, collect_guards, write_guard_stubs  # noqa: E402

DEBUG_DRAW_HEADER = "Engine/Graphics/DebugDraw/IDebugDrawQueue.h"


class TargetLayersDebugDrawTest(unittest.TestCase):
    def test_gameplay_targets_deny_debug_draw(self) -> None:
        table = tomllib.loads(RULE_FILE.read_text(encoding="utf-8"))
        map_gate = collect_guards(get_project_root() / "Source", table["gate"], table["allow"], table["gatefolder"])
        for gate_name in ("gameframework", "kit", "game"):
            self.assertIn(DEBUG_DRAW_HEADER, map_gate[gate_name][0])
        map_open = collect_dev_open_headers(table["gate"], table["devopen"], map_gate)
        self.assertEqual(["game"], list(map_open))
        self.assertIn(DEBUG_DRAW_HEADER, map_open["game"])

    def test_stale_rule_allow_and_folder_rows_stop(self) -> None:
        table = tomllib.loads(RULE_FILE.read_text(encoding="utf-8"))
        source_dir = get_project_root() / "Source"
        gate_name = next(iter(table["gate"]))
        cases = (
            ("Engine/NoSuchMovedFolder", {**table["gate"], gate_name: [*table["gate"][gate_name], "Engine/NoSuchMovedFolder"]}, table["allow"], table["gatefolder"]),
            ("Engine/NoSuchMovedHeader.h", table["gate"], {**table["allow"], gate_name: [*table["allow"].get(gate_name, []), "Engine/NoSuchMovedHeader.h"]}, table["gatefolder"]),
            ("NoSuchFolderName", table["gate"], table["allow"], {**table["gatefolder"], gate_name: [*table["gatefolder"].get(gate_name, []), "NoSuchFolderName"]}),
        )
        for stale, map_rule, map_allow, map_folder in cases:
            with self.subTest(stale=stale), self.assertRaises(GeneratorError) as context:
                collect_guards(source_dir, map_rule, map_allow, map_folder)
            self.assertIn(stale, str(context.exception))

    def test_stale_dev_open_prefix_stops(self) -> None:
        with self.assertRaises(GeneratorError):
            collect_dev_open_headers({"game": ["Editor"]}, {"game": ["Engine/Graphics/DebugDraw"]}, {"game": ([], "")})

    def test_stub_content_names_its_header(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            write_guard_stubs(Path(folder), "game", ["A/B.h"], f"#include \"S/{HEADER_TOKEN}\"\n")
            self.assertEqual('#include "S/A/B.h"\n', (Path(folder) / "game" / "A" / "B.h").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
