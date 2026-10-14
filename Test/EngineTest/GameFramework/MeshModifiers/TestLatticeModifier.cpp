// 격자(FFD) 모디파이어 — 항등 격자, 가중치 합(모든 점을 같이 옮기면 정점도 그만큼), 모서리 점, 해상도 올리기 보존, 내리기 표본, 대칭 이동, 저장 왕복.
#include "pch.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/LatticeModifier.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct LatticeModifierTestInternal
    {
        static const float3* getSamples( size_t& outCount )
        {
            static const float3 s_arrSample[] = { float3{ 0.0f, 0.0f, 0.0f }, float3{ 1.0f, 1.0f, 1.0f }, float3{ 0.25f, 0.5f, 0.75f },
                                                  float3{ 0.9f, 0.1f, 0.4f },  float3{ -0.3f, 1.2f, 0.5f } };
            outCount = sizeof( s_arrSample ) / sizeof( s_arrSample[0] );
            return s_arrSample;
        }
    };
} // namespace

SW_TEST_CASE( LatticeModifierTest, IdentityLatticeKeepsEveryPointIncludingOutside )
{
    for ( int32 resolution = LatticeModifier::kMinResolution; resolution <= LatticeModifier::kMaxResolution; ++resolution )
    {
        const LatticeModifier lattice( resolution );
        size_t                count    = 0;
        const float3*         pSamples = LatticeModifierTestInternal::getSamples( count );
        for ( size_t index = 0; index < count; ++index )
        {
            SW_EXPECT_TRUE( ( lattice.deformNormalized( pSamples[index] ) - pSamples[index] ).getLength() < 1e-5f );
        }
    }
}

SW_TEST_CASE( LatticeModifierTest, MovingEveryPointTranslatesEverything )
{
    // 베른슈타인 가중치의 합이 1 이므로 모든 제어점을 같은 만큼 옮기면 모든 점이 그만큼 옮겨진다
    LatticeModifier lattice( 4 );
    const float3    delta{ 0.1f, -0.2f, 0.3f };
    for ( int32 index = 0; index < lattice.getControlPointCount(); ++index )
    {
        lattice.moveControlPoint( index, delta );
    }
    size_t        count    = 0;
    const float3* pSamples = LatticeModifierTestInternal::getSamples( count );
    for ( size_t index = 0; index < count; ++index )
    {
        SW_EXPECT_TRUE( ( lattice.deformNormalized( pSamples[index] ) - ( pSamples[index] + delta ) ).getLength() < 1e-5f );
    }
}

