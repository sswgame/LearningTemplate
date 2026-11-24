"""
flat_map_in_processes 의 회귀 검사 — 부모가 강제로 끝나면 풀의 자식도 끝난다.

실행: python -X utf8 Scripts/lint/tests/test_parallel_parent_death.py (CTest `Scripts.ParallelParentDeath`).
"""
from __future__ import annotations

import subprocess
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.parallel import flat_map_in_processes  # noqa: E402

SLEEP_SECONDS = 600


def sleep_item(item: int) -> list[int]:
    print("started", flush=True)
    time.sleep(SLEEP_SECONDS)
    return [item]


class ParallelParentDeathTest(unittest.TestCase):
    def test_children_exit_with_killed_parent(self) -> None:
        process = subprocess.Popen([sys.executable, "-X", "utf8", __file__, "--run"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(process.stdout.readline().strip(), b"started")
        process.kill()
        # 자식이 하나라도 남으면 물려받은 표준 출력을 쥐고 있어 끝(EOF)이 오지 않는다.
        process.communicate(timeout=60)


if __name__ == "__main__":
    if "--run" in sys.argv:
        flat_map_in_processes(sleep_item, range(1000), worker_count=2)
    else:
        unittest.main()
