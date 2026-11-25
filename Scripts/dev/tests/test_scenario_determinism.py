"""
scenario_determinism 회귀 검사 — 돌릴 차례, 바이트 비교, 진 실행 · 사진 없는 시나리오를 통과로 치지 않는 것.

실행: python -X utf8 Scripts/dev/tests/test_scenario_determinism.py (CTest `Scripts.ScenarioDeterminism`). 앱을 띄우지 않고 임시 폴더만 쓴다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.scenario_determinism import check_determinism, make_sequence_command, make_stage_runs  # noqa: E402


class FakeApp:
    """단계마다 산출물 폴더 하나를 만드는 가짜 앱 — `contents` 가 그 단계에서 찍는 사진의 내용을 정한다."""

    def __init__(self, root: Path, contents: list[bytes | None]) -> None:
        self._root = root
        self._contents = list(contents)
        self._count = 0

    def run(self, list_stage: Sequence[str]) -> str:
        for stage in list_stage:
            if stage != "target":
                continue
            self._count += 1
            folder = self._root / "name" / str(self._count)
            folder.mkdir(parents=True)
            content = self._contents.pop(0)
            if content is not None:
                (folder / "view.ppm").write_bytes(content)
        return ""


class ScenarioDeterminismTest(unittest.TestCase):
    def check(self, contents: list[bytes | None]) -> list[str]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            return check_determinism(["before", "target"], 1, "name", root, FakeApp(root, contents).run)

    def test_stage_runs_put_the_chain_neighbor_first(self) -> None:
        self.assertEqual([stages for _, stages in make_stage_runs(["a", "b", "c"], 1)], [["b"], ["a", "b"], ["b", "b"]])
        self.assertEqual([stages for _, stages in make_stage_runs(["a", "b"], 0)], [["a"], ["b", "a"], ["a", "a"]])
        self.assertEqual([stages for _, stages in make_stage_runs(["a"], 0)], [["a"], ["a", "a"]])

    def test_same_bytes_pass(self) -> None:
        self.assertEqual(self.check([b"x", b"x", b"x", b"x"]), [])

    def test_one_different_photo_is_named(self) -> None:
        list_problem = self.check([b"x", b"x", b"x", b"y"])
        self.assertEqual(len(list_problem), 1)
        self.assertIn("view.ppm", list_problem[0])
        self.assertIn("두 번 이어 2", list_problem[0])

    def test_scenario_without_images_is_not_a_pass(self) -> None:
        self.assertEqual(len(self.check([None, None, None, None])), 1)

    def test_command_runs_one_backend(self) -> None:
        chain = ["App.exe", "-scenario=first.scenario.xml,second.scenario.xml", "-scenario-backends=all", "-scenario-backend-split=independent", "-scenario-keep-going"]
        command = make_sequence_command(chain, ["second.scenario.xml", "second.scenario.xml"], "dx12", Path("r.json"), Path("p.log"))
        self.assertIn("-scenario=second.scenario.xml,second.scenario.xml", command)
        self.assertIn("-scenario-backends=dx12", command)
        self.assertFalse(any("split" in part for part in command))

    def test_failed_run_is_not_compared(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            self.assertIn("GoldenMismatch", check_determinism(["target"], 0, "name", Path(temporary), lambda list_stage: "GoldenMismatch")[0])


if __name__ == "__main__":
    unittest.main()
