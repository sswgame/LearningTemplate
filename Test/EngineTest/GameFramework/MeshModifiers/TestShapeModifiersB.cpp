// 형태 변형 모디파이어 ② — Spherify · Squeeze · Scale · Sinus Curve · Rolled · Rubber · Simple Mod 의 알려진 점 결과와 보존 성질.
#include "pch.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ShapeModifiersBTestInternal
    {
        static bool isNear( const float3& actual, const float3& expected ) { return ( actual - expected ).getLength() < 1e-4f; }

        static float32 computeBoxVolume( const vector<float3>& listPoint )
        {
            const float3 size = MeshModifierBounds::makeFromPoints( listPoint ).getSize();
            return size._x * size._y * size._z;
        }
    };
} // namespace

SW_TEST_CASE( ShapeModifiersBTest, SpherifyFullPercentPutsEveryVertexOnTheSphere )
{
    MeshModifierMesh         mesh     = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    unique_ptr<MeshModifier> spherify = MeshModifierRegistry::create( "spherify" );
    SW_ASSERT_NOT_NULL( spherify.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *spherify, "Percent", 1.0f ) );
    test::MeshModifierTestUtil::applyOne( *spherify, mesh );
    // 2 x 4 x 2 상자 → 반경 2
    for ( const float3& point : mesh._listPosition )
    {
        SW_EXPECT_NEAR_EQUAL( 2.0f, point.getLength(), 1e-4f );
    }
}

SW_TEST_CASE( ShapeModifiersBTest, SqueezePreservesBoxVolume )
{
    MeshModifierMesh         mesh    = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    const float32            before  = ShapeModifiersBTestInternal::computeBoxVolume( mesh._listPosition );
    unique_ptr<MeshModifier> squeeze = MeshModifierRegistry::create( "squeeze" );
    SW_ASSERT_NOT_NULL( squeeze.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *squeeze, "Amount", 0.5f ) );
    test::MeshModifierTestUtil::applyOne( *squeeze, mesh );
    const MeshModifierBounds after = MeshModifierBounds::makeFromPoints( mesh._listPosition );
    SW_EXPECT_NEAR_EQUAL( 2.0f, after.getSize()._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( before, ShapeModifiersBTestInternal::computeBoxVolume( mesh._listPosition ), 1e-3f );
}

SW_TEST_CASE( ShapeModifiersBTest, ScaleMultipliesUniformAndPerAxis )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 1.0f, 1.0f, 1.0f } };
    unique_ptr<MeshModifier> scale = MeshModifierRegistry::create( "scale" );
    SW_ASSERT_NOT_NULL( scale.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *scale, "Uniform", 2.0f ) );
    uint32 axisScaleIndex = 0;
    SW_ASSERT_TRUE( scale->tryGetParamIndex( "Axis Scale", axisScaleIndex ) );
    scale->setVector3( axisScaleIndex, float3{ 1.0f, 0.5f, 1.0f } );
    scale->onParamsChanged();
    test::MeshModifierTestUtil::applyOne( *scale, mesh );
    SW_EXPECT_TRUE( ShapeModifiersBTestInternal::isNear( mesh._listPosition[0], float3{ 2.0f, 1.0f, 2.0f } ) );
}

SW_TEST_CASE( ShapeModifiersBTest, SinusCurveFollowsOneSinePeriod )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, -2.0f, 0.0f }, float3{ 0.0f, -1.0f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 2.0f, 0.0f } };
    unique_ptr<MeshModifier> sinus = MeshModifierRegistry::create( "sinuscurve" );
    SW_ASSERT_NOT_NULL( sinus.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *sinus, "Amplitude", 1.0f ) );
    test::MeshModifierTestUtil::applyOne( *sinus, mesh );
    // t = 0, 0.25, 0.5 → sin 0, 1, 0 (주축 Y 의 방향 0 은 Z)
    SW_EXPECT_NEAR_EQUAL( 0.0f, mesh._listPosition[0]._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, mesh._listPosition[1]._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, mesh._listPosition[2]._z, 1e-4f );
}

SW_TEST_CASE( ShapeModifiersBTest, RolledQuarterTurnWrapsThenContinuesStraight )
{
    // 반경 1, 0.25 바퀴 → 말리는 길이 π/2. 높이 π/2 인 점은 (z 1, y 1) 로 눕고, 1 더 위의 점은 끝 접선(+Z)을 따라 곧게
    const float32    quarter = MathUtil::kHalfPi;
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, quarter, 0.0f }, float3{ 0.0f, quarter + 1.0f, 0.0f } };
    unique_ptr<MeshModifier> rolled = MeshModifierRegistry::create( "rolled" );
    SW_ASSERT_NOT_NULL( rolled.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *rolled, "Turns", 0.25f ) );
    test::MeshModifierTestUtil::applyOne( *rolled, mesh );
    SW_EXPECT_TRUE( ShapeModifiersBTestInternal::isNear( mesh._listPosition[0], float3{ 0.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( ShapeModifiersBTestInternal::isNear( mesh._listPosition[1], float3{ 0.0f, 1.0f, 1.0f } ) );
    SW_EXPECT_TRUE( ShapeModifiersBTestInternal::isNear( mesh._listPosition[2], float3{ 0.0f, 1.0f, 2.0f } ) );
}

SW_TEST_CASE( ShapeModifiersBTest, RubberStretchesTheTopAndThinsIt )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 1.0f, 0.0f, 0.0f }, float3{ 1.0f, 4.0f, 0.0f } };
    unique_ptr<MeshModifier> rubber = MeshModifierRegistry::create( "rubber" );
    SW_ASSERT_NOT_NULL( rubber.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *rubber, "Amount", 1.0f ) );
    test::MeshModifierTestUtil::applyOne( *rubber, mesh );
    // 기본 감쇠 곡선 w = t: 바닥은 그대로, 꼭대기는 1 늘고 1/√(1 + 1/4) 로 가늘어진다
    SW_EXPECT_TRUE( ShapeModifiersBTestInternal::isNear( mesh._listPosition[0], float3{ 1.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( ShapeModifiersBTestInternal::isNear( mesh._listPosition[1], float3{ 1.0f / MathUtil::sqrt( 1.25f ), 5.0f, 0.0f } ) );
}

SW_TEST_CASE( ShapeModifiersBTest, SimpleModAppliesPerAxisPolynomial )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 2.0f, 3.0f, 4.0f } };
    unique_ptr<MeshModifier> simpleMod = MeshModifierRegistry::create( "simplemod" );
    SW_ASSERT_NOT_NULL( simpleMod.get() );
    uint32 offsetIndex    = 0;
    uint32 quadraticIndex = 0;
    SW_ASSERT_TRUE( simpleMod->tryGetParamIndex( "Offset", offsetIndex ) );
    SW_ASSERT_TRUE( simpleMod->tryGetParamIndex( "Quadratic", quadraticIndex ) );
    simpleMod->setVector3( offsetIndex, float3{ 1.0f, 0.0f, 0.0f } );
    simpleMod->setVector3( quadraticIndex, float3{ 0.0f, 0.0f, 1.0f } );
    simpleMod->onParamsChanged();
    test::MeshModifierTestUtil::applyOne( *simpleMod, mesh );
    // x: 2 + 1, y 그대로, z: 4 + 4² = 20
    SW_EXPECT_TRUE( ShapeModifiersBTestInternal::isNear( mesh._listPosition[0], float3{ 3.0f, 3.0f, 20.0f } ) );
}
