// 절차 · 애니메이션 모디파이어 — 구김 · 퍼지는 물결 · 종이 넘김 · 나무 휘기 · 법선 노이즈 · 축 늘이기 노이즈 · 깃발 · 피벗 · 범위 제한 · 칠하기.
#include "pch.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ProceduralModifiersTestInternal
    {
        static void applyAtTime( const MeshModifier& modifier, MeshModifierMesh& inoutMesh, float32 time )
        {
            MeshModifierEvaluation evaluation{};
            evaluation._time = time;
            modifier.apply( inoutMesh, evaluation );
        }

        static bool setVector3( MeshModifier& modifier, string_view name, const float3& value )
        {
            uint32 paramIndex = 0;
            if ( modifier.tryGetParamIndex( name, paramIndex ) == false )
                return false;
            modifier.setVector3( paramIndex, value );
            modifier.onParamsChanged();
            return true;
        }

        static bool setNumberList( MeshModifier& modifier, string_view name, const vector<float32>& listNumber )
        {
            uint32 paramIndex = 0;
            if ( modifier.tryGetParamIndex( name, paramIndex ) == false )
                return false;
            modifier.setNumberList( paramIndex, listNumber );
            modifier.onParamsChanged();
            return true;
        }

        /** @brief X 방향으로 0..width 에 놓인 평평한 띠(Y = 0)입니다. */
        static MeshModifierMesh makeStrip( uint32 count, float32 width )
        {
            MeshModifierMesh mesh{};
            for ( uint32 index = 0; index <= count; ++index )
            {
                const float32 x = width * static_cast<float32>( index ) / static_cast<float32>( count );
                mesh._listPosition.push_back( float3{ x, 0.0f, 0.0f } );
                mesh._listPosition.push_back( float3{ x, 0.0f, 1.0f } );
            }
            return mesh;
        }
    };
} // namespace

SW_TEST_CASE( ProceduralModifiersTest, CrumpleIsDeterministicAndMovesTheBox )
{
    const MeshModifierMesh   source  = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    unique_ptr<MeshModifier> crumple = MeshModifierRegistry::create( "crumple" );
    SW_ASSERT_NOT_NULL( crumple.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *crumple, "Amplitude", 0.2f ) );
    MeshModifierMesh meshA = source;
    MeshModifierMesh meshB = source;
    test::MeshModifierTestUtil::applyOne( *crumple, meshA );
    test::MeshModifierTestUtil::applyOne( *crumple, meshB );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( meshA._listPosition, meshB._listPosition ) );
    const float32 moved = test::MeshModifierTestUtil::computeMaxDistance( source._listPosition, meshA._listPosition );
    SW_EXPECT_TRUE( moved > 1e-3f );
    // 노이즈는 [-1, 1] 이고 섞기는 볼록 결합이라 변위는 진폭 · √3 을 넘지 않는다
    SW_EXPECT_TRUE( moved <= 0.2f * 1.7321f + 1e-4f );
}

SW_TEST_CASE( ProceduralModifiersTest, DynamicRippleStartsFlatAndSpreadsOverTime )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.25f, 0.0f, 0.0f }, float3{ 3.0f, 0.0f, 0.0f } };
    unique_ptr<MeshModifier> ripple = MeshModifierRegistry::create( "dynamicripple" );
    SW_ASSERT_NOT_NULL( ripple.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *ripple, "Amplitude", 1.0f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *ripple, "Speed", 1.0f ) );

    MeshModifierMesh atZero = mesh;
    ProceduralModifiersTestInternal::applyAtTime( *ripple, atZero, 0.0f );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( mesh._listPosition, atZero._listPosition ) < 1e-5f );

    // t = 1: 파면 반지름 1. 안쪽 점(r = 0.25)은 sin(2π · (0.25 - 1)) = 1 만큼 오르고 바깥 점(r = 3)은 아직 그대로
    MeshModifierMesh atOne = mesh;
    ProceduralModifiersTestInternal::applyAtTime( *ripple, atOne, 1.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, atOne._listPosition[0]._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, atOne._listPosition[1]._y, 1e-6f );
}

SW_TEST_CASE( ProceduralModifiersTest, PageFlipFullProgressMirrorsThePage )
{
    const MeshModifierMesh   source = ProceduralModifiersTestInternal::makeStrip( 8, 4.0f );
    unique_ptr<MeshModifier> flip   = MeshModifierRegistry::create( "pageflip" );
    SW_ASSERT_NOT_NULL( flip.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *flip, "Curl Radius", 0.5f ) );

    // 기즈모 중심이 원점이라 모디파이어 공간 = 메시 로컬. 진행도 1 → 말림선이 x = 0, 반 바퀴(π r) 넘은 점은 뒤집혀 높이 2r 에 눕는다
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *flip, "Progress", 1.0f ) );
    MeshModifierMesh mesh = source;
    test::MeshModifierTestUtil::applyOne( *flip, mesh );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isFinite( mesh._listPosition ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, mesh._listPosition[0]._x, 1e-5f ); // 시작선(x = 0)은 그대로
    const float3& last = mesh._listPosition[16];                   // x = 4
    SW_EXPECT_NEAR_EQUAL( -( 4.0f - MathUtil::kPi * 0.5f ), last._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, last._y, 1e-4f );

    // 중간 진행도에서 말림선 앞쪽(x < 2)은 그대로다
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *flip, "Progress", 0.5f ) );
    MeshModifierMesh half = source;
    test::MeshModifierTestUtil::applyOne( *flip, half );
    SW_EXPECT_TRUE( ( half._listPosition[2] - source._listPosition[2] ).getLength() < 1e-6f ); // x = 0.5
}

