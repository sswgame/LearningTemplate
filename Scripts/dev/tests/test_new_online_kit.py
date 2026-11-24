"""
new-online-kit 회귀 검사 — 뼈대의 파일 자리 · 매니페스트 의존과 대상 · 공유 쪽이 서버 헤더를 보지 않는지 · 쓴 이름이 저장소에 있는지,
중앙 표(메서드 영역 · 서버 키트 이름)의 줄, 만들 수 없는 요청의 거절.

실행: python -X utf8 Scripts/dev/tests/test_new_online_kit.py (CTest `Scripts.NewOnlineKit`). 저장소에 쓰지 않는다.
"""

from __future__ import annotations

import json
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.paths import get_project_root  # noqa: E402
from dev.new_online_kit import (KIT_NAME_HEADER, KIT_ROOT, RANGE_HEADER, add_kit_name, add_method_range,  # noqa: E402
                                describe_invalid_request, make_files)
from dev.tests.kit_skeleton_checks import find_problems  # noqa: E402

NAME = "ZzGuildHall"
INCLUDE_PATH = f"GameFramework/Kits/Feature/Online/{NAME}"


def read_repository_text(relative: str) -> str:
    return (get_project_root() / relative).read_bytes().decode("utf-8")


class NewOnlineKitTest(unittest.TestCase):
    def setUp(self) -> None:
        self._files = make_files(NAME)

    def test_the_file_list(self) -> None:
        self.assertEqual(sorted(self._files), sorted([
            f"Runtime/GF_{NAME}.module.json", "Runtime/CMakeLists.txt", f"Runtime/{NAME}Protocol.h", f"Runtime/{NAME}Protocol.cpp", f"Runtime/{NAME}Client.h",
            f"Server/GF_Server_{NAME}.module.json", "Server/CMakeLists.txt", f"Server/{NAME}Service.h", f"Server/{NAME}Service.cpp",
            f"Server/{NAME}OnlineServerKit.cpp", f"Server/Tests/Test{NAME}Service.cpp"]))

    def test_manifests_split_the_shared_module_from_the_server_module(self) -> None:
        shared = json.loads(self._files[f"Runtime/GF_{NAME}.module.json"])
        server = json.loads(self._files[f"Server/GF_Server_{NAME}.module.json"])
        self.assertEqual((shared["_name"], shared["_kind"], shared["_listTarget"]), (f"GF_{NAME}", "Kit", ["Client", "Server"]))
        self.assertEqual((server["_name"], server["_kind"], server["_listTarget"]), (f"GF_Server_{NAME}", "Kit", ["Server"]))
        self.assertEqual([row["_name"] for row in server["_listDependency"]], ["GameFramework", "GF_Server_OnlineCore", f"GF_{NAME}"])
        self.assertIn(f"sw_add_game_framework_kit(GF_{NAME})", self._files["Runtime/CMakeLists.txt"])
        self.assertIn(f"sw_add_game_framework_kit(GF_Server_{NAME})", self._files["Server/CMakeLists.txt"])

    def test_manifest_columns_match_the_existing_online_kit(self) -> None:
        for relative, existing in ((f"Runtime/GF_{NAME}.module.json", "LiveOps/Runtime/GF_LiveOps.module.json"),
                                   (f"Server/GF_Server_{NAME}.module.json", "LiveOps/Server/GF_Server_LiveOps.module.json")):
            self.assertEqual(list(json.loads(self._files[relative])), list(json.loads(read_repository_text(f"{KIT_ROOT}/{existing}"))), relative)

    def test_includes_stay_inside_the_manifest_dependencies(self) -> None:
        for relative, text in self._files.items():
            for include in re.findall(r'#include "([^"]+)"', text):
                if relative.startswith("Runtime/"):
                    self.assertNotIn("/Server/", include, f"{relative}: 공유 모듈은 클라이언트에도 든다")
                if include.startswith("GameFramework/Kits/"):
                    self.assertTrue(include.startswith((INCLUDE_PATH + "/", "GameFramework/Kits/Feature/Online/OnlineCore/")), f"{relative}: {include}")

    def test_code_uses_only_names_the_repository_has(self) -> None:
        self.assertEqual(find_problems(self._files, INCLUDE_PATH, NAME, {"Echo", "ResultName", "Table", "EchoIsInsideTheKitRange"}), [])

    def test_no_placeholder_is_left(self) -> None:
        for relative, text in self._files.items():
            self.assertNotIn("{name}", text, relative)
            self.assertEqual(text.count("{"), text.count("}"), relative)

    def test_the_central_tables_get_one_free_row_each(self) -> None:
        ranges = read_repository_text(RANGE_HEADER)
        added = add_method_range(ranges, NAME)
        row = re.search(rf"static constexpr uint16 k{NAME} = (0x[0-9A-F]{{4}});\s*static constexpr uint16 kGame\b", added)
        self.assertIsNotNone(row)
        assert row is not None
        self.assertEqual(int(row.group(1), 16) & 0xFF, 0)
        self.assertNotIn(row.group(1), ranges)
        self.assertEqual(add_method_range(added, NAME), added)

        names = read_repository_text(KIT_NAME_HEADER)
        added = add_kit_name(names, NAME)
        self.assertIn(f'static constexpr const utf8* k{NAME} = "zzGuildHall";', added)
        self.assertEqual(add_kit_name(added, NAME), added)
        self.assertIn('kAccountBook = "accountBook"', add_kit_name(names, "AccountBook"))   # 이름이 앞부분만 같은 키트

        for add_row in (add_method_range, add_kit_name):
            with self.assertRaises(RuntimeError):
                add_row("struct Other {};", NAME)

    def test_invalid_requests_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.assertIsNone(describe_invalid_request(root, NAME))
            self.assertIn("PascalCase", describe_invalid_request(root, "guildHall") or "")
            other = root / "Source/GameFramework/Kits/Genre/Other"
            other.mkdir(parents=True)
            (other / f"GF_{NAME}.module.json").write_text("{}", encoding="utf-8")
            self.assertIn("already used", describe_invalid_request(root, NAME) or "")
            (root / KIT_ROOT / NAME).mkdir(parents=True)
            self.assertIn("already exists", describe_invalid_request(root, NAME) or "")


if __name__ == "__main__":
    unittest.main()
