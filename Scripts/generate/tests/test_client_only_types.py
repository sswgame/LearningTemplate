"""
ClientOnlyTypes 생성 검사 — 리플렉션 형식만 이름을 뽑고, 같은 글이면 표를 다시 쓰지 않는다(서버 빌드의 뒤 단계가 다시 돌지 않는다).

실행: python -X utf8 Scripts/generate/tests/test_client_only_types.py (CTest `Scripts.ClientOnlyTypes`). 헤더와 표는 임시 폴더의 것이다.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.command import CommandError  # noqa: E402
from generate.client_only_types import REFLECTED_TYPE, write_table  # noqa: E402

HEADER = "REFLECT( Component )\nclass SW_API HudComponent : public Component\n{\n};\nREFLECT()\nstruct Marker\n{\n};\nclass NotReflected\n{\n};\n"


class ClientOnlyTypesTest(unittest.TestCase):
    def test_reflected_types_only(self) -> None:
        self.assertEqual(REFLECTED_TYPE.findall(HEADER), ["HudComponent", "Marker"])

    def test_multi_line_reflect_arguments(self) -> None:
        header = 'REFLECT( Category = "Kit", DisplayName = "Avatar (client)",\n         Tooltip = "two lines" )\nclass SW_KIT_CLIENT_API AvatarComponent : public Component\n{\n};\n'
        self.assertEqual(REFLECTED_TYPE.findall(header), ["AvatarComponent"])

    def test_table_is_sorted_and_kept_when_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "B.h").write_text(HEADER, encoding="utf-8")
            (root / "A.h").write_text("REFLECT()\nstruct Alpha\n{\n};\nREFLECT()\nstruct Marker\n{\n};\n", encoding="utf-8")
            output = root / "Bin" / "ClientOnlyTypes.json"
            self.assertEqual(write_table([root / "B.h", root / "A.h"], output), 3)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["listType"], ["Alpha", "HudComponent", "Marker"])
            os.utime(output, (0, 0))
            write_table([root / "A.h", root / "B.h"], output)
            self.assertEqual(output.stat().st_mtime, 0, "같은 글이면 표를 다시 쓰지 않는다")

    def test_unread_reflect_stops(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / "Odd.h"
            header.write_text("REFLECT() class Odd\n{\n};\n" + HEADER, encoding="utf-8")
            with self.assertRaisesRegex(CommandError, "Odd.h:1 "):
                write_table([header], Path(folder) / "ClientOnlyTypes.json")

    def test_missing_header_stops(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(OSError):
                write_table([Path(folder) / "Missing.h"], Path(folder) / "ClientOnlyTypes.json")


if __name__ == "__main__":
    unittest.main()