SW_TEST_CASE( ProceduralModifiersTest, TreeBendKeepsTheRootAndBendsTheTop )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, -2.0f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 2.0f, 0.0f } };
    unique_ptr<MeshModifier> tree = MeshModifierRegistry::create( "treebend" );
    SW_ASSERT_NOT_NULL( tree.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *tree, "Strength", 1.0f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *tree, "Sway", 0.0f ) );
    test::MeshModifierTestUtil::applyOne( *tree, mesh );
    SW_EXPECT_NEAR_EQUAL( 0.0f, mesh._listPosition[0]._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, mesh._listPosition[1]._x, 1e-5f ); // t = 0.5 → t² = 0.25
    SW_EXPECT_NEAR_EQUAL( 1.0f, mesh._listPosition[2]._x, 1e-5f );
}

SW_TEST_CASE( ProceduralModifiersTest, TreeBendSwaysWithTime )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, -2.0f, 0.0f }, float3{ 0.0f, 2.0f, 0.0f } };
    unique_ptr<MeshModifier> tree = MeshModifierRegistry::create( "treebend" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *tree, "Strength", 1.0f ) );
    MeshModifierMesh early = mesh;
    MeshModifierMesh late  = mesh;
    ProceduralModifiersTestInternal::applyAtTime( *tree, early, 0.0f );
    ProceduralModifiersTestInternal::applyAtTime( *tree, late, 0.25f ); // 주파수 1 → sin(π/2) = 1
    SW_EXPECT_NEAR_EQUAL( 1.0f, early._listPosition[1]._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.5f, late._listPosition[1]._x, 1e-4f );
}

SW_TEST_CASE( ProceduralModifiersTest, VertNoiseMovesOnlyAlongTheNormal )
{
    const MeshModifierMesh   source = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    unique_ptr<MeshModifier> noise  = MeshModifierRegistry::create( "vertnoise" );
    SW_ASSERT_NOT_NULL( noise.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *noise, "Amplitude", 0.3f ) );
    MeshModifierMesh mesh = source;
    test::MeshModifierTestUtil::applyOne( *noise, mesh );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( source._listPosition, mesh._listPosition ) > 1e-3f );
    for ( size_t index = 0; index < source.getVertexCount(); ++index )
    {
        const float3 offset  = mesh._listPosition[index] - source._listPosition[index];
        const float3 tangent = offset - source._listNormal[index] * offset.dot( source._listNormal[index] );
        SW_EXPECT_TRUE( tangent.getLength() < 1e-4f );
    }
}

SW_TEST_CASE( ProceduralModifiersTest, ZStretchNoiseScalesTheChosenAxis )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 1.0f, 2.0f, 3.0f }, float3{ -1.0f, 0.5f, -2.0f } };
    unique_ptr<MeshModifier> stretch = MeshModifierRegistry::create( "zstretchnoise" );
    SW_ASSERT_NOT_NULL( stretch.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stretch, "Amount", 1.0f ) );
    test::MeshModifierTestUtil::applyOne( *stretch, mesh );
    // 노이즈 0 → Z 만 두 배, X · Y 는 그대로
    SW_EXPECT_NEAR_EQUAL( 6.0f, mesh._listPosition[0]._z, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( -4.0f, mesh._listPosition[1]._z, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, mesh._listPosition[0]._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, mesh._listPosition[1]._x, 1e-6f );
}

SW_TEST_CASE( ProceduralModifiersTest, WavingKeepsTheFixedEdgeAndChangesOverTime )
{
    const MeshModifierMesh   source = ProceduralModifiersTestInternal::makeStrip( 8, 4.0f );
    unique_ptr<MeshModifier> waving = MeshModifierRegistry::create( "waving" );
    SW_ASSERT_NOT_NULL( waving.get() );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *waving, "Amplitude", 1.0f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *waving, "Wavelength", 3.0f ) );
    MeshModifierMesh early = source;
    MeshModifierMesh late  = source;
    ProceduralModifiersTestInternal::applyAtTime( *waving, early, 0.0f );
    ProceduralModifiersTestInternal::applyAtTime( *waving, late, 0.3f );
    // 고정 끝(x = 0)은 언제나 그대로
    SW_EXPECT_TRUE( ( early._listPosition[0] - source._listPosition[0] ).getLength() < 1e-6f );
    SW_EXPECT_TRUE( ( late._listPosition[1] - source._listPosition[1] ).getLength() < 1e-6f );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( early._listPosition, late._listPosition ) > 1e-3f );
    // 끝 점(x = 4, 비율 1, t = 0): sin(2π · 4 / 3)
    SW_EXPECT_NEAR_EQUAL( MathUtil::sin( MathUtil::kTwoPi * 4.0f / 3.0f ), early._listPosition[16]._y, 1e-4f );
}

