"""
함수 · 람다 본문 범위 도우미(`function_spans.py`) 회귀 검사.

실행: python -X utf8 Scripts/lint/tests/test_function_spans.py (CTest `Scripts.FunctionSpans`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.function_spans import find_function_spans  # noqa: E402


class FunctionSpansTest(unittest.TestCase):
    def test_function_and_lambda_names(self) -> None:
        code = (
            "bool A::loadFromAsset( const AssetID& id )\n{\n    if ( x )\n    {\n        call();\n    }\n"
            "    each( []( int v ) { use( v ); } );\n    each( [&] { other(); } );\n}\n\n"
            "static EditorMarkHit evaluate( const State& s, const float2& a = float2{ 0.5f, 0.5f } )\n{\n    run( [this]() mutable {\n        inner( [] { deep(); } );\n    } );\n}\n"
            "int32 arr[2]{ 1, 2 };\n"
        )
        list_span = find_function_spans(code)
        names = [span.name for span in list_span]
        assert names == ["A::loadFromAsset", "A::loadFromAsset#lambda1", "A::loadFromAsset#lambda2", "evaluate", "evaluate#lambda1", "evaluate#lambda1#lambda1"], names   # 제어문 블록 · 배열 초기화는 함수 · 람다가 아니다


if __name__ == "__main__":
    unittest.main()
