#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""RunFunctionSize — 여러 줄 시그니처, 람다, 매크로, 문자열 · 주석 속 중괄호, 초기화 목록, 클래스 · 네임스페이스 본문을 제대로 재는지."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "lint" / "report"))

import RunFunctionSize  # noqa: E402


def measureByName(text: str) -> dict[str, RunFunctionSize.FunctionSize]:
    measure = RunFunctionSize.measureText(text, "a.cpp")
    assert measure.bBalanced, "braces should balance in the sample"
    return {function.name: function for function in measure.listFunction}


class FunctionSizeTest(unittest.TestCase):
    def testMultiLineSignatureInNamespaceAndClass(self) -> None:
        text = (
            "namespace sw\n"
            "{\n"
            "    class Foo : public Bar<int>\n"
            "    {\n"
            "    public:\n"
            "        int compute( int a,\n"
            "                     int b ) const noexcept\n"
            "        {\n"
            "            if ( a )\n"
            "            {\n"
            "                return a;\n"
            "            }\n"
            "            return b;\n"
            "        }\n"
            "    };\n"
            "\n"
            "    sw::string Foo::name(\n"
            "        const utf8* p ) const\n"
            "    {\n"
            "        return p;\n"
            "    }\n"
            "} // namespace sw\n"
        )
        mapFunction = measureByName(text)
        self.assertEqual(set(mapFunction), {"compute", "Foo::name"})
        self.assertEqual(mapFunction["compute"].bodyLines, 6)
        self.assertEqual(mapFunction["compute"].maxDepth, 1)
        self.assertEqual(mapFunction["compute"].returnCount, 2)
        self.assertEqual(mapFunction["Foo::name"].line, 19)

    def testLambdaLinesAndReturnsAreSeparated(self) -> None:
        text = (
            "void run()\n"
            "{\n"
            "    auto onLine = [&]( const string& line ) -> bool\n"
            "    {\n"
            "        return line.empty();\n"
            "    };\n"
            "    forEach( [this]() mutable { return 1; } );\n"
            "    return;\n"
            "}\n"
        )
        mapFunction = measureByName(text)
        self.assertEqual(list(mapFunction), ["run"])
        self.assertEqual(mapFunction["run"].bodyLines, 7)
        self.assertEqual(mapFunction["run"].lambdaLines, 2)
        self.assertEqual(mapFunction["run"].returnCount, 1)

    def testBracesInStringsCommentsAndMacrosAreIgnored(self) -> None:
        text = (
            "#define SW_OPEN_SCOPE {\n"
            "#define SW_BLOCK( x ) \\\n"
            "    do { x; } while ( 0 )\n"
            "void f()\n"
            "{\n"
            "    const char* p = \"{{{\"; // }\n"
            "    char c = '}';\n"
            "    /* { */\n"
            "}\n"
            "SW_TEST_CASE( Suite, Name )\n"
            "{\n"
            "    SW_EXPECT_TRUE( true );\n"
            "}\n"
        )
        mapFunction = measureByName(text)
        self.assertEqual(set(mapFunction), {"f", "SW_TEST_CASE(Suite, Name)"})
        self.assertEqual(mapFunction["f"].bodyLines, 4)
        self.assertEqual(mapFunction["f"].maxDepth, 0)

    def testConstructorInitializerListAndInitializers(self) -> None:
        text = (
            "constexpr int kTable[] = { 1, 2, 3 };\n"
            "enum class Kind : uint8 { A, B };\n"
            "struct Data { int _x{ 0 }; };\n"
            "Foo::Foo( int a )\n"
            "    : _a( a )\n"
            "    , _b{ a }\n"
            "{\n"
            "    init();\n"
            "}\n"
            "bool Foo::operator==( const Foo& other ) const\n"
            "{\n"
            "    return _a == other._a;\n"
            "}\n"
        )
        measure = RunFunctionSize.measureText(text, "a.cpp")
        names = [function.name for function in measure.listFunction]
        self.assertEqual(names, ["Foo::Foo", "Foo::operator=="])
        self.assertEqual(measure.listFunction[0].line, 7)

    def testUnbalancedFileIsReported(self) -> None:
        text = "void f()\n{\n#if A\n    if ( x ) {\n#else\n    if ( y ) {\n#endif\n    }\n}\n"
        # 전처리 줄을 지워도 `#if` 두 갈래의 여는 중괄호가 남는다 — 짝이 어긋난 것으로 알린다.
        self.assertFalse(RunFunctionSize.measureText(text, "a.cpp").bBalanced)


if __name__ == "__main__":
    unittest.main()
