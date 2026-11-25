"""
명령 발견(`common/command.py`) 회귀 검사 — 파일 하나를 더하면 명령이 생기고 지우면 사라지며, 머리 리터럴이 빠지거나 이름이 겹치면 멈춘다.

실행: python -X utf8 Scripts/common/tests/test_command.py (CTest `Scripts.Command`). 임시 폴더에 명령 파일을 쓴다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common import command  # noqa: E402

SOURCE = 'class {0}Command(Command):\n    name = "{1}"\n    help = "{2}"\n\n    def run(self, context, args):\n        return 0\n'


class FindCommandsTest(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.tools = Path(folder.name) / "tools"
        self.tools.mkdir()
        patch = mock.patch.object(command, "SCRIPTS_DIR", Path(folder.name))
        patch.start()
        self.addCleanup(patch.stop)

    def test_a_file_is_a_command(self) -> None:
        path = self.tools / "copy_file.py"
        path.write_text(SOURCE.format("Copy", "copy", "사본"), encoding="utf-8")
        self.assertEqual(command.find_commands(), {"copy": command.CommandEntry("tools.copy_file", "CopyCommand", "사본")})
        path.unlink()
        self.assertEqual(command.find_commands(), {})

    def test_bad_heads_stop(self) -> None:
        (self.tools / "a.py").write_text(SOURCE.format("A", "copy", "x"), encoding="utf-8")
        (self.tools / "b.py").write_text(SOURCE.format("B", "copy", "x"), encoding="utf-8")
        with self.assertRaises(SystemExit, msg="겹친 이름"):
            command.find_commands()
        (self.tools / "b.py").write_text('class BCommand(Command):\n    name = "other"\n\n', encoding="utf-8")
        with self.assertRaises(SystemExit, msg="help 없음"):
            command.find_commands()

    def test_command_error_is_one_line_and_a_violation(self) -> None:
        class FailCommand(command.Command):
            name = "fail"
            help = "진다"

            def run(self, context: command.ScriptContext, args: object) -> int:
                raise command.CommandError("까닭")

        self.assertEqual(FailCommand.main([]), command.ExitCode.VIOLATION)


if __name__ == "__main__":
    unittest.main()
