"""
Scripts/common/CppConstants.py 의 단위 테스트 — 다른 헤더의 상수 · 산식 · 순환 include · 이름 충돌 · 못 푸는 경우의 오류.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.ConfigReference import resolveRangeConstantInternal  # noqa: E402
from common.CppConstants import ConstantError, ConstantResolver, formatDoubleInternal, hasConstantName  # noqa: E402


class CppConstantsTest(unittest.TestCase):
    def makeResolverInternal(self, mapFile: dict[str, str]) -> ConstantResolver:
        tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(tempDir.cleanup)
        root = Path(tempDir.name)
        for relative, text in mapFile.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        return ConstantResolver(root, lambda relative: (root / relative).read_text(encoding="utf-8"))

    def testResolvesConstantFromIncludedHeader(self) -> None:
        resolver = self.makeResolverInternal({
            "Source/A/Limits.h": "namespace sw { struct NetLimit { static constexpr int32 kMaxSize = 1024; }; }",
            "Source/A/Settings.h": '#include "A/Limits.h"\nstruct S {};'})
        self.assertEqual(resolver.resolveExpression("Source/A/Settings.h", "NetLimit::kMaxSize"), "1024")
        self.assertEqual(resolver.resolveExpression("Source/A/Settings.h", "-NetLimit::kMaxSize"), "-1024")

    def testResolvesRelativeIncludeAndTransitiveInclude(self) -> None:
        resolver = self.makeResolverInternal({
            "Source/A/Base.h": "constexpr float kScale = 0.5f;",
            "Source/A/Mid.h": '#include "Base.h"',
            "Source/B/Top.h": '#include "A/Mid.h"'})
        self.assertEqual(resolver.resolveExpression("Source/B/Top.h", "kScale"), "0.5")

    def testEvaluatesArithmeticAcrossHeaders(self) -> None:
        resolver = self.makeResolverInternal({
            "Source/A/Base.h": "constexpr int kBase = 1 << 4;\nenum class Mode { A, B = 5, C };",
            "Source/A/Top.h": '#include "A/Base.h"\nconstexpr int kTwice = static_cast<int>( kBase * 2 ) + 1;'})
        self.assertEqual(resolver.resolveExpression("Source/A/Top.h", "kTwice"), "33")
        self.assertEqual(resolver.resolveExpression("Source/A/Top.h", "Mode::C"), "6")
        self.assertEqual(resolver.resolveExpression("Source/A/Top.h", "kBase / 3"), "5")
        self.assertEqual(resolver.resolveExpression("Source/A/Top.h", "kBase / 4.0"), "4.0")

    def testCircularIncludeDoesNotHang(self) -> None:
        resolver = self.makeResolverInternal({
            "Source/A/One.h": '#include "A/Two.h"\nconstexpr int kOne = 1;',
            "Source/A/Two.h": '#include "A/One.h"\nconstexpr int kTwo = kOne + 1;'})
        self.assertEqual(resolver.resolveExpression("Source/A/One.h", "kTwo"), "2")
        with self.assertRaises(ConstantError) as raised:
            resolver.resolveExpression("Source/A/One.h", "kMissing")
        self.assertIn("Source/A/Two.h", str(raised.exception))

    def testSameNameInDifferentNamespacesNeedsQualification(self) -> None:
        resolver = self.makeResolverInternal({
            "Source/A/X.h": "namespace a { constexpr int kMax = 1; }\nnamespace b { constexpr int kMax = 2; }",
            "Source/A/Top.h": '#include "A/X.h"'})
        self.assertEqual(resolver.resolveExpression("Source/A/Top.h", "a::kMax"), "1")
        self.assertEqual(resolver.resolveExpression("Source/A/Top.h", "b::kMax"), "2")
        with self.assertRaises(ConstantError) as raised:
            resolver.resolveExpression("Source/A/Top.h", "kMax")
        self.assertIn("한정", str(raised.exception))

    def testOwnHeaderWinsOverIncludedHeader(self) -> None:
        resolver = self.makeResolverInternal({
            "Source/A/Far.h": "constexpr int kLimit = 9;",
            "Source/A/Near.h": '#include "A/Far.h"\nstruct S { static constexpr int kLimit = 3; };'})
        self.assertEqual(resolver.resolveExpression("Source/A/Near.h", "kLimit"), "3")

    def testMissingConstantNamesHeaderAndName(self) -> None:
        resolver = self.makeResolverInternal({"Source/A/Top.h": '#include "A/Gone.h"\nconstexpr int kA = 1;'})
        with self.assertRaises(ConstantError) as raised:
            resolver.resolveExpression("Source/A/Top.h", "kNope")
        message = str(raised.exception)
        self.assertIn("Source/A/Top.h", message)
        self.assertIn("kNope", message)
        self.assertIn("A/Gone.h", message)

    def testCyclicDefinitionAndUnsupportedShapeStop(self) -> None:
        resolver = self.makeResolverInternal({"Source/A/Top.h": "constexpr int kA = kB;\nconstexpr int kB = kA;\nconstexpr int kC = sizeof(int);"})
        with self.assertRaises(ConstantError):
            resolver.resolveExpression("Source/A/Top.h", "kA")
        with self.assertRaises(ConstantError):
            resolver.resolveExpression("Source/A/Top.h", "kC")

    def testLocalConstantInsideFunctionIsNotACandidate(self) -> None:
        resolver = self.makeResolverInternal({"Source/A/Top.h": "inline void f() { constexpr int kLocal = 1; }"})
        with self.assertRaises(ConstantError):
            resolver.resolveExpression("Source/A/Top.h", "kLocal")

    def testDefaultExpressionResolvesConstexprOnly(self) -> None:
        resolver = self.makeResolverInternal({
            "Source/A/Base.h": "namespace g { inline constexpr float kGravity = 9.81f; }\nenum class Mode { Fast, Slow };\n"
                               "enum class Kind : unsigned char { A };\ninline constexpr Kind kKindUnset = static_cast<Kind>( 0xFF );",
            "Source/A/Top.h": '#include "A/Base.h"'})
        header = "Source/A/Top.h"
        self.assertEqual(resolver.resolveDefaultExpression(header, "-g::kGravity"), "-9.81")
        self.assertEqual(resolver.resolveDefaultExpression(header, "kGravity * 2"), "19.62")
        self.assertIsNone(resolver.resolveDefaultExpression(header, "Mode::Fast"))       # enum 값은 이름이 뜻이다
        self.assertIsNone(resolver.resolveDefaultExpression(header, "kKindUnset"))       # 산술형이 아닌 상수
        self.assertIsNone(resolver.resolveDefaultExpression(header, "true"))
        self.assertIsNone(resolver.resolveDefaultExpression(header, "Vec3( kGravity )"))  # 생성자 호출
        self.assertIsNone(resolver.resolveDefaultExpression(header, '"name"'))
        self.assertIsNone(resolver.resolveDefaultExpression(header, "1.0"))

    def testDefaultExpressionStopsOnAmbiguousConstant(self) -> None:
        resolver = self.makeResolverInternal({"Source/A/Top.h": "namespace a { constexpr int kMax = 1; }\nnamespace b { constexpr int kMax = 2; }"})
        with self.assertRaises(ConstantError):
            resolver.resolveDefaultExpression("Source/A/Top.h", "kMax")

    def testFloat32ConstantsMatchGeneratorDoubleValue(self) -> None:
        resolver = self.makeResolverInternal({"Source/A/Top.h": "constexpr float32 kPi = 3.1415926535f;\nconstexpr float32 kHalfPi = kPi * 0.5f;\n"
                                                                "constexpr double kD = 0.1;\nconstexpr int kN = 7;\nenum class Mode { A };"})
        header = "Source/A/Top.h"
        # 생성기: clang 이 float 로 평가한 값을 double 로 읽어 to_chars 로 찍는다.
        self.assertEqual(formatDoubleInternal(resolver.resolveRangeConstantValue(header, "kHalfPi")), "1.5707963705062866")
        self.assertEqual(formatDoubleInternal(resolver.resolveRangeConstantValue(header, "kD")), "0.1")
        self.assertEqual(formatDoubleInternal(resolver.resolveRangeConstantValue(header, "kN")), "7")
        # 기본값 칸: 되읽으면 같은 float32 인 가장 짧은 글.
        self.assertEqual(resolver.resolveDefaultExpression(header, "kHalfPi * 0.5"), "0.7853982")
        self.assertEqual(resolver.resolveDefaultExpression(header, "kD"), "0.1")
        with self.assertRaises(ConstantError):
            resolver.resolveRangeConstantValue(header, "Mode::A")   # 생성기는 변수 선언만 찾는다

    def testRangeTextFollowsGeneratorGrammar(self) -> None:
        resolver = self.makeResolverInternal({"Source/A/Top.h": "namespace n { constexpr int kMax = 8; constexpr float32 kScale = 0.5f; }"})
        header = "Source/A/Top.h"
        self.assertEqual(resolveRangeConstantInternal("0.0, -n::kMax, _other", header, resolver, "T"), "0.0, -8, _other")
        self.assertEqual(resolveRangeConstantInternal("n::kScale", header, resolver, "T"), "0.5")
        self.assertEqual(resolveRangeConstantInternal("-1.5", header, resolver, "T"), "-1.5")
        self.assertEqual(resolveRangeConstantInternal("_field", header, resolver, "T"), "_field")
        for bad in ("n::kMax * 2", "0.5f", "n::kMax, ", "static_cast<int>(n::kMax)"):
            with self.assertRaises(ValueError, msg=bad):
                resolveRangeConstantInternal(bad, header, resolver, "T")
        with self.assertRaises(ValueError):
            resolveRangeConstantInternal("kMissing", header, resolver, "T")

    def testScopeRuleDifferenceFromGeneratorIsKnown(self) -> None:
        # 생성기(clang)는 필드가 든 타입의 안쪽 `kMax` 를 쓰지만 여기서는 같은 이름이 둘이라 멈춘다 — 틀린 숫자를 쓰지 않는 쪽의 차이(shortcut 주석).
        resolver = self.makeResolverInternal({"Source/A/Top.h": "constexpr int kMax = 1;\nstruct S { static constexpr int kMax = 2; };"})
        with self.assertRaises(ValueError):
            resolveRangeConstantInternal("kMax", "Source/A/Top.h", resolver, "S")

    def testHasConstantName(self) -> None:
        self.assertFalse(hasConstantName("1.0f"))
        self.assertFalse(hasConstantName("-0x10"))
        self.assertTrue(hasConstantName("-Limit::kMax"))


if __name__ == "__main__":
    unittest.main()
