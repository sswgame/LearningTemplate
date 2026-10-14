// 개별 변형 모디파이어 — 휘기(90 도면 끝이 옆으로 눕는다), 비틀기(축 위 점은 그대로), 늘이기(부피 보존), 노이즈(시드 결정성 · 시드마다 다름), 법선 밀기.
#include "pch.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct DeformModifiersTestInternal
    {
        static float32 computeBoxVolume( const vector<float3>& listPoint )
        {
            const float3 size = MeshModifierBounds::makeFromPoints( listPoint ).getSize();
            return size._x * size._y * size._z;
        }
    };
} // namespace

SW_TEST_CASE( DeformModifiersTest, BendQuarterTurnLaysTheTopSideways )
{
    // 주축 Y 길이 4, 90 도 휘기 → 반지름 r = 4 / (π/2). 원점에서 위로 2 인 점은 π/4 만큼 감겨 (휨 방향 = 다음 축 Z) r(1 - cos π/4) 로 눕는다
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, -2.0f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 2.0f, 0.0f } };
    unique_ptr<MeshModifier> bend = MeshModifierRegistry::create( "bend" );
    SW_ASSERT_NOT_NULL( bend.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *bend, "Angle", 90.0f ) );
    test::MeshModifierTestUtil::applyOne( *bend, mesh );
    const float32 radius = 4.0f / MathUtil::kHalfPi;
    const float32 cosine = MathUtil::cos( MathUtil::kPi * 0.25f );
    SW_EXPECT_TRUE( mesh._listPosition[1].getLength() < 1e-5f );
    SW_EXPECT_NEAR_EQUAL( radius * ( 1.0f - cosine ), mesh._listPosition[2]._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( radius * MathUtil::sin( MathUtil::kPi * 0.25f ), mesh._listPosition[2]._y, 1e-4f );
    // 아래 끝도 같은 쪽(+Z)으로 휜다 — 원호의 양 끝
    SW_EXPECT_NEAR_EQUAL( radius * ( 1.0f - cosine ), mesh._listPosition[0]._z, 1e-4f );
}

SW_TEST_CASE( DeformModifiersTest, TwistKeepsPointsOnTheAxis )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, -2.0f, 0.0f }, float3{ 0.0f, 2.0f, 0.0f }, float3{ 1.0f, 2.0f, 0.0f } };
    unique_ptr<MeshModifier> twist = MeshModifierRegistry::create( "twist" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *twist, "Angle", 180.0f ) );
    test::MeshModifierTestUtil::applyOne( *twist, mesh );
    SW_EXPECT_TRUE( ( mesh._listPosition[0] - float3{ 0.0f, -2.0f, 0.0f } ).getLength() < 1e-5f );
    SW_EXPECT_TRUE( ( mesh._listPosition[1] - float3{ 0.0f, 2.0f, 0.0f } ).getLength() < 1e-5f );
    // 위 끝(+2)은 180 · (2 / 4) = 90 도 돌아 X 축 점이 축을 따라 돈다 — 축까지 거리는 그대로다
    const float3& turned = mesh._listPosition[2];
    SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::sqrt( turned._x * turned._x + turned._z * turned._z ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, turned._x, 1e-5f );
}

SW_TEST_CASE( DeformModifiersTest, StretchPreservesBoxVolume )
{
    MeshModifierMesh         mesh    = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    const float32            before  = DeformModifiersTestInternal::computeBoxVolume( mesh._listPosition );
    unique_ptr<MeshModifier> stretch = MeshModifierRegistry::create( "stretch" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stretch, "Amount", 1.5f ) );
    test::MeshModifierTestUtil::applyOne( *stretch, mesh );
    const MeshModifierBounds after = MeshModifierBounds::makeFromPoints( mesh._listPosition );
    SW_EXPECT_NEAR_EQUAL( 10.0f, after.getSize()._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( before, DeformModifiersTestInternal::computeBoxVolume( mesh._listPosition ), 1e-3f );
}

SW_TEST_CASE( DeformModifiersTest, NoiseIsSeedDeterministicAndSeedSensitive )
{
    const MeshModifierMesh   source = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    unique_ptr<MeshModifier> first  = MeshModifierRegistry::create( "noise" );
    unique_ptr<MeshModifier> second = MeshModifierRegistry::create( "noise" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *first, "Amplitude", 0.3f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *second, "Amplitude", 0.3f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *first, "Seed", 7.0f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *second, "Seed", 7.0f ) );
    MeshModifierMesh meshA = source;
    MeshModifierMesh meshB = source;
    test::MeshModifierTestUtil::applyOne( *first, meshA );
    test::MeshModifierTestUtil::applyOne( *second, meshB );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( meshA._listPosition, meshB._listPosition ) );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( source._listPosition, meshA._listPosition ) > 1e-3f );

    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *second, "Seed", 8.0f ) );
    MeshModifierMesh meshC = source;
    test::MeshModifierTestUtil::applyOne( *second, meshC );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( meshA._listPosition, meshC._listPosition ) > 1e-3f );
}

SW_TEST_CASE( DeformModifiersTest, PushMovesAlongNormalAndWeightScalesIt )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, 0.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f } };
    mesh._listNormal   = { float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f } };
    mesh._listWeight   = { 1.0f, 0.5f };
    unique_ptr<MeshModifier> push = MeshModifierRegistry::create( "push" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *push, "Amount", 2.0f ) );
    test::MeshModifierTestUtil::applyOne( *push, mesh );
    SW_EXPECT_NEAR_EQUAL( 2.0f, mesh._listPosition[0]._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, mesh._listPosition[1]._y, 1e-5f );
}
