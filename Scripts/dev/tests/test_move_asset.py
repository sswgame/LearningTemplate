"""
move-asset 회귀 검사 — 팩 이름 바꾸기(`game/<옛>` → `game/<새>`)에서 골든 파일 이름 · 시나리오 이름 · 문자열 경로 접두를 함께 바꾼다.
팩 흡수(`game/<옛>` → `game/<새>/<하위>`)는 경로 접두만 바꾼다.

실행: python -X utf8 Scripts/dev/tests/test_move_asset.py (CTest `Scripts.MoveAsset`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.move_asset import (PackAbsorb, PackRename, ScenarioRename, get_pack_absorb, get_pack_rename, get_scenario_rename, is_domain_root, is_pack_string_file,  # noqa: E402
                            list_golden_renames, list_leftover_patterns, list_scenario_golden_renames, rewrite_absorbed_strings, rewrite_pack_strings,
                            rewrite_scenario_name)

RENAME = PackRename("oldpack", "newpack")


class MoveAssetScenarioRenameTest(unittest.TestCase):
    RENAME = ScenarioRename("game.oldpack.mount", "game.newpack.old_mount", "Content/game/newpack/old/automation/golden", "Content/game/newpack/automation/golden")

    def test_the_name_follows_the_file_and_the_pack(self) -> None:
        text = '<Scenario name="game.oldpack.mount" timeoutFrames="9">'
        self.assertEqual(get_scenario_rename("game/newpack/old/automation/mount.scenario.xml", "game/newpack/automation/old_mount.scenario.xml", text), self.RENAME)
        editor = get_scenario_rename("engine/automation/editor/a.scenario.xml", "engine/automation/editor/b.scenario.xml", '<Scenario name="editor.a">')
        self.assertEqual(editor.new_name if editor is not None else "", "editor.b")
        self.assertIsNone(get_scenario_rename("game/x/automation/a.scenario.xml", "game/x/automation/b.scenario.xml", '<Scenario name="game.x.other">'))   # 이름이 파일을 따르지 않는 시나리오
        self.assertIsNone(get_scenario_rename("game/x/data/a.xml", "game/x/data/b.xml", text))

    def test_only_its_goldens_move(self) -> None:
        folder = "Content/game/newpack/old/automation/golden/"
        list_index = [folder + "game.oldpack.mount.gallop.dx11.ppm", folder + "game.oldpack.mountain.view.dx11.ppm"]
        self.assertEqual(list_scenario_golden_renames(list_index, self.RENAME),
                         [(list_index[0], "Content/game/newpack/automation/golden/game.newpack.old_mount.gallop.dx11.ppm")])

    def test_only_the_exact_name_changes(self) -> None:
        self.assertEqual(rewrite_scenario_name('"game.oldpack.mount" game.oldpack.mount.gallop game.oldpack.mountain x.game.oldpack.mount', self.RENAME),
                         ('"game.newpack.old_mount" game.newpack.old_mount.gallop game.oldpack.mountain x.game.oldpack.mount', 2))


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


class MoveAssetPackAbsorbTest(unittest.TestCase):
    def test_only_a_pack_root_into_another_pack_folder_is_absorbed(self) -> None:
        self.assertEqual(get_pack_absorb("game/oldpack", "game/hub/oldpack"), PackAbsorb("oldpack", "game/hub/oldpack"))
        self.assertIsNone(get_pack_absorb("game/oldpack", "game/newpack"))            # 이름 바꾸기
        self.assertIsNone(get_pack_absorb("game/oldpack/maps", "game/hub/maps"))      # 팩 뿌리가 아니다
        self.assertIsNone(get_pack_absorb("game/oldpack", "game/oldpack/inner/x"))   # 같은 팩 안
        self.assertTrue(is_pack_string_file("Tools/launch-args/Source/Extension.js"))

    def test_paths_and_domains_move_under_the_new_folder_but_names_stay_for_the_leftover_list(self) -> None:
        absorb = PackAbsorb("oldpack", "game/hub/oldpack")
        text = ('load("game/oldpack/maps/a.scene.xml"); "_packRoot": "game/oldpack"; `game/oldpack`\n'
                'mygame/oldpack/x; game/oldpackish/y; game/oldpack.mesh; <Scenario name="game.oldpack.talk">; ctest -R Scenario.oldpack\n')
        rewritten, count = rewrite_absorbed_strings(text, absorb)
        self.assertEqual(rewritten, ('load("game/hub/oldpack/maps/a.scene.xml"); "_packRoot": "game/hub/oldpack"; `game/hub/oldpack`\n'
                                     'mygame/oldpack/x; game/oldpackish/y; game/oldpack.mesh; <Scenario name="game.oldpack.talk">; ctest -R Scenario.oldpack\n'))
        self.assertEqual(count, 3)
        self.assertEqual(list_leftover_patterns("game/oldpack", absorb), ["game/oldpack", "game.oldpack.", "scenario.oldpack"])
        self.assertEqual(list_leftover_patterns("game/oldpack", None), ["game/oldpack"])


if __name__ == "__main__":
    unittest.main()
