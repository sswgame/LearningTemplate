// 모디파이어 의미 — Displace Limits 의 변위 자르기(스택 입력 대비), 경로 회전 최소화 프레임(3D 나선에서 직교 · 비틀림 없음, 평면 경로에서 고정 축 프레임과 같음).
#include "pch.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/PathModifierUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 법선이 +Y 인 정점 셋(원점에서 떨어진 곳 포함)의 메시입니다. */
    MeshModifierMesh makePointMesh()
    {
        MeshModifierMesh mesh{};
        mesh._listPosition = {
            float3{ 0.0f, 0.0f, 0.0f},
            float3{ 3.0f, 0.0f, 0.0f},
            float3{-1.0f, 1.0f, 2.0f}
        };
        mesh._listNormal = {
            float3{0.0f, 1.0f, 0.0f},
            float3{0.0f, 1.0f, 0.0f},
            float3{0.0f, 1.0f, 0.0f}
        };
        return mesh;
    }

    /** @brief Push(양 2) 다음 Displace Limits(구 반경 0.5)의 두 모디파이어를 만듭니다. 만들거나 매개변수를 넣지 못하면 false 입니다. */
    bool makePushAndLimits( bool bClampDisplacement, unique_ptr<MeshModifier>& outPush, unique_ptr<MeshModifier>& outLimits )
    {
        outPush   = MeshModifierRegistry::create( "push" );
        outLimits = MeshModifierRegistry::create( "displacelimits" );
        if ( outPush == nullptr || outLimits == nullptr )
            return false;
        return test::MeshModifierTestUtil::setScalar( *outPush, "Amount", 2.0f ) && test::MeshModifierTestUtil::setScalar( *outLimits, "Mode", 2.0f ) &&
               test::MeshModifierTestUtil::setScalar( *outLimits, "Radius", 0.5f ) &&
               test::MeshModifierTestUtil::setScalar( *outLimits, "Clamp Displacement", bClampDisplacement ? 1.0f : 0.0f );
    }
} // namespace

SW_TEST_CASE( MeshModifierSemanticsTest, DisplaceLimitsClampsDisplacementFromStackInput )
{
    const MeshModifierMesh   source = makePointMesh();
    unique_ptr<MeshModifier> push;
    unique_ptr<MeshModifier> limits;
    SW_ASSERT_TRUE( makePushAndLimits( true, push, limits ) );
    MeshModifierStack stack;
    SW_ASSERT_EQUAL( 0, stack.addModifier( std::move( push ) ) );
    SW_ASSERT_EQUAL( 1, stack.addModifier( std::move( limits ) ) );

    MeshModifierMesh mesh = source;
    stack.apply( mesh, MeshModifierEvaluation{} );
    for ( size_t index = 0; index < mesh.getVertexCount(); ++index )
    {
        const float3 displacement = mesh._listPosition[index] - source._listPosition[index];
        SW_EXPECT_TRUE( displacement.getLength() <= 0.5f + 1e-4f );
        // 변위 방향(법선 +Y)은 그대로다
        SW_EXPECT_NEAR_EQUAL( 0.5f, displacement._y, 1e-4f );
    }
}

SW_TEST_CASE( MeshModifierSemanticsTest, DisplaceLimitsWithoutClampMatchesPositionClamp )
{
    const MeshModifierMesh   source = makePointMesh();
    unique_ptr<MeshModifier> push;
    unique_ptr<MeshModifier> limits;
    SW_ASSERT_TRUE( makePushAndLimits( false, push, limits ) );

    // 기대값: 예전처럼 하나씩 건 결과(결과 위치를 원점 구 안으로 자름)
    MeshModifierMesh expected = source;
    test::MeshModifierTestUtil::applyOne( *push, expected );
    test::MeshModifierTestUtil::applyOne( *limits, expected );

    MeshModifierStack stack;
    SW_ASSERT_EQUAL( 0, stack.addModifier( std::move( push ) ) );
    SW_ASSERT_EQUAL( 1, stack.addModifier( std::move( limits ) ) );
    MeshModifierMesh mesh = source;
    stack.apply( mesh, MeshModifierEvaluation{} );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( expected._listPosition, mesh._listPosition ) );
    SW_EXPECT_TRUE( mesh._listPosition[1].getLength() <= 0.5f + 1e-4f );
}

