"""
여러 프로세스 실행기의 판정 회귀 검사 — 파괴 해시 로그(JSON 본문) 맞대기, 바닥 위 자리 견주기(보고 records), 서버 설정 사본의 빈 포트, ctest 항목 모으기.

실행: python -X utf8 Scripts/net/tests/test_net_checks.py (CTest `Scripts.NetChecks`). 프로세스를 띄우지 않는다.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

SCRIPTS_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SCRIPTS_ROOT))

from net import net_pair_run  # noqa: E402
from net.ctest_items import read_items  # noqa: E402
from net.destruction_net_pair import compare_logs  # noqa: E402
from net.online_local_run import write_server_config  # noqa: E402


def _hash_line(side: str, events: int, value: str) -> str:
    return f'[t] [Game] [Info] [tick=3] - [id:DestructionNetSession.HashLine] {{"side":"{side}","id":1,"events":{events},"hash":"{value}","name":"Wall \\"A\\""}}\n'


class NetChecks(unittest.TestCase):
    def test_destruction_hashes_are_compared_by_event_count(self) -> None:
        server = _hash_line("server", 1, "aa") + _hash_line("server", 3, "cc")
        self.assertEqual(compare_logs(server, _hash_line("client", 1, "aa") + _hash_line("client", 2, "bb") + _hash_line("client", 3, "cc")).problems, [])
        self.assertIn("hash mismatch", compare_logs(server, _hash_line("client", 1, "ab") + _hash_line("client", 3, "cc")).problems[0])
        self.assertIn("the server never reached", compare_logs(server, _hash_line("client", 3, "cc") + _hash_line("client", 4, "dd")).problems[0])
        self.assertIn("client stopped", compare_logs(server, _hash_line("client", 1, "aa")).problems[0])
        self.assertIn("nothing was compared", compare_logs(server, "").problems[0])

    def test_floor_spots_are_compared_as_numbers(self) -> None:
        rider = SimpleNamespace(name="first", records=lambda: {"F.X": -1.4305115e-06, "F.Z": 2.0})
        near = SimpleNamespace(name="second", records=lambda: {"R.X": 0.1, "R.Z": 2.1})
        far = SimpleNamespace(name="second", records=lambda: {"R.X": 0.1, "R.Z": 2.5})
        self.assertEqual(net_pair_run._check_same_spot(rider, ("F.X", "F.Z"), near, ("R.X", "R.Z")), [])
        self.assertEqual(len(net_pair_run._check_same_spot(rider, ("F.X", "F.Z"), far, ("R.X", "R.Z"))), 1)

    def test_server_config_copy_takes_free_ports(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            source, out = Path(folder) / "Source", Path(folder) / "Out"
            source.mkdir()
            out.mkdir()
            (source / "Game.json").write_text(json.dumps({"_gamePort": 1, "_servicePort": 2, "_keep": 3}), encoding="utf-8")
            (source / "Game.online.json").write_text("{}", encoding="utf-8")
            copied, service_port = write_server_config(out, source / "Game.json")
            config = json.loads(copied.read_text(encoding="utf-8"))
            self.assertEqual((config["_servicePort"], config["_keep"]), (service_port, 3))
            self.assertNotIn(config["_gamePort"], (1, 2))
            self.assertTrue((out / "Game.online.json").is_file())

    def test_every_item_names_existing_scenarios_once(self) -> None:
        list_item = read_items()
        self.assertEqual(len({item.name for item in list_item}), len(list_item))
        list_scenario = [scenario for item in list_item for scenario in item.scenarios]
        self.assertEqual(len(set(list_scenario)), len(list_scenario))
        for scenario in list_scenario:
            self.assertTrue((SCRIPTS_ROOT.parent / "Content" / scenario).is_file(), scenario)


if __name__ == "__main__":
    unittest.main()