SW_TEST_CASE( LatticeModifierTest, CornerFollowsItsControlPointAndOneMoveIsPartial )
{
    LatticeModifier lattice( 3 );
    const int32     corner = lattice.getControlPointIndex( 2, 2, 2 );
    const float3    delta{ 0.5f, 0.0f, 0.0f };
    lattice.moveControlPoint( corner, delta );
    // 모서리 점은 그 제어점 하나만 무게 1 이다
    SW_EXPECT_TRUE( ( lattice.deformNormalized( float3{ 1.0f, 1.0f, 1.0f } ) - float3{ 1.5f, 1.0f, 1.0f } ).getLength() < 1e-5f );
    // 반대쪽 모서리는 움직이지 않는다
    SW_EXPECT_TRUE( lattice.deformNormalized( float3{ 0.0f, 0.0f, 0.0f } ).getLength() < 1e-5f );
    // 가운데 점은 무게 (1/4)^3 만큼 움직인다
    const float3 center = lattice.deformNormalized( float3{ 0.5f, 0.5f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( 0.5f + 0.5f / 64.0f, center._x, 1e-5f );
}

SW_TEST_CASE( LatticeModifierTest, RaisingResolutionPreservesTheDeformation )
{
    LatticeModifier lattice( 2 );
    lattice.moveControlPoint( lattice.getControlPointIndex( 1, 1, 0 ), float3{ 0.2f, 0.3f, -0.1f } );
    lattice.moveControlPoint( lattice.getControlPointIndex( 0, 1, 1 ), float3{ -0.1f, 0.0f, 0.4f } );
    size_t         count    = 0;
    const float3*  pSamples = LatticeModifierTestInternal::getSamples( count );
    vector<float3> listBefore;
    for ( size_t index = 0; index < count; ++index )
    {
        listBefore.push_back( lattice.deformNormalized( pSamples[index] ) );
    }
    lattice.setResolution( 4, 3, 5 );
    SW_EXPECT_EQUAL( 4, lattice.getResolutionX() );
    SW_EXPECT_EQUAL( 3, lattice.getResolutionY() );
    SW_EXPECT_EQUAL( 5, lattice.getResolutionZ() );
    SW_EXPECT_EQUAL( 4 * 3 * 5, lattice.getControlPointCount() );
    for ( size_t index = 0; index < count; ++index )
    {
        SW_EXPECT_TRUE( ( lattice.deformNormalized( pSamples[index] ) - listBefore[index] ).getLength() < 1e-4f );
    }
}

SW_TEST_CASE( LatticeModifierTest, LoweringResolutionKeepsIdentityAndClamps )
{
    LatticeModifier lattice( 5 );
    lattice.setResolution( 1, 99, 3 );
    SW_EXPECT_EQUAL( LatticeModifier::kMinResolution, lattice.getResolutionX() );
    SW_EXPECT_EQUAL( LatticeModifier::kMaxResolution, lattice.getResolutionY() );
    SW_EXPECT_EQUAL( 3, lattice.getResolutionZ() );
    size_t        count    = 0;
    const float3* pSamples = LatticeModifierTestInternal::getSamples( count );
    for ( size_t index = 0; index < count; ++index )
    {
        SW_EXPECT_TRUE( ( lattice.deformNormalized( pSamples[index] ) - pSamples[index] ).getLength() < 1e-4f );
    }
}

SW_TEST_CASE( LatticeModifierTest, MirrorMovesTheOppositePointWithFlippedComponent )
{
    LatticeModifier     lattice( 3 );
    const int32         left  = lattice.getControlPointIndex( 0, 1, 1 );
    const int32         right = lattice.getControlPointIndex( 2, 1, 1 );
    const int32         mid   = lattice.getControlPointIndex( 1, 1, 1 );
    const vector<int32> listIndex{ left, mid };
    lattice.moveControlPoints( listIndex, float3{ -0.1f, 0.2f, 0.0f }, true, 0 );
    SW_EXPECT_TRUE( ( lattice.getControlPoint( left ) - ( lattice.getIdentityControlPoint( left ) + float3{ -0.1f, 0.2f, 0.0f } ) ).getLength() < 1e-6f );
    SW_EXPECT_TRUE( ( lattice.getControlPoint( right ) - ( lattice.getIdentityControlPoint( right ) + float3{ 0.1f, 0.2f, 0.0f } ) ).getLength() < 1e-6f );
    // 가운데 줄은 자기 자신이 거울이라 한 번만 옮긴다
    SW_EXPECT_TRUE( ( lattice.getControlPoint( mid ) - ( lattice.getIdentityControlPoint( mid ) + float3{ -0.1f, 0.2f, 0.0f } ) ).getLength() < 1e-6f );
}

SW_TEST_CASE( LatticeModifierTest, ExtraStateRoundTrips )
{
    LatticeModifier lattice( 3 );
    lattice.setResolution( 3, 4, 2 );
    lattice.moveControlPoint( 5, float3{ 0.25f, -0.5f, 0.125f } );
    MeshModifierBounds box{};
    box._min = float3{ -1.0f, -2.0f, -3.0f };
    box._max = float3{ 1.0f, 2.0f, 3.0f };
    lattice.setBox( box );

    JSONDocument    document;
    const JSONValue root = document.makeObject();
    lattice.writeExtraJSON( root );
    LatticeModifier reread( 2 );
    SW_ASSERT_TRUE( reread.readExtraJSON( root ) );
    SW_EXPECT_EQUAL( 3, reread.getResolutionX() );
    SW_EXPECT_EQUAL( 4, reread.getResolutionY() );
    SW_EXPECT_EQUAL( 2, reread.getResolutionZ() );
    SW_EXPECT_TRUE( ( reread.getControlPoint( 5 ) - lattice.getControlPoint( 5 ) ).getLength() < 1e-6f );
    SW_EXPECT_TRUE( reread.getBox().isValid() );
    SW_EXPECT_NEAR_EQUAL( 3.0f, reread.getBox()._max._z, 1e-6f );
}

SW_TEST_CASE( LatticeModifierTest, AppliedToMeshUsesTheFittedBox )
{
    MeshModifierMesh   mesh = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    LatticeModifier    lattice( 2 );
    const MeshModifierBounds bounds = MeshModifierBounds::makeFromPoints( mesh._listPosition );
    lattice.fitToBounds( bounds );
    // 위(Y = 1) 네 제어점을 정규 좌표 +0.25 만큼 올리면 상자 높이(4)의 1/4 = 1 만큼 위 면이 올라간다
    for ( int32 indexZ = 0; indexZ < 2; ++indexZ )
    {
        for ( int32 indexX = 0; indexX < 2; ++indexX )
        {
            lattice.moveControlPoint( lattice.getControlPointIndex( indexX, 1, indexZ ), float3{ 0.0f, 0.25f, 0.0f } );
        }
    }
    test::MeshModifierTestUtil::applyOne( lattice, mesh );
    const MeshModifierBounds after = MeshModifierBounds::makeFromPoints( mesh._listPosition );
    SW_EXPECT_NEAR_EQUAL( 3.0f, after._max._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, after._min._y, 1e-4f );
}
