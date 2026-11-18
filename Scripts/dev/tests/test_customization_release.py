"""
customization-release 회귀 검사 — 꾸미기 스키마를 출시 스냅숏과 견준다: 추가만은 통과, 단계 없는 제거는 실패하고 --fix 가 Remove 단계와 판 올림을 적는다(음성 대조: 단계가 있으면 통과),
이름 바꾸기 · 종류 바꾸기는 명시 단계 없이 실패, 판이 출시 판보다 낮으면 실패.

실행: python -X utf8 Scripts/dev/tests/test_customization_release.py (CTest `Scripts.CustomizationRelease`). 저장소를 읽지 않는다(임시 폴더).
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # Scripts

from dev.customization_release import SNAPSHOT_PATH, compare, parse_catalog, run, write_snapshot  # noqa: E402

RELEASED = """<?xml version="1.0" encoding="utf-8"?>
<CustomizationSchemaCatalog>
  <Schema id="Cloak">
    <Color name="Tint" default="1 1 1 1"/>
    <Slider name="Shine" min="0" max="1"/>
    <Slider name="Clasp" min="0" max="3"/>
  </Schema>
</CustomizationSchemaCatalog>
"""


def catalog(body: str, head: str = '<Schema id="Cloak">') -> str:
    return f'<?xml version="1.0" encoding="utf-8"?>\n<CustomizationSchemaCatalog>\n  {head}\n{body}  </Schema>\n</CustomizationSchemaCatalog>\n'


TINT = '    <Color name="Tint" default="1 1 1 1"/>\n'
SHINE = '    <Slider name="Shine" min="0" max="1"/>\n'
GEM = '    <Slider name="Clasp" min="0" max="3"/>\n'


class CompareTest(unittest.TestCase):
    def issues(self, text: str):
        return compare(parse_catalog(RELEASED), parse_catalog(text))

    def test_added_parameter_passes(self):
        self.assertEqual([], self.issues(catalog(TINT + SHINE + GEM + '    <Color name="Trim" default="1 1 1 1"/>\n')))

    def test_removal_without_step_fails_and_is_fixable(self):
        issues = self.issues(catalog(TINT + SHINE))
        self.assertEqual(1, len(issues))
        self.assertEqual("Clasp", issues[0].removable)

    def test_removal_with_step_passes(self):
        text = catalog('    <Migration toVersion="2"><Remove parameter="Clasp"/></Migration>\n' + TINT + SHINE, '<Schema id="Cloak" version="2">')
        self.assertEqual([], self.issues(text))

    def test_rename_without_step_fails_not_fixable(self):
        issues = self.issues(catalog('    <Color name="Primary" default="1 1 1 1"/>\n' + SHINE + GEM))
        self.assertEqual(1, len(issues))
        self.assertFalse(issues[0].fixable)
        self.assertIn('<Rename from="Tint" to="Primary"/>', issues[0].message)

    def test_rename_with_step_passes(self):
        text = catalog('    <Migration toVersion="2"><Rename from="Tint" to="Primary"/></Migration>\n    <Color name="Primary" default="1 1 1 1"/>\n' + SHINE + GEM,
                       '<Schema id="Cloak" version="2">')
        self.assertEqual([], self.issues(text))

    def test_kind_change_needs_step(self):
        changed = TINT + SHINE + '    <Color name="Clasp" default="1 1 1 1"/>\n'
        issues = self.issues(catalog(changed))
        self.assertEqual(1, len(issues))
        self.assertFalse(issues[0].fixable)
        stepped = catalog('    <Migration toVersion="2"><PaletteIndexToColor parameter="Clasp" palette="Metals"/></Migration>\n' + changed, '<Schema id="Cloak" version="2">')
        self.assertEqual([], self.issues(stepped))

    def test_lower_version_fails(self):
        released = parse_catalog(catalog(TINT, '<Schema id="Cloak" version="3">'))
        self.assertEqual(1, len(compare(released, parse_catalog(catalog(TINT)))))


class FixTest(unittest.TestCase):
    def run_in(self, text: str, fix: bool) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            write_snapshot(root / SNAPSHOT_PATH, parse_catalog(RELEASED))
            path = root / "Content" / "common" / "data" / "appearance" / "customization.xml"
            path.parent.mkdir(parents=True)
            path.write_text(text, encoding="utf-8")
            code = run(root, "check", fix)
            return code, path.read_text(encoding="utf-8")

    def test_check_fails_on_removal_without_step(self):
        code, text = self.run_in(catalog(TINT + SHINE), fix=False)
        self.assertEqual(1, code)
        self.assertNotIn("<Remove", text)

    def test_fix_writes_remove_and_raises_version(self):
        code, text = self.run_in(catalog(TINT + SHINE), fix=True)
        self.assertEqual(0, code)
        self.assertIn('<Schema id="Cloak" version="2">', text)
        self.assertIn('<Migration toVersion="2"><Remove parameter="Clasp"/></Migration>', text)
        self.assertEqual([], compare(parse_catalog(RELEASED), parse_catalog(text)))

    def test_fix_adds_to_pending_version(self):
        pending = catalog('    <Migration toVersion="2"><Rename from="Tint" to="Primary"/></Migration>\n    <Color name="Primary" default="1 1 1 1"/>\n' + SHINE,
                          '<Schema id="Cloak" version="2">')
        code, text = self.run_in(pending, fix=True)
        self.assertEqual(0, code)
        self.assertIn('<Rename from="Tint" to="Primary"/><Remove parameter="Clasp"/></Migration>', text)
        self.assertEqual(1, text.count("<Migration"))

    def test_fix_does_not_write_a_rename(self):
        code, text = self.run_in(catalog('    <Color name="Primary" default="1 1 1 1"/>\n' + SHINE + GEM), fix=True)
        self.assertEqual(1, code)
        self.assertNotIn("<Remove", text)


if __name__ == "__main__":
    unittest.main()
