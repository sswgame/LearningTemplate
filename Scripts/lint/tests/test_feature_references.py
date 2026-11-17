"""
feature_references 회귀 검사 — 낡은 기대 로그 id(`SW_EXPECT_LOG` · `<ExpectLog>` · `<AllowLog>` 의 id 를 내는 코드가 없음).

실행: python -X utf8 Scripts/lint/tests/test_feature_references.py (CTest `Scripts.FeatureReferences`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.feature_references import Exceptions, Sources, find_problems, run_self_check  # noqa: E402


def make_sources() -> Sources:
    sources = Sources()
    sources.dict_header["Source/A/Log.cpp"] = (
        'void f() { SW_LOG_ERROR_ID( "A.Direct", "x" ); }\n'
        'void g( Report& r ) { r.addErrorWithID( "A.ViaHelper", "x" ); }\n'   # 도우미를 거쳐 매크로에 닿는 id
        '#define SW_LOG_ID_A_MACRO "A.ViaMacro"\n'
        '// "A.InComment" 는 주석이라 정의가 아니다\n'
        "char c = '\"'; const char* p = \"A.AfterQuoteChar\";\n")
    sources.dict_test_code["Source/A/Tests/TestA.cpp"] = (
        'TEST_CASE( A ) { SW_LOG_WARNING_ID( "A.FromTest", "x" ); const char* k = "A.TableOnly";\n'
        ' SW_EXPECT_LOG( "A.Direct" ); SW_EXPECT_LOG( "A.ViaHelper" ); SW_EXPECT_LOG( "A.ViaMacro" ); SW_EXPECT_LOG( "A.FromTest" );\n'
        ' SW_EXPECT_LOG( "A.AfterQuoteChar" ); SW_EXPECT_LOG( "A.TableOnly" ); SW_EXPECT_LOG( kOther ); }\n')
    sources.dict_content["Content/a.scenario.xml"] = '<ExpectLog id="A.Direct" count="1"/>\n<AllowLog id="A.InComment" reason="x"/>\n<AllowLog id="A.Gone" reason="x"/>\n'
    return sources


def log_problems(sources: Sources, dict_log_id: dict[str, str]) -> list[str]:
    return [line for line in find_problems(sources, Exceptions({}, {}, {}, {}, dict_log_id)) if "로그 id" in line]


class StaleLogIDTest(unittest.TestCase):
    def test_reference_without_definition_is_a_problem(self) -> None:
        list_problem = log_problems(make_sources(), {})
        stale = sorted(line.split("로그 id ")[1].split(" ")[0] for line in list_problem)
        # 주석 속 글자 · 테스트의 글자 표는 정의가 아니다. 도우미 · #define · 문자 리터럴 뒤 글자는 정의다.
        self.assertEqual(stale, ["A.Gone", "A.InComment", "A.TableOnly"])
        self.assertTrue(any(line.startswith("Content/a.scenario.xml:") for line in list_problem))

    def test_exception_with_reason_is_accepted(self) -> None:
        list_problem = log_problems(make_sources(), {"A.Gone": "실행 중에 이어 붙인다", "A.InComment": "x", "A.TableOnly": "x"})
        self.assertEqual(list_problem, [])

    def test_stale_exception_rows_are_problems(self) -> None:
        list_problem = log_problems(make_sources(), {"A.Gone": "", "A.Direct": "x", "A.Unused": "x", "A.InComment": "x", "A.TableOnly": "x"})
        self.assertEqual(sum("까닭이 비었다" in line for line in list_problem), 1, list_problem)
        self.assertEqual(sum("이제 정의 리터럴이 있다" in line for line in list_problem), 1, list_problem)
        self.assertEqual(sum("참조가 없다" in line for line in list_problem), 1, list_problem)

    def test_self_check_still_passes(self) -> None:
        run_self_check()


if __name__ == "__main__":
    unittest.main()
