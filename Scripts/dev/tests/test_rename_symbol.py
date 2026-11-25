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
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common import command  # noqa: E402
from common.git import run_git_or_raise  # noqa: E402
from dev.rename_scope import (  # noqa: E402
    BANNED_NAMES_PATH, RENAME_PAIRS_FOLDER, collect_reflected_properties, dump_naming_table, make_serialized_name, plan_serialized_rename, replace_serialized_name,
    rewrite_rename_target, rewrite_unmarked_lines,
)
from dev import rename_symbol  # noqa: E402


def run_rename(root: Path, list_argument: list[str]) -> int:
    """도구를 @p root 저장소에서 돌린다. 이름 기준선 낮추기 · clang-format 은 이 저장소의 린트라 건너뛴다."""
    with mock.patch.object(command, "get_project_root", return_value=root), mock.patch.object(rename_symbol, "format_changed", return_value=0):
        return int(rename_symbol.RenameSymbolCommand.main(list_argument))


class RenameRuleTest(unittest.TestCase):
    def test_pair_and_text_rules(self) -> None:
        # 예시 이름은 이 도구의 치환이 다시 바꾸지 않게 표지를 단다.
        assert rename_symbol.check_pair("isCursorCaptured", "shouldShowCursor") is None  # keep-old-name
        assert rename_symbol.check_pair("reads", "uses") is not None, "낱말 하나는 거절"
        assert rename_symbol.check_pair("reads", "uses", allow_common_name=True) is None
        assert rename_symbol.check_pair("When", "Condition") is not None, "흔한 이름은 거절"
        assert rename_symbol.check_pair("_bNot", "_bNegate") is None, "멤버 머리 이름은 낱말 하나여도 받는다"
        assert rename_symbol.check_pair("swText", "text", rename_symbol.CMAKE_SCOPE) is None, "CMake 낙타 지역 변수는 짧아도 받는다"
        assert rename_symbol.check_pair("target", "goal", rename_symbol.CMAKE_SCOPE) is not None, "CMake 소문자 한 낱말은 명령 · 키워드와 겹친다"
        text, count = rename_symbol.replace_word("isCursorCaptured(); bool isCursorCapturedLater; x.isCursorCaptured", "isCursorCaptured", "shouldShowCursor")  # keep-old-name
        assert count == 2 and "isCursorCapturedLater" in text, (text, count)
        source = 'Ogg a; // Ogg "x\nf( "Ogg", R"(Ogg)", \'"\' ); /* Ogg */ Ogg b;\n#include "A/Ogg.h"\nOgg c; // keep-old-name\n'
        rewritten, skipped = rename_symbol.rename_in_text(Path("a.cpp"), "a.cpp", source, "Ogg", "OGG", False)
        assert rewritten == 'OGG a; // OGG "x\nf( "Ogg", R"(Ogg)", \'"\' ); /* OGG */ OGG b;\n#include "A/OGG.h"\nOgg c; // keep-old-name\n', rewritten
        assert skipped == 2, skipped
        python = "def runOgg():  # Ogg\n    return f\"Ogg {Ogg}\" + 'Ogg' + Ogg + \"\"\"\nOgg\n\"\"\"\n"
        rewritten, skipped = rename_symbol.rename_in_text(Path("a.py"), "a.py", python, "Ogg", "OGG", False)
        assert rewritten == "def runOgg():  # Ogg\n    return f\"Ogg {OGG}\" + 'Ogg' + OGG + \"\"\"\nOgg\n\"\"\"\n", rewritten   # 이름 토큰만 — 여러 줄 문자열 · 주석은 지킨다
        assert skipped == 4, skipped
        assert rename_symbol.is_cmake_file(Path("cmake/A.cmake")) and rename_symbol.is_cmake_file(Path("Source/CMakeLists.txt")) and not rename_symbol.is_cmake_file(Path("Source/A.cpp"))

    def test_marked_lines_stay(self) -> None:
        """표지 줄은 지키고 나머지 줄은 바꾸는지 본다."""
        rename = lambda part: part.replace("OldName", "NewName")  # noqa: E731
        text = "use `OldName`\nnot `OldName` <!-- keep-old-name -->\n`OldName` again\n"
        assert rewrite_unmarked_lines(text, rename) == "use `NewName`\nnot `OldName` <!-- keep-old-name -->\n`NewName` again\n"
        assert rewrite_unmarked_lines("`OldName`", rename) == "`NewName`"
        table = dump_naming_table({"_comment": "OldName", "_bannedNames": {"OldName": "OldName — x"}})
        assert json.loads(rewrite_rename_target(BANNED_NAMES_PATH, table, rename)) == {"_comment": "OldName", "_bannedNames": {"OldName": "NewName — x"}}
        assert rewrite_rename_target("Config/Rules/Lint/NamingWords.json", '{"_keep": {"OldName": "y"}}', rename) == '{"_keep": {"NewName": "y"}}'
        assert rewrite_rename_target(RENAME_PAIRS_FOLDER + "Example.json", "OldName", rename) == "OldName"
        assert rewrite_rename_target("Config/Rules/Lint/ThreadOwners.json", "OldName", rename) == "NewName"

    def test_serialized_names_follow(self) -> None:
        """실제 사례(e15f47d876): 사진 단계의 `_b` 멤버로 옮긴 개명이 시나리오 13 개의 옛 속성 이름을 남겼다(그 멤버는 뒤에 `stability` 글 칸이 되었다)."""
        assert make_serialized_name("_bRequiresForeground") == "requiresForeground" and make_serialized_name("_fileName") == "fileName"
        assert make_serialized_name("_bias") == "bias" and make_serialized_name("_b") == "b" and make_serialized_name("clips") == "clips"
        header = (
            "#define PROPERTY( ... )\n"
            "struct Shot\n{\n"
            "    // PROPERTY() 설명 속 _notAField;\n"
            "    PROPERTY( Required )\n    string _fileName;\n"
            "    PROPERTY( Min = min( 0, 1 ) )\n    bool32 _bHoldFrame : 1;\n"
            "    PROPERTY( Name = \"clips\" )\n    vector<string> _listClipName{};\n"
            "    PROPERTY()\n    AssetRef<Texture>* _pMainTexture{ nullptr };\n"
            "    float32 _plainField;\n};\n"
            "struct Other\n{\n    PROPERTY()\n    float32 _holdFrame;\n};\n"
        )
        properties = collect_reflected_properties({"Shot.h": header})
        assert properties == {"_fileName": [("Shot.h", "fileName")], "_bHoldFrame": [("Shot.h", "holdFrame")], "clips": [("Shot.h", "clips")],
                          "_pMainTexture": [("Shot.h", "pMainTexture")], "_holdFrame": [("Shot.h", "holdFrame")]}, properties
        data = {"a.scenario.xml": '<Shot fileName="a.ppm" holdFrame="true" otherFileName="x"><fileName>b</fileName></Shot>',
                "Steps.cpp": 'find( "fileName" ); parse( "Shot fileName=a" ); string fileName = "a"; if ( fileName == b )',
                "notes.md": "the fileName of a shot"}
        pair, problems = plan_serialized_rename(properties, data, "_fileName", "_outputPath")
        assert pair == ("fileName", "outputPath") and not problems, (pair, problems)
        assert replace_serialized_name(data["a.scenario.xml"], *pair) == ('<Shot outputPath="a.ppm" holdFrame="true" otherFileName="x"><outputPath>b</outputPath></Shot>', 3)
        assert replace_serialized_name(data["Steps.cpp"], *pair) == ('find( "outputPath" ); parse( "Shot outputPath=a" ); string fileName = "a"; if ( fileName == b )', 2)
        assert replace_serialized_name(data["notes.md"], *pair) == (data["notes.md"], 0), "글 속 낱말은 남긴다"
        # XML 은 `=` 둘레를 띄워도 속성이다 — 코드의 `string fileName = "a"` 는 변수라 바꾸지 않는다
        assert replace_serialized_name('<Shot fileName = "a.ppm"/>', *pair, path="a.scenario.xml") == ('<Shot outputPath = "a.ppm"/>', 1)
        assert replace_serialized_name('string fileName = "a";', *pair, path="Steps.cpp") == ('string fileName = "a";', 0)
        assert plan_serialized_rename(properties, {"b.xml": '<Shot fileName ="x"/>'}, "_fileName", "_outputPath") == (("fileName", "outputPath"), [])
        # 병합 뒤 다시 돌리기: 선언은 이미 새 이름이다
        renamed = collect_reflected_properties({"Shot.h": header.replace("_fileName", "_outputPath")})
        assert plan_serialized_rename(renamed, data, "_fileName", "_outputPath") == (("fileName", "outputPath"), [])
        # 겹침: `_bHoldFrame` 과 `_holdFrame` 이 같은 속성 이름이다 — 조용히 바꾸지 않고 멈춘다
        pair, problems = plan_serialized_rename(properties, data, "_bHoldFrame", "_bFreezeFrame")
        assert pair is None and len(problems) == 1 and "_holdFrame(Shot.h)" in problems[0] and "a.scenario.xml" in problems[0], problems
        # 데이터에 쓰인 곳이 없거나, 이름을 직접 적었거나(`Name =`), 리플렉션 칸이 아니면 할 일이 없다
        assert plan_serialized_rename(properties, {"notes.md": "holdFrame"}, "_bHoldFrame", "_bFreezeFrame") == (None, [])
        assert plan_serialized_rename(properties, {"a.xml": '<A clips="x" listClipName="y" plainField="1"/>'}, "_listClipName", "_listClipTitle") == (None, [])
        assert plan_serialized_rename(properties, {"a.xml": '<A plainField="1"/>'}, "_plainField", "_simpleField") == (None, [])
        # 흔한 낱말(한 낱말 · 짧은 이름)은 손으로 읽는 속성과 가릴 수 없다
        common = collect_reflected_properties({"T.h": "PROPERTY()\nfloat32 _tolerance;"})
        pair, problems = plan_serialized_rename(common, {"a.xml": '<Expect tolerance="1"/>'}, "_tolerance", "_errorMargin")
        assert pair is None and len(problems) == 1, problems


class RenameSymbolTest(unittest.TestCase):
    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        self.root = Path(self._folder.name)
        self.write(BANNED_NAMES_PATH, json.dumps({"_bannedNames": {}}, indent=2) + "\n")
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

    # ── 한 쌍 도구에서 온 규칙 ──

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

    # ── 쌍 표 도구에서 온 규칙 ──

    def test_short_or_common_names_are_refused(self) -> None:
        self.write("Source/Engine/Short.cpp", "int reads = 0;\n")
        run_git_or_raise(["add", "-A"], cwd=self.root)
        self.assertNotEqual(run_rename(self.root, ["--pairs", self.write_pairs([{"old": "reads", "new": "uses", "why": "t"}])]), 0)
        self.assertIn("int reads", self.read("Source/Engine/Short.cpp"))

    def test_old_name_is_banned_with_reason(self) -> None:
        self.assertEqual(run_rename(self.root, ["--pairs", self.write_pairs([{"old": "OldWidgetPanel", "new": "NewWidgetPanel", "why": "패널 이름"}])]), 0)
        banned = json.loads(self.read(BANNED_NAMES_PATH))["_bannedNames"]
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
