"""
syntax_check 의 회귀 검사 — 묶음 TU 풀기, 헤더를 거친 TU 고르기, 새 파일의 명령 빌리기, 명령 가르기.

실행: python -X utf8 Scripts/dev/tests/test_syntax_check.py. 임시 폴더만 쓴다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.syntax_check import SyntaxUnit, borrow_unit, make_units, select_units, split_command, strip_arguments  # noqa: E402
from common.compile_database import rewrite_command  # noqa: E402


def write(path: Path, text: str = "") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


class MakeUnitsTest(unittest.TestCase):
    def test_unity_entries_open_into_their_files_in_this_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            main_root, tree_root = Path(temporary).resolve() / "main", Path(temporary).resolve() / "tree"
            for root in (main_root, tree_root):
                for name in ("A.cpp", "B.cpp"):
                    write(root / "Source/Core" / name)
            write(main_root / "Source/Core/Gone.cpp")   # 이 트리가 지운 파일
            unity = write(main_root / "build/X/Unity/unity_0_cxx.cxx",
                          "".join(f'#include "{(main_root / "Source/Core" / name).as_posix()}"\n' for name in ("A.cpp", "B.cpp", "Gone.cpp")))
            database = [{"directory": temporary, "file": unity.as_posix(), "command": f"cl -DUNITY -I{main_root}\\Source -I{main_root}\\build\\gates\\core /Foa.obj -c -- {unity}"},
                        {"directory": temporary, "file": (main_root / "Source/Core/A.cpp").as_posix(), "command": "cl -DALONE -c -- A.cpp"}]
            units = make_units(database, main_root, tree_root)
            self.assertEqual(sorted(units), ["source/core/a.cpp", "source/core/b.cpp"])
            self.assertEqual(units["source/core/a.cpp"].arguments, "cl -DALONE")   # 따로 짓는 항목이 앞선다
            self.assertEqual(units["source/core/b.cpp"].arguments, f"cl -DUNITY -I{tree_root}\\Source")   # 이 트리의 경로, 문지기 · 산출물 없음
            self.assertEqual(units["source/core/b.cpp"].file, tree_root / "Source/Core/B.cpp")

    def test_module_stubs_stay_in_the_main_tree(self) -> None:
        self.assertIn("gates", strip_arguments(r"cl -ID:\x\build\gates\m /Fo1.obj -c -- a.cpp", keep_module_stubs=True))
        self.assertNotIn("gates", strip_arguments(r"cl -ID:\x\build\gates\m /Fo1.obj -c -- a.cpp", keep_module_stubs=False))


class SelectUnitsTest(unittest.TestCase):
    def test_headers_reach_every_including_unit_and_new_files_borrow_a_command(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            main_root, tree_root = Path(temporary).resolve() / "main", Path(temporary).resolve() / "tree"
            source = tree_root / "Source"
            write(source / "Core/Low.h")
            write(source / "Core/Middle.h", '#include "Core/Low.h"\n')
            write(source / "Core/User.cpp", '#include "Core/Middle.h"\n')
            write(source / "Core/Other.cpp")
            write(source / "Core/Tests/TestNew.cpp")
            write(source / "Core/Web.cpp")
            write(main_root / "Source/Core/Web.cpp")   # 본 트리에도 있는데 DB 에 없다 — 이 구성이 짓지 않는다
            write(source / "Core/Tests/TestOld.cpp")
            units = {f"source/core/{name.lower()}": SyntaxUnit(source / "Core" / name, "d", f"cl -D{Path(name).stem}") for name in ("User.cpp", "Other.cpp", "Tests/TestOld.cpp")}
            selected, unbuilt = select_units([source / "Core/Low.h", source / "Core/Tests/TestNew.cpp", source / "Core/Web.cpp"], units, main_root, tree_root)
            self.assertEqual(sorted(selected), ["source/core/tests/testnew.cpp", "source/core/user.cpp"])
            self.assertEqual(selected["source/core/tests/testnew.cpp"].arguments, "cl -DTestOld")   # 같은 갈래(테스트)의 명령
            self.assertEqual(unbuilt, ["Source/Core/Web.cpp"])
            self.assertIsNone(borrow_unit("Source/Nowhere/X.cpp", units, tree_root))


class CommandTest(unittest.TestCase):
    def test_quoted_defines_stay_one_argument(self) -> None:
        self.assertEqual(split_command('cl.exe  -DNAME=\\"Release\\" "-IC:\\a b" /FID:/x.h -DV=1'), ["cl.exe", '-DNAME="Release"', "-IC:\\a b", "/FID:/x.h", "-DV=1"])

    def test_pch_headers_come_from_this_tree(self) -> None:
        """강제 포함한 cmake_pch.hxx 는 본 트리 헤더를 절대 경로로 적는다 — 그대로 두면 이 트리의 같은 헤더와 두 번 정의된다."""
        with tempfile.TemporaryDirectory() as temporary:
            main_root, tree_root = Path(temporary).resolve() / "main", Path(temporary).resolve() / "tree"
            header = (main_root / "Source/Engine/pch.h").as_posix()
            pch = write(main_root / "build/X/cmake_pch.hxx", f'#pragma clang system_header\n#ifdef __cplusplus\n#include "{header}"\n#endif\n')
            command = rewrite_command(f"cl /Yucmake_pch.hxx /FI{pch.as_posix()} -c a.cpp", main_root, tree_root)
            self.assertEqual(command, f"cl /FI{(tree_root / 'Source/Engine/pch.h').as_posix()} -c a.cpp")


if __name__ == "__main__":
    unittest.main()
