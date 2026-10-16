// 형태 변형 모디파이어 ① — Bubble · Bulge · Cylindrify · Globe · Hump · Radial Skew · Skew 의 알려진 점 결과와 범위 밖 보존.
#include "pch.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    bool isNearShapeValue( const float3& actual, const float3& expected )
    {
        return ( actual - expected ).getLength() < 1e-4f;
    }
} // namespace

SW_TEST_CASE( ShapeModifiersATest, BubblePushesInsideRadiusOnly )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = {
        float3{0.5f, 0.0f, 0.0f},
        float3{2.0f, 0.0f, 0.0f}
    };
    unique_ptr<MeshModifier> bubble = MeshModifierRegistry::create( "bubble" );
    SW_ASSERT_NOT_NULL( bubble.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *bubble, "Strength", 1.0f ) );
    test::MeshModifierTestUtil::applyOne( *bubble, mesh );
    // 0.5 + (1 - 0.25)² = 1.0625, 반경 밖은 그대로
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[0], float3{ 1.0625f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[1], float3{ 2.0f, 0.0f, 0.0f } ) );
}

SW_TEST_CASE( ShapeModifiersATest, BulgeWidensTheMiddleAndKeepsTheEnds )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = {
        float3{1.0f, -2.0f, 0.0f},
        float3{1.0f,  0.0f, 0.0f},
        float3{1.0f,  2.0f, 0.0f}
    };
    unique_ptr<MeshModifier> bulge = MeshModifierRegistry::create( "bulge" );
    SW_ASSERT_NOT_NULL( bulge.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *bulge, "Amount", 1.0f ) );
    test::MeshModifierTestUtil::applyOne( *bulge, mesh );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[0], float3{ 1.0f, -2.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[1], float3{ 2.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[2], float3{ 1.0f, 2.0f, 0.0f } ) );
}

SW_TEST_CASE( ShapeModifiersATest, CylindrifyFullPercentLandsOnTheCylinder )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = {
        float3{1.0f, 5.0f, 0.0f},
        float3{0.0f, 1.0f, 3.0f}
    };
    unique_ptr<MeshModifier> cylindrify = MeshModifierRegistry::create( "cylindrify" );
    SW_ASSERT_NOT_NULL( cylindrify.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *cylindrify, "Percent", 1.0f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *cylindrify, "Radius", 2.0f ) );
    test::MeshModifierTestUtil::applyOne( *cylindrify, mesh );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[0], float3{ 2.0f, 5.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[1], float3{ 0.0f, 1.0f, 2.0f } ) );
}

SW_TEST_CASE( ShapeModifiersATest, GlobeInterpolatesTowardTheFixedRadius )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = {
        float3{2.0f, 0.0f, 0.0f},
        float3{1.0f, 1.0f, 1.0f}
    };
    unique_ptr<MeshModifier> globe = MeshModifierRegistry::create( "globe" );
    SW_ASSERT_NOT_NULL( globe.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *globe, "Percent", 0.5f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *globe, "Radius", 3.0f ) );
    test::MeshModifierTestUtil::applyOne( *globe, mesh );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[0], float3{ 2.5f, 0.0f, 0.0f } ) );
    SW_EXPECT_NEAR_EQUAL( ( MathUtil::sqrt( 3.0f ) + 3.0f ) * 0.5f, mesh._listPosition[1].getLength(), 1e-4f );
}

SW_TEST_CASE( ShapeModifiersATest, HumpIsGaussianAlongTheAxis )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = {
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 0.0f, 0.0f}
    };
    unique_ptr<MeshModifier> hump = MeshModifierRegistry::create( "hump" );
    SW_ASSERT_NOT_NULL( hump.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *hump, "Height", 2.0f ) );
    test::MeshModifierTestUtil::applyOne( *hump, mesh );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[0], float3{ 0.0f, 2.0f, 0.0f } ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f * MathUtil::exp( -1.0f ), mesh._listPosition[1]._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, mesh._listPosition[1]._x, 1e-5f );
}

SW_TEST_CASE( ShapeModifiersATest, RadialSkewOpensWithHeight )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = {
        float3{1.0f, 0.0f, 0.0f},
        float3{1.0f, 2.0f, 0.0f}
    };
    unique_ptr<MeshModifier> radialSkew = MeshModifierRegistry::create( "radialskew" );
    SW_ASSERT_NOT_NULL( radialSkew.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *radialSkew, "Angle", 45.0f ) );
    test::MeshModifierTestUtil::applyOne( *radialSkew, mesh );
    // 아래(상자 바닥)는 그대로, 높이 2 는 tan 45 · 2 만큼 바깥으로
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[0], float3{ 1.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[1], float3{ 3.0f, 2.0f, 0.0f } ) );
}

SW_TEST_CASE( ShapeModifiersATest, SkewShearsLinearlyAlongTheAxis )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = {
        float3{0.0f, -2.0f, 0.0f},
        float3{0.0f,  0.0f, 0.0f},
        float3{0.0f,  2.0f, 0.0f}
    };
    unique_ptr<MeshModifier> skew = MeshModifierRegistry::create( "skew" );
    SW_ASSERT_NOT_NULL( skew.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *skew, "Amount", 2.0f ) );
    test::MeshModifierTestUtil::applyOne( *skew, mesh );
    // 주축 Y 의 방향 0 은 다음 축 Z 쪽이다
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[0], float3{ 0.0f, -2.0f, 0.0f } ) );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[1], float3{ 0.0f, 0.0f, 1.0f } ) );
    SW_EXPECT_TRUE( isNearShapeValue( mesh._listPosition[2], float3{ 0.0f, 2.0f, 2.0f } ) );
}
