"""
이름 바꾸기 도구의 안전 규칙 회귀 검사 — 임시 git 저장소에서 돌린다.

- C 계열 · 파이썬 소스의 문자열 안은 기본으로 건너뛰고, `--include-strings` 면 함께 바꾼다.
- 새 이름이 코드에 이미 있으면 멈추고, `--force` 면 바꾼다.
- 짧거나 흔한 이름(낱말 하나 · 6 글자 미만)은 거절한다.
- 옛 이름을 `_bannedNames` 에 올린다(까닭과 날짜를 적는다).
- 형식 이름과 같은 줄기의 파일(`Old.h` · `TestOld.cpp`)을 `git mv` 로 옮긴다.
- `--reapply` 는 새 이름이 이미 있어도 남은 옛 이름을 마저 바꾼다(병합 뒤 다시 돌리기).
- `keep-old-name` 표지가 있는 줄은 그대로 둔다.

실행: python -X utf8 Scripts/dev/tests/test_rename_symbol.py (CTest `Scripts.RenameSymbol`). 저장소를 읽지 않는다(임시 git 저장소를 테스트가 만든다).
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.git import run_git_or_raise  # noqa: E402
from common.rename_scope import NAMING_WORDS_PATH  # noqa: E402
from dev import rename_identifier  # noqa: E402
from dev import move_source  # noqa: E402


_REAL_RUN = subprocess.run


def skip_lower_baseline(command, *args, **kwargs):
    """이름 기준선 낮추기(`naming_words.py --lower-baseline`)는 이 저장소의 린트라 임시 저장소에서는 건너뛴다. 나머지(git)는 그대로 돈다."""
    if any("naming_words.py" in str(part) for part in command):
        return subprocess.CompletedProcess(command, 0)
    return _REAL_RUN(command, *args, **kwargs)


def run_rename(root: Path, list_argument: list[str]) -> int:
    """도구를 @p root 저장소에서 돌린다. 쌍 표(`--pairs`)가 있으면 표 도구, 아니면 `Old=New` 도구."""
    with mock.patch.object(move_source, "get_project_root", return_value=root), mock.patch.object(move_source, "format_changed", return_value=0), \
            mock.patch.object(rename_identifier, "get_project_root", return_value=root), mock.patch("subprocess.run", side_effect=skip_lower_baseline):
        if "--pairs" in list_argument:
            index = list_argument.index("--pairs")
            return rename_identifier.main([list_argument[index + 1], *list_argument[:index], *list_argument[index + 2:]])
        return int(move_source.main_rename(list_argument))


class RenameSymbolTest(unittest.TestCase):
    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        self.root = Path(self._folder.name)
        self.write(NAMING_WORDS_PATH, json.dumps({"_bannedNames": {}}, indent=2) + "\n")
        self.write("Source/Engine/OldWidgetPanel.h", 'struct OldWidgetPanel {};\nconst char* kName = "OldWidgetPanel";\n')
        self.write("Source/Engine/Tests/TestOldWidgetPanel.cpp", '#include "Engine/OldWidgetPanel.h"\nOldWidgetPanel g_panel;\n')
        self.write("docs/Note.md", "`OldWidgetPanel` 를 쓴다.\n옛 이름 `OldWidgetPanel` <!-- keep-old-name -->\n")
        run_git_or_raise(["init", "-q"], cwd=self.root)
        run_git_or_raise(["add", "-A"], cwd=self.root)

    def tearDown(self) -> None:
        self._folder.cleanup()

    def write(self, relative: str, text: str) -> None:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")

    def read(self, relative: str) -> str:
        return (self.root / relative).read_text(encoding="utf-8")

    def write_pairs(self, list_pair: list[dict]) -> str:
        path = self.root / "pairs.json"
        path.write_text(json.dumps(list_pair), encoding="utf-8")
        return str(path)

    # ── rename-symbol 쪽 규칙 ──

    def test_strings_are_skipped_by_default(self) -> None:
        self.assertEqual(run_rename(self.root, ["OldWidgetPanel=NewWidgetPanel"]), 0)
        header = (self.root / "Source/Engine/OldWidgetPanel.h").read_text(encoding="utf-8") if (self.root / "Source/Engine/OldWidgetPanel.h").exists() else \
            self.read("Source/Engine/NewWidgetPanel.h")
        self.assertIn("struct NewWidgetPanel", header)
        self.assertIn('"OldWidgetPanel"', header)

    def test_include_strings_changes_literals_too(self) -> None:
        self.assertEqual(run_rename(self.root, ["OldWidgetPanel=NewWidgetPanel", "--include-strings"]), 0)
        header = next(self.root.glob("Source/Engine/*WidgetPanel.h")).read_text(encoding="utf-8")
        self.assertIn('"NewWidgetPanel"', header)

    def test_existing_new_name_stops_unless_forced(self) -> None:
        self.write("Source/Engine/Other.cpp", "int NewWidgetPanel = 0;\n")
        run_git_or_raise(["add", "-A"], cwd=self.root)
        self.assertNotEqual(run_rename(self.root, ["OldWidgetPanel=NewWidgetPanel"]), 0)
        self.assertIn("struct OldWidgetPanel", self.read("Source/Engine/OldWidgetPanel.h"))
        self.assertEqual(run_rename(self.root, ["OldWidgetPanel=NewWidgetPanel", "--force"]), 0)
        self.assertIn("struct NewWidgetPanel", next(self.root.glob("Source/Engine/*WidgetPanel.h")).read_text(encoding="utf-8"))

    def test_marked_lines_keep_the_old_name(self) -> None:
        self.assertEqual(run_rename(self.root, ["OldWidgetPanel=NewWidgetPanel"]), 0)
        self.assertEqual(self.read("docs/Note.md"), "`NewWidgetPanel` 를 쓴다.\n옛 이름 `OldWidgetPanel` <!-- keep-old-name -->\n")

    # ── rename_identifier 쪽 규칙 ──

    def test_short_or_common_names_are_refused(self) -> None:
        self.write("Source/Engine/Short.cpp", "int reads = 0;\n")
        run_git_or_raise(["add", "-A"], cwd=self.root)
        self.assertNotEqual(run_rename(self.root, ["--pairs", self.write_pairs([{"old": "reads", "new": "uses", "why": "t"}])]), 0)
        self.assertIn("int reads", self.read("Source/Engine/Short.cpp"))

    def test_old_name_is_banned_with_reason(self) -> None:
        self.assertEqual(run_rename(self.root, ["--pairs", self.write_pairs([{"old": "OldWidgetPanel", "new": "NewWidgetPanel", "why": "패널 이름"}])]), 0)
        banned = json.loads(self.read(NAMING_WORDS_PATH))["_bannedNames"]
        self.assertIn("OldWidgetPanel", banned)
        self.assertIn("NewWidgetPanel", banned["OldWidgetPanel"])
        self.assertIn("패널 이름", banned["OldWidgetPanel"])

    def test_files_named_after_the_type_move(self) -> None:
        self.assertEqual(run_rename(self.root, ["--pairs", self.write_pairs([{"old": "OldWidgetPanel", "new": "NewWidgetPanel", "why": "t"}])]), 0)
        self.assertTrue((self.root / "Source/Engine/NewWidgetPanel.h").is_file())
        self.assertTrue((self.root / "Source/Engine/Tests/TestNewWidgetPanel.cpp").is_file())
        self.assertFalse((self.root / "Source/Engine/OldWidgetPanel.h").exists())
        self.assertIn('#include "Engine/NewWidgetPanel.h"', self.read("Source/Engine/Tests/TestNewWidgetPanel.cpp"))

    def test_reapply_finishes_after_a_merge(self) -> None:
        self.write("Source/Engine/Merged.cpp", "NewWidgetPanel a; OldWidgetPanel b;\n")
        run_git_or_raise(["add", "-A"], cwd=self.root)
        pairs = self.write_pairs([{"old": "OldWidgetPanel", "new": "NewWidgetPanel", "why": "t"}])
        self.assertNotEqual(run_rename(self.root, ["--pairs", pairs]), 0)
        self.assertEqual(run_rename(self.root, ["--pairs", pairs, "--reapply"]), 0)
        self.assertEqual(self.read("Source/Engine/Merged.cpp"), "NewWidgetPanel a; NewWidgetPanel b;\n")


if __name__ == "__main__":
    unittest.main()
