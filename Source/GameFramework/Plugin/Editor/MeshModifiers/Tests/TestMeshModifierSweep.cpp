#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/formatString.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierStack.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct MeshModifierSweepTestUtil
    {
        static constexpr int32 kGridCount = 5;

        /** @brief [-1, 1] 상자를 채운 격자 메시입니다. */
        static MeshModifierMesh makeGridMesh()
        {
            MeshModifierMesh mesh;
            for ( int32 z = 0; z < kGridCount; ++z )
            {
                for ( int32 y = 0; y < kGridCount; ++y )
                {
                    for ( int32 x = 0; x < kGridCount; ++x )
                    {
                        const float32 step = 2.0f / static_cast<float32>( kGridCount - 1 );
                        mesh._listPosition.push_back( float3{ -1.0f + step * static_cast<float32>( x ), -1.0f + step * static_cast<float32>( y ), -1.0f + step * static_cast<float32>( z ) } );
                        mesh._listNormal.push_back( float3{ 0.0f, 1.0f, 0.0f } );
                        mesh._listUV.push_back( float2{ static_cast<float32>( x ) * 0.25f, static_cast<float32>( y ) * 0.25f } );
                        mesh._listColor.push_back( float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
                    }
                }
            }
            return mesh;
        }

        static MeshModifierBounds makeGridBounds()
        {
            MeshModifierBounds bounds;
            bounds._min = float3{ -1.0f, -1.0f, -1.0f };
            bounds._max = float3{ 1.0f, 1.0f, 1.0f };
            return bounds;
        }

        /** @brief 격자에 맞춘 @p pID 하나짜리 스택을 만듭니다. */
        static void makeStack( MeshModifierStack& outStack, const utf8* pID )
        {
            const expected<int32> index = outStack.addModifier( pID );
            SW_REQUIRE_TRUE( index.has_value() );
            MeshModifier* pModifier = outStack.findModifier( *index );
            SW_REQUIRE_TRUE( pModifier != nullptr );
            pModifier->fitToBounds( makeGridBounds() );
        }

        static bool isSamePositions( const MeshModifierMesh& lhs, const MeshModifierMesh& rhs )
        {
            if ( lhs._listPosition.size() != rhs._listPosition.size() )
                return false;
            for ( size_t index = 0; index < lhs._listPosition.size(); ++index )
            {
                const float3 delta = lhs._listPosition[index] - rhs._listPosition[index];
                if ( MathUtil::abs( delta._x ) > 1e-4f || MathUtil::abs( delta._y ) > 1e-4f || MathUtil::abs( delta._z ) > 1e-4f )
                    return false;
            }
            return true;
        }

        /** @brief @p pName 매개변수에 값을 넣습니다. */
        static void setParam( MeshModifier& modifier, string_view name, float32 value )
        {
            const vector<MeshModifierParamDef>& listDef = modifier.getParamDefs();
            for ( uint32 index = 0; index < static_cast<uint32>( listDef.size() ); ++index )
            {
                if ( name == listDef[index]._pName )
                {
                    modifier.setScalar( index, value );
                    modifier.onParamsChanged();
                    return;
                }
            }
            SW_REQUIRE_TRUE( false );
        }
    };
} // namespace

// 등록된 모디파이어마다: 기본값으로 걸어도 정점 수가 그대로이고, 값이 유한하며, 같은 입력은 같은 결과다(문서가 약속한 결정성).
SW_TEST_CASE( MeshModifierSweepTest, EveryModifierIsFiniteAndDeterministic )
{
    vector<const MeshModifierRegistration*> listRegistration;
    MeshModifierRegistry::getRegistrations( listRegistration );
    SW_REQUIRE_TRUE( listRegistration.empty() == false );
    const MeshModifierEvaluation evaluation{};
    for ( const MeshModifierRegistration* pRegistration : listRegistration )
    {
        MeshModifierStack stack;
        MeshModifierSweepTestUtil::makeStack( stack, pRegistration->_pID );
        MeshModifierMesh first = MeshModifierSweepTestUtil::makeGridMesh();
        MeshModifierMesh again = first;
        const size_t     count = first.getVertexCount();
        stack.apply( first, evaluation );
        stack.apply( again, evaluation );
        StringBuilder<constant::buffer::kMax256> contextBuilder;
        contextBuilder.appendFormat( "modifier %#", pRegistration->_pID );
        const string context{ contextBuilder.view() };
        SW_EXPECT_TRUE_MSG( count == first.getVertexCount(), context.c_str() );
        SW_EXPECT_TRUE_MSG( MeshModifierSweepTestUtil::isSamePositions( first, again ), context.c_str() );
        for ( const float3& position : first._listPosition )
        {
            const bool bFinite = MathUtil::isFinite( position._x ) && MathUtil::isFinite( position._y ) && MathUtil::isFinite( position._z );
            SW_EXPECT_TRUE_MSG( bFinite, context.c_str() );
            if ( bFinite == false )
                break;
        }
    }
}

