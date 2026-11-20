"""
move-asset 회귀 검사 — 팩 이름 바꾸기(`game/<옛>` → `game/<새>`)에서 골든 파일 이름 · 시나리오 이름 · 문자열 경로 접두를 함께 바꾸고,
도메인 뿌리를 통째로 옮기면 파일마다가 아니라 새 도메인 표에 접두 리다이렉트 한 줄을 쓴다.

실행: python -X utf8 Scripts/dev/tests/test_move_asset.py (CTest `Scripts.MoveAsset`). 저장소를 읽지 않는다(글 · 임시 git 저장소를 테스트가 만든다).
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.git import run_git_or_raise  # noqa: E402
from dev.move_asset import (PackRename, get_pack_rename, is_domain_root, is_pack_string_file, list_golden_renames, rewrite_pack_strings,  # noqa: E402
                            rewrite_table_targets, write_redirects)
from lint.asset_identity import FOUND, read_header  # noqa: E402
from lint.asset_references import add_redirects  # noqa: E402

RENAME = PackRename("oldpack", "newpack")


class MoveAssetPackRenameTest(unittest.TestCase):
    def test_only_pack_roots_are_pack_renames(self) -> None:
        self.assertEqual(get_pack_rename("game/oldpack", "game/newpack"), RENAME)
        self.assertIsNone(get_pack_rename("game/oldpack/maps", "game/oldpack/levels"))
        self.assertIsNone(get_pack_rename("common/data", "common/tables"))
        self.assertTrue(is_domain_root("game/oldpack"))
        self.assertTrue(is_domain_root("common"))
        self.assertFalse(is_domain_root("game/oldpack/maps"))

    def test_golden_files_get_the_new_scenario_prefix(self) -> None:
        list_index = ["Content/game/oldpack/automation/golden/game.oldpack.boat.storm.dx11.ppm",
                      "Content/game/oldpack/automation/golden/game.oldpack.boat.storm.dx11.golden.json",
                      "Content/game/oldpack/automation/golden/engine.editor.view.dx11.ppm",   # 다른 시나리오 이름 — 그대로
                      "Content/game/oldpack/automation/game.oldpack.notgolden.ppm"]           # 골든 폴더 밖 — 그대로
        self.assertEqual(list_golden_renames(list_index, RENAME), [
            ("Content/game/oldpack/automation/golden/game.oldpack.boat.storm.dx11.ppm", "Content/game/oldpack/automation/golden/game.newpack.boat.storm.dx11.ppm"),
            ("Content/game/oldpack/automation/golden/game.oldpack.boat.storm.dx11.golden.json",
             "Content/game/oldpack/automation/golden/game.newpack.boat.storm.dx11.golden.json")])

    def test_scenario_names_and_path_prefixes_change_but_plain_words_do_not(self) -> None:
        text = ('<Scenario name="game.oldpack.boat" timeout="5">\n'
                '\t<Run scenario="game.oldpack.lake" />\n'
                '\t<!-- Content/game/oldpack/maps/a.scene.xml -->\n'
                '</Scenario>\n')
        rewritten, count = rewrite_pack_strings(text, RENAME)
        self.assertEqual(rewritten, ('<Scenario name="game.newpack.boat" timeout="5">\n'
                                     '\t<Run scenario="game.newpack.lake" />\n'
                                     '\t<!-- Content/game/newpack/maps/a.scene.xml -->\n'
                                     '</Scenario>\n'))
        self.assertEqual((count.scenario_name, count.other_name, count.path), (1, 1, 1))

        code = 'load("game/oldpack/data/a.xml"); load("mygame/oldpack/b.xml"); id = "Oldpack"; x = "game/oldpackish/c.xml"; y = "subgame.oldpack.z"'
        rewritten, count = rewrite_pack_strings(code, RENAME)
        self.assertEqual(rewritten, code.replace('"game/oldpack/data', '"game/newpack/data', 1))
        self.assertEqual((count.scenario_name, count.other_name, count.path), (0, 0, 1))

    def test_chain_names_domains_and_pack_file_names_change_but_longer_names_do_not(self) -> None:
        text = ('"test": "Scenario.oldpack.desktop.editor.playreload"; ctest -R Scenario.oldpack; Scenario.oldpackish; Scenario.oldpack_x\n'
                '"_packRoot": "game/oldpack"; `game/oldpack`; game/oldpack.mesh; "game_oldpack.pack"; mygame_oldpack; game_oldpackish\n')
        rewritten, count = rewrite_pack_strings(text, RENAME)
        self.assertEqual(rewritten, ('"test": "Scenario.newpack.desktop.editor.playreload"; ctest -R Scenario.newpack; Scenario.oldpackish; Scenario.oldpack_x\n'
                                     '"_packRoot": "game/newpack"; `game/newpack`; game/oldpack.mesh; "game_newpack.pack"; mygame_oldpack; game_oldpackish\n'))
        self.assertEqual((count.chain, count.domain), (2, 3))

    def test_folded_paths_content_folders_and_scene_chunks_change(self) -> None:
        text = ('"Game/Oldpack/Maps/A.scene.xml" "Game\\\\Oldpack/" "GAME/OLDPACK/x" "D:/c/Content/game/oldpack" "scene_oldpack_alpha"\n'
                '"Game/Oldpackish/x" "MyGame/Oldpack/x" "subscene_oldpack_alpha"\n')
        rewritten, count = rewrite_pack_strings(text, RENAME)
        self.assertEqual(rewritten, ('"Game/Newpack/Maps/A.scene.xml" "Game\\\\Newpack/" "GAME/NEWPACK/x" "D:/c/Content/game/newpack" "scene_newpack_alpha"\n'
                                     '"Game/Oldpackish/x" "MyGame/Oldpack/x" "subscene_oldpack_alpha"\n'))   # 대소문자 꼴을 옮기고, 더 긴 이름 · 앞 낱말이 붙은 것은 두지 않는다
        self.assertEqual((count.path, count.domain), (3, 2))

    def test_string_files_are_code_and_data_but_not_redirect_tables_or_docs(self) -> None:
        self.assertTrue(is_pack_string_file("Source/Engine/Content/Pack/Tests/TestPack.cpp"))
        self.assertTrue(is_pack_string_file("Scripts/net/net_pair_run.py"))
        self.assertTrue(is_pack_string_file("Test/Scenario/CMakeLists.txt"))
        self.assertTrue(is_pack_string_file("Config/Game/Playground.json"))
        self.assertTrue(is_pack_string_file("Content/game/oldpack/data/localization/po/ko.po"))
        self.assertTrue(is_pack_string_file(".gitignore"))
        self.assertFalse(is_pack_string_file("Content/game/oldpack/redirects.xml"))
        self.assertFalse(is_pack_string_file("docs/NEXT.md"))

    def test_moved_table_keeps_old_ids_and_points_to_the_new_pack(self) -> None:
        table = '<Redirects>\n\t<Redirect from="game/oldpack/a.xml" to="game/oldpack/b.xml" />\n</Redirects>\n'
        self.assertEqual(rewrite_table_targets(table, [("game/oldpack", "game/newpack", True)]),
                         '<Redirects>\n\t<Redirect from="game/oldpack/a.xml" to="game/newpack/b.xml" />\n</Redirects>\n')


class MoveAssetRedirectTest(unittest.TestCase):
    def test_prefix_line_survives_later_file_moves(self) -> None:
        prefixed = add_redirects(None, [], [("game/oldpack", "game/newpack")])
        self.assertEqual(prefixed, '<Redirects>\n\t<PrefixRedirect from="game/oldpack" to="game/newpack" />\n</Redirects>\n')
        moved = add_redirects(prefixed, [("game/newpack/a.xml", "game/newpack/b.xml")])
        self.assertIn('<PrefixRedirect from="game/oldpack" to="game/newpack" />', moved)
        self.assertIn('<Redirect from="game/newpack/a.xml" to="game/newpack/b.xml" />', moved)
        with self.assertRaises(ValueError):
            add_redirects(prefixed, [], [("game/oldpack", "game/oldpack")])

    def test_root_move_writes_one_prefix_line_in_the_new_domain_table(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            run_git_or_raise(["init", "-q"], cwd=root)
            written = write_redirects(root, [], ("game/oldpack", "game/newpack"))
            self.assertEqual(written, ["Content/game/newpack/redirects.xml"])
            text = (root / "Content/game/newpack/redirects.xml").read_text(encoding="utf-8")
            state, guid = read_header(text)
            self.assertEqual(state, FOUND)   # 새 표도 정체성 머리를 단다(asset-ids 검사)
            self.assertEqual(text, f'<?asset guid="{guid}"?>\n<Redirects>\n\t<PrefixRedirect from="game/oldpack" to="game/newpack" />\n</Redirects>\n')
            write_redirects(root, [("game/newpack/a.xml", "game/newpack/b.xml")])
            self.assertEqual(read_header((root / "Content/game/newpack/redirects.xml").read_text(encoding="utf-8")), (FOUND, guid))   # 있던 표는 머리를 지킨다
            self.assertFalse((root / "Content/game/oldpack").exists())   # 옛 팩 폴더를 표 하나로 되살리지 않는다


if __name__ == "__main__":
    unittest.main()
