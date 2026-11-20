"""
scenario_bisect 회귀 검사 — 진 단계 다시 돌기(--rerun-failed)가 고르는 백엔드 보고 폴더.

실행: python -X utf8 Scripts/dev/tests/test_scenario_bisect.py (CTest `Scripts.ScenarioBisect`). 임시 폴더만 쓴다.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.scenario_bisect import find_latest_per_backend_folder  # noqa: E402


def write_reports(folder: Path, names: list[str], mtime: float) -> None:
    folder.mkdir(parents=True)
    for index, name in enumerate(names):
        (folder / f'dx12.{index}.json').write_text(json.dumps({'name': name, 'result': 'Passed'}), encoding='utf-8')
    os.utime(folder, (mtime, mtime))


class FindLatestPerBackendFolderTest(unittest.TestCase):
    def test_a_later_run_of_another_chain_sharing_a_stage_is_not_chosen(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            now = time.time()
            write_reports(root / 'kitmodulereload' / 'per-backend-1', ['editor.bugreport', 'kitmodulereload'], now - 100)
            write_reports(root / 'kitmodulereload' / 'per-backend-2', ['game.empty.boat', 'kitmodulereload'], now)
            chosen = find_latest_per_backend_folder(root, 'editor/kitmodulereload.scenario.xml', {'editor.bugreport', 'kitmodulereload'}, root / 'no-chain.log')
            self.assertEqual(chosen, root / 'kitmodulereload' / 'per-backend-1')

    def test_no_matching_folder_is_none(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_reports(root / 'x' / 'per-backend-1', ['game.empty.boat'], time.time())
            self.assertIsNone(find_latest_per_backend_folder(root, 'x/x.scenario.xml', {'editor.bugreport'}, root / 'no-chain.log'))


if __name__ == '__main__':
    unittest.main()