SW_TEST_CASE( ProceduralModifiersTest, PivotAdjustShiftsEveryVertexOpposite )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, 0.0f, 0.0f }, float3{ 1.0f, 2.0f, 3.0f } };
    unique_ptr<MeshModifier> pivot = MeshModifierRegistry::create( "pivotadjust" );
    SW_ASSERT_NOT_NULL( pivot.get() );
    SW_ASSERT_TRUE( ProceduralModifiersTestInternal::setVector3( *pivot, "Pivot Offset", float3{ 1.0f, -1.0f, 0.5f } ) );
    test::MeshModifierTestUtil::applyOne( *pivot, mesh );
    SW_EXPECT_TRUE( ( mesh._listPosition[0] - float3{ -1.0f, 1.0f, -0.5f } ).getLength() < 1e-5f );
    SW_EXPECT_TRUE( ( mesh._listPosition[1] - float3{ 0.0f, 3.0f, 2.5f } ).getLength() < 1e-5f );
}

SW_TEST_CASE( ProceduralModifiersTest, DisplaceLimitsClampsToBoxAndSphere )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 3.0f, 0.0f, 0.0f }, float3{ 0.5f, -0.25f, 0.0f } };
    unique_ptr<MeshModifier> limits = MeshModifierRegistry::create( "displacelimits" );
    SW_ASSERT_NOT_NULL( limits.get() );

    MeshModifierMesh box = mesh;
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *limits, "Mode", 1.0f ) );
    test::MeshModifierTestUtil::applyOne( *limits, box );
    SW_EXPECT_NEAR_EQUAL( 1.0f, box._listPosition[0]._x, 1e-6f ); // 기본 상자 [-1, 1]
    SW_EXPECT_TRUE( ( box._listPosition[1] - mesh._listPosition[1] ).getLength() < 1e-6f );

    MeshModifierMesh sphere = mesh;
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *limits, "Mode", 2.0f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *limits, "Radius", 2.0f ) );
    test::MeshModifierTestUtil::applyOne( *limits, sphere );
    SW_EXPECT_NEAR_EQUAL( 2.0f, sphere._listPosition[0].getLength(), 1e-5f );
    SW_EXPECT_TRUE( ( sphere._listPosition[1] - mesh._listPosition[1] ).getLength() < 1e-6f );
}

SW_TEST_CASE( ProceduralModifiersTest, PaintColorsInsideTheBrushOnly )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, 0.0f, 0.0f }, float3{ 5.0f, 0.0f, 0.0f } };
    unique_ptr<MeshModifier> paint = MeshModifierRegistry::create( "paint" );
    SW_ASSERT_NOT_NULL( paint.get() );
    // 브러시: 원점 · 반경 1 · 세기 1 · 감쇠 0 → 안쪽은 Color(기본 빨강)로 완전히 덮인다
    SW_ASSERT_TRUE( ProceduralModifiersTestInternal::setNumberList( *paint, "Brushes", vector<float32>{ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f } ) );
    const vector<float3> before = mesh._listPosition;
    test::MeshModifierTestUtil::applyOne( *paint, mesh );
    SW_ASSERT_TRUE( mesh.hasColor() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, mesh._listColor[0]._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, mesh._listColor[0]._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, mesh._listColor[1]._y, 1e-6f ); // 바깥은 흰색 그대로
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( before, mesh._listPosition ) < 1e-5f );
}

SW_TEST_CASE( ProceduralModifiersTest, PaintWeightSelectsOnlyPaintedVertices )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.5f, 0.0f, 0.0f }, float3{ 5.0f, 0.0f, 0.0f } };
    unique_ptr<MeshModifier> paint = MeshModifierRegistry::create( "paint" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *paint, "Target", 1.0f ) );
    // 감쇠 1 → smoothstep(1 - d / r): 중심 1, d = 0.5 에서 0.5
    SW_ASSERT_TRUE( ProceduralModifiersTestInternal::setNumberList( *paint, "Brushes", vector<float32>{ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f } ) );
    test::MeshModifierTestUtil::applyOne( *paint, mesh );
    SW_ASSERT_TRUE( mesh.hasWeight() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, mesh._listWeight[0], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, mesh._listWeight[1], 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, mesh._listWeight[2], 1e-6f );
}