// 저장 문서로 왕복해도 같은 결과다 — 문서에 빠지는 칸이 있으면 다시 열 때 모양이 바뀐다.
SW_TEST_CASE( MeshModifierSweepTest, EveryModifierSurvivesTheDocumentRoundTrip )
{
    vector<const MeshModifierRegistration*> listRegistration;
    MeshModifierRegistry::getRegistrations( listRegistration );
    const MeshModifierEvaluation evaluation{};
    for ( const MeshModifierRegistration* pRegistration : listRegistration )
    {
        MeshModifierStack stack;
        MeshModifierSweepTestUtil::makeStack( stack, pRegistration->_pID );
        MeshModifierStack                        loaded;
        StringBuilder<constant::buffer::kMax256> contextBuilder;
        contextBuilder.appendFormat( "modifier %#", pRegistration->_pID );
        const string context{ contextBuilder.view() };
        SW_EXPECT_TRUE_MSG( loaded.readJSONText( stack.makeJSONText( -1 ) ), context.c_str() );
        MeshModifierMesh original = MeshModifierSweepTestUtil::makeGridMesh();
        MeshModifierMesh reread   = original;
        stack.apply( original, evaluation );
        loaded.apply( reread, evaluation );
        SW_EXPECT_TRUE_MSG( MeshModifierSweepTestUtil::isSamePositions( original, reread ), context.c_str() );
    }
}

SW_TEST_CASE( MeshModifierSweepTest, DisabledModifierLeavesTheMeshAlone )
{
    MeshModifierStack stack;
    MeshModifierSweepTestUtil::makeStack( stack, "twist" );
    MeshModifierSweepTestUtil::setParam( *stack.findModifier( 0 ), "Angle", 90.0f );
    stack.findModifier( 0 )->setEnabled( false );
    MeshModifierMesh mesh = MeshModifierSweepTestUtil::makeGridMesh();
    stack.apply( mesh, MeshModifierEvaluation{} );
    SW_EXPECT_TRUE( MeshModifierSweepTestUtil::isSamePositions( mesh, MeshModifierSweepTestUtil::makeGridMesh() ) );
}

SW_TEST_CASE( MeshModifierSweepTest, BendWithoutAnAngleIsIdentity )
{
    MeshModifierStack stack;
    MeshModifierSweepTestUtil::makeStack( stack, "bend" );
    MeshModifierSweepTestUtil::setParam( *stack.findModifier( 0 ), "Angle", 0.0f );
    MeshModifierMesh mesh = MeshModifierSweepTestUtil::makeGridMesh();
    stack.apply( mesh, MeshModifierEvaluation{} );
    SW_EXPECT_TRUE( MeshModifierSweepTestUtil::isSamePositions( mesh, MeshModifierSweepTestUtil::makeGridMesh() ) );
}

// 비틀기는 주축을 도는 회전이라 주축 좌표와 축에서의 거리가 그대로다.
SW_TEST_CASE( MeshModifierSweepTest, TwistKeepsHeightAndRadius )
{
    MeshModifierStack stack;
    MeshModifierSweepTestUtil::makeStack( stack, "twist" );
    MeshModifierSweepTestUtil::setParam( *stack.findModifier( 0 ), "Angle", 90.0f );
    const MeshModifierMesh original = MeshModifierSweepTestUtil::makeGridMesh();
    MeshModifierMesh       mesh     = original;
    stack.apply( mesh, MeshModifierEvaluation{} );
    SW_EXPECT_FALSE( MeshModifierSweepTestUtil::isSamePositions( mesh, original ) );
    for ( size_t index = 0; index < mesh._listPosition.size(); ++index )
    {
        const float3& before       = original._listPosition[index];
        const float3& after        = mesh._listPosition[index];
        const float32 radiusBefore = MathUtil::sqrt( before._x * before._x + before._z * before._z );
        const float32 radiusAfter  = MathUtil::sqrt( after._x * after._x + after._z * after._z );
        SW_EXPECT_TRUE( MathUtil::abs( before._y - after._y ) < 1e-3f );
        SW_EXPECT_TRUE( MathUtil::abs( radiusBefore - radiusAfter ) < 1e-3f );
    }
}