SW_TEST_CASE( MeshModifierSemanticsTest, RotationMinimizingFrameOnHelixIsOrthonormalAndUntwisted )
{
    // 반지름 2, 한 바퀴에 3 만큼 오르는 나선을 네 바퀴 표본한다(주축 Y)
    constexpr uint32  kSampleCount = 400;
    constexpr float32 kRadius      = 2.0f;
    constexpr float32 kRise        = 3.0f / ( 2.0f * MathUtil::kPi );
    vector<float3>    listPosition;
    vector<float3>    listTangent;
    for ( uint32 index = 0; index < kSampleCount; ++index )
    {
        const float32 angle = static_cast<float32>( index ) / static_cast<float32>( kSampleCount - 1 ) * 8.0f * MathUtil::kPi;
        listPosition.push_back( float3{ kRadius * MathUtil::cos( angle ), kRise * angle, kRadius * MathUtil::sin( angle ) } );
        listTangent.push_back( float3{ -kRadius * MathUtil::sin( angle ), kRise, kRadius * MathUtil::cos( angle ) }.normalize() );
    }
    vector<float3> listAxisU;
    vector<float3> listAxisV;
    PathModifierUtil::computeRotationMinimizingFrames( listPosition, listTangent, 1, listAxisU, listAxisV );
    SW_ASSERT_EQUAL( listPosition.size(), listAxisV.size() );

    for ( size_t index = 0; index < listAxisV.size(); ++index )
    {
        const float3& tangent = listTangent[index];
        const float3& axisU   = listAxisU[index];
        const float3& axisV   = listAxisV[index];
        SW_EXPECT_NEAR_EQUAL( 1.0f, axisU.getLength(), 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 1.0f, axisV.getLength(), 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, axisU.dot( axisV ), 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, axisU.dot( tangent ), 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, axisV.dot( tangent ), 1e-4f );
        if ( index == 0 )
            continue;
        // 회전 최소화: 축 V 의 변화는 접선 방향 성분뿐이고 축 U 쪽으로 돌지 않는다(접선 둘레 비틀림이 접선 변화보다 훨씬 작다)
        const float3  deltaV      = axisV - listAxisV[index - 1];
        const float32 tangentTurn = ( tangent - listTangent[index - 1] ).getLength();
        SW_EXPECT_TRUE( MathUtil::abs( deltaV.dot( listAxisU[index - 1] ) ) < tangentTurn * 0.05f + 1e-5f );
    }
}

SW_TEST_CASE( MeshModifierSemanticsTest, RotationMinimizingFrameOnPlanarArcMatchesFixedAxisFrame )
{
    // YZ 평면 반원(+Y 에서 출발해 +Z 쪽으로 돈다) — 평면 경로에서는 고정 기준 축 프레임과 같아야 한다(기준 축 X 가 평면 법선)
    constexpr uint32 kSampleCount = 129;
    vector<float3>   listPosition;
    vector<float3>   listTangent;
    for ( uint32 index = 0; index < kSampleCount; ++index )
    {
        const float32 angle = MathUtil::kPi * static_cast<float32>( index ) / static_cast<float32>( kSampleCount - 1 );
        listPosition.push_back( float3{ 0.0f, MathUtil::sin( angle ), 1.0f - MathUtil::cos( angle ) } );
        listTangent.push_back( float3{ 0.0f, MathUtil::cos( angle ), MathUtil::sin( angle ) } );
    }
    vector<float3> listAxisU;
    vector<float3> listAxisV;
    PathModifierUtil::computeRotationMinimizingFrames( listPosition, listTangent, 1, listAxisU, listAxisV );
    for ( size_t index = 0; index < listAxisV.size(); ++index )
    {
        float3 fixedU{ 1.0f, 0.0f, 0.0f };
        float3 fixedV{ 0.0f, 0.0f, 1.0f };
        PathModifierUtil::makeFrame( listTangent[index], 1, fixedU, fixedV );
        SW_EXPECT_TRUE( ( listAxisU[index] - fixedU ).getLength() < 1e-4f );
        SW_EXPECT_TRUE( ( listAxisV[index] - fixedV ).getLength() < 1e-4f );
    }
}
