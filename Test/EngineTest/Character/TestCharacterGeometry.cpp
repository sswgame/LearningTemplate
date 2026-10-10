#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Character/Fit/GeometryCut.h"
#include "Engine/Character/Fit/MeshMerger.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Character/Hit/Dismemberment.h"

#include "EngineTest/CharacterTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// 부품 병합(MeshMerger) · 신체 절단(캡) · 표면 상태(영역 채널 · UV 도장 · 찢김).

namespace
{
    struct CharacterGeometryTestInternal
    {
        static AppearanceGeometry makeCylinder( float32 radius, float32 yMin, float32 yMax, uint32 segmentCount, uint32 ringCount, bool bCapped )
        {
            test::CharacterTestUtil::CylinderDesc desc;
            desc._radius       = radius;
            desc._yMin         = yMin;
            desc._yMax         = yMax;
            desc._segmentCount = segmentCount;
            desc._ringCount    = ringCount;
            desc._bCapped      = bCapped;
            return test::CharacterTestUtil::makeCylinder( desc );
        }

        /** @brief 묶음을 "Atlas" 하나로, UV 를 왼쪽 반으로 옮기는 훅. */
        class AtlasHooks final : public IMeshMergeHooks
        {
        public:
            hashed_string getMaterialGroup( const MeshMergeSource& source ) const override
            {
                (void)source;
                return hashed_string( "Atlas" );
            }
            float2 remapUv( const MeshMergeSource& source, const float2& uv ) const override
            {
                (void)source;
                return float2( uv._x * 0.5f, uv._y );
            }
        };
    };
} // namespace

/**
 * @brief [MeshMergerTest] 스켈레톤(애니메이션 단위)을 넘어 병합하지 않는다
 */
SW_TEST_CASE( MeshMergerTest, NeverMergesAcrossSkeletons )
{
    using Internal                = CharacterGeometryTestInternal;
    const AppearanceGeometry body = Internal::makeCylinder( 0.04f, 0.0f, 1.0f, 8, 4, false );
    const AppearanceGeometry hat  = Internal::makeCylinder( 0.1f, 1.0f, 1.2f, 8, 2, false );
    MeshMergeSource          arrSource[2];
    arrSource[0]._name       = hashed_string( "Body" );
    arrSource[0]._pGeometry  = &body;
    arrSource[0]._skeletonID = 1;
    arrSource[1]._name       = hashed_string( "Hat" );
    arrSource[1]._pGeometry  = &hat;
    arrSource[1]._skeletonID = 2;
    MergedMesh merged;
    string     error;
    SW_EXPECT_FALSE( MeshMerger::merge( vector_reference<const MeshMergeSource>( arrSource, 2 ), nullptr, merged, &error ) );
    SW_EXPECT_TRUE( StringUtil::contains( error, "another skeleton" ) );
    arrSource[1]._skeletonID = 1;
    SW_EXPECT_TRUE( MeshMerger::merge( vector_reference<const MeshMergeSource>( arrSource, 2 ), nullptr, merged, &error ) );
    SW_EXPECT_EQUAL( body.getTriangleCount() + hat.getTriangleCount(), merged._geometry.getTriangleCount() );
}

/**
 * @brief [MeshMergerTest] 잘린 삼각형을 빼고 정점을 압축하고, 피팅 델타를 싣고, 머티리얼 묶음별 구간을 낸다
 */
SW_TEST_CASE( MeshMergerTest, DropsCutTrianglesAndGroupsSections )
{
    using Internal                  = CharacterGeometryTestInternal;
    const AppearanceGeometry body   = Internal::makeCylinder( 0.04f, 0.0f, 1.0f, 8, 4, false );
    const AppearanceGeometry sleeve = Internal::makeCylinder( 0.06f, 0.5f, 1.0f, 8, 2, false );
    FitPartResult            bodyFit;
    bodyFit.initialize( body.getTriangleCount(), body.getVertexCount() );
    vector<uint8> listHidden( body.getTriangleCount(), SW_FALSE );
    for ( uint32 triangle = 0; triangle < body.getTriangleCount() / 2; ++triangle )
    {
        listHidden[triangle] = SW_TRUE; // 아래 절반을 숨긴다
    }
    SW_EXPECT_EQUAL( body.getTriangleCount() / 2, bodyFit.hideTriangles( listHidden ) );
    bodyFit._listVertexDelta.assign( body.getVertexCount(), float3( 0.0f, 0.0f, 0.01f ) );

    MeshMergeSource arrSource[2];
    arrSource[0]._name      = hashed_string( "Body" );
    arrSource[0]._material  = hashed_string( "Skin" );
    arrSource[0]._pGeometry = &body;
    arrSource[0]._pFit      = &bodyFit;
    arrSource[1]._name      = hashed_string( "Sleeve" );
    arrSource[1]._material  = hashed_string( "Cloth" );
    arrSource[1]._pGeometry = &sleeve;
    MergedMesh merged;
    SW_ASSERT_TRUE( MeshMerger::merge( vector_reference<const MeshMergeSource>( arrSource, 2 ), nullptr, merged, nullptr ) );
    SW_EXPECT_EQUAL( body.getTriangleCount() / 2 + sleeve.getTriangleCount(), merged._geometry.getTriangleCount() );
    // 아래 절반을 숨겼으니 맨 아래 고리(y = 0, 0.25)의 정점은 남지 않는다.
    for ( const float3& position : merged._geometry._listPosition )
    {
        SW_EXPECT_TRUE( position._y >= 0.5f - 1.0e-5f );
    }
    SW_EXPECT_TRUE( merged._geometry.isValid() );
    SW_ASSERT_EQUAL( size_t( 2 ), merged._listSection.size() );
    SW_EXPECT_TRUE( merged._listSection[0]._materialGroup == hashed_string( "Skin" ) );
    SW_EXPECT_EQUAL( body.getTriangleCount() / 2 * 3, merged._listSection[0]._indexCount );
    for ( uint32 triangle = 0; triangle < merged._geometry.getTriangleCount(); ++triangle )
    {
        const bool bBodyTriangle = triangle < body.getTriangleCount() / 2;
        SW_EXPECT_EQUAL( bBodyTriangle ? uint16( 0 ) : uint16( 1 ), merged._geometry._listTrianglePart[triangle] );
    }
    // 첫 정점 = 처음 보이는 삼각형의 첫 정점 + 피팅 델타.
    const float3 firstVisible = body._listPosition[body._listIndex[body.getTriangleCount() / 2 * 3]] + float3( 0.0f, 0.0f, 0.01f );
    SW_EXPECT_TRUE( float3::getDistance( firstVisible, merged._geometry._listPosition[0] ) < 1.0e-6f );

    // 아틀라스 훅 — 한 묶음, UV 를 옮긴다.
    Internal::AtlasHooks hooks;
    SW_ASSERT_TRUE( MeshMerger::merge( vector_reference<const MeshMergeSource>( arrSource, 2 ), &hooks, merged, nullptr ) );
    SW_ASSERT_EQUAL( size_t( 1 ), merged._listSection.size() );
    for ( const float2& uv : merged._geometry._listUv )
    {
        SW_EXPECT_TRUE( uv._x <= 0.5f + 1.0e-5f );
    }
}

/**
 * @brief [MeshMergerTest] 쉬는 강체 부품은 소켓 본에 가중치 1 로 묶여 병합되고, 뽑을 때 원래 형상으로 떼어 낸다
 */
SW_TEST_CASE( MeshMergerTest, RestingRigidPartIsBoundToSocketBoneAndExtracted )
{
    using Internal          = CharacterGeometryTestInternal;
    AppearanceGeometry body = Internal::makeCylinder( 0.04f, 0.0f, 1.0f, 8, 4, false );
    test::CharacterTestUtil::skinToBone( body, 1 );
    const AppearanceGeometry sword = test::CharacterTestUtil::makeBox( float3( 0.0f, 0.3f, 0.0f ), float3( 0.01f, 0.3f, 0.02f ) );
    MeshMergeSource          arrSource[2];
    arrSource[0]._name          = hashed_string( "Body" );
    arrSource[0]._pGeometry     = &body;
    arrSource[1]._name          = hashed_string( "Sword" );
    arrSource[1]._pGeometry     = &sword;
    arrSource[1]._socketBone    = 2;
    arrSource[1]._restTransform = float4x4::createRotationZ( MathUtil::kHalfPi ) * float4x4::createTranslation( 0.1f, 0.8f, -0.1f );
    MergedMesh merged;
    SW_ASSERT_TRUE( MeshMerger::merge( vector_reference<const MeshMergeSource>( arrSource, 2 ), nullptr, merged, nullptr ) );
    const MergedPartRange& swordRange = merged._listPart[1];
    for ( uint32 vertex = swordRange._vertexStart; vertex < swordRange._vertexStart + swordRange._vertexCount; ++vertex )
    {
        SW_EXPECT_EQUAL( uint16( 2 ), merged._geometry._listSkin[vertex]._arrJoint[0] );
        SW_EXPECT_NEAR_EQUAL( 1.0f, merged._geometry._listSkin[vertex]._arrWeight[0], 1.0e-6f );
    }
    SW_EXPECT_TRUE( float3::getDistance( float3::transform( sword._listPosition[0], arrSource[1]._restTransform ),
                                         merged._geometry._listPosition[swordRange._vertexStart] ) < 1.0e-5f );

    AppearanceGeometry extracted;
    SW_ASSERT_TRUE( MeshMerger::extractPart( merged, hashed_string( "Sword" ), extracted ) );
    SW_ASSERT_EQUAL( sword.getVertexCount(), extracted.getVertexCount() );
    SW_EXPECT_EQUAL( sword.getTriangleCount(), extracted.getTriangleCount() );
    for ( uint32 vertex = 0; vertex < sword.getVertexCount(); ++vertex )
    {
        SW_EXPECT_TRUE( float3::getDistance( sword._listPosition[vertex], extracted._listPosition[vertex] ) < 1.0e-5f );
    }
    SW_EXPECT_FALSE( extracted.isSkinned() );
    SW_EXPECT_FALSE( MeshMerger::extractPart( merged, hashed_string( "Shield" ), extracted ) );
}

/**
 * @brief [DismembermentTest] 영역을 잘라 떨어진 조각과 남은 몸을 나누고, 자른 자리만 캡으로 막아 둘 다 닫힌다 — 마스크는 보임 마스크에 그대로
 */
SW_TEST_CASE( DismembermentTest, SeveredRegionProducesClosedCappedPieces )
{
    using Internal          = CharacterGeometryTestInternal;
    AppearanceGeometry body = Internal::makeCylinder( 0.05f, 0.0f, 1.0f, 16, 10, true );
    SW_ASSERT_TRUE( GeometryCutUtil::isClosed( body ) );
    test::CharacterTestUtil::skinBySplit( body, 0.6f, 1, 2 );
    const CharacterBoneArray bones = test::CharacterTestUtil::makeArmBones();
    FitTables                tables;
    BodyRegionDef            forearm;
    forearm._name = hashed_string( "Forearm" );
    forearm._listBone.push_back( hashed_string( "lowerarm" ) );
    tables.addRegion( forearm );
    vector<uint16> listRegion;
    tables.assignRegions( body, bones, listRegion );

    const uint16        arrSevered[1] = { 0 };
    DismembermentResult result;
    SW_ASSERT_TRUE( DismembermentUtil::severRegions( body, listRegion, vector_reference<const uint16>( arrSevered, 1 ), result ) );
    SW_EXPECT_TRUE( result._severed.getTriangleCount() > 0 );
    SW_EXPECT_EQUAL( body.getTriangleCount(), result._remaining.getTriangleCount() + result._severed.getTriangleCount() );
    SW_EXPECT_FALSE( GeometryCutUtil::isClosed( result._remaining ) ); // 막기 전에는 열려 있다
    SW_EXPECT_TRUE( result._remainingCap.getTriangleCount() > 0 );
    SW_EXPECT_TRUE( result._severedCap.getTriangleCount() > 0 );

    AppearanceGeometry closedRemaining = result._remaining;
    GeometryCutUtil::appendGeometry( closedRemaining, result._remainingCap );
    SW_EXPECT_TRUE( GeometryCutUtil::isClosed( closedRemaining ) );
    AppearanceGeometry closedSevered = result._severed;
    GeometryCutUtil::appendGeometry( closedSevered, result._severedCap );
    SW_EXPECT_TRUE( GeometryCutUtil::isClosed( closedSevered ) );
    // 캡 법선은 잘린 자리에서 바깥 — 남은 몸 캡은 위(+Y), 떨어진 조각 캡은 아래(−Y).
    SW_EXPECT_TRUE( result._remainingCap._listNormal[0]._y > 0.9f );
    SW_EXPECT_TRUE( result._severedCap._listNormal[0]._y < -0.9f );

    // 같은 마스크가 조립된 메시의 보임 마스크로 간다.
    FitPartResult bodyFit;
    bodyFit.initialize( body.getTriangleCount(), body.getVertexCount() );
    SW_EXPECT_EQUAL( result._severed.getTriangleCount(), bodyFit.hideTriangles( result._listTriangleSevered ) );

    // 원래 열린 구멍(끝을 막지 않은 원기둥)은 막지 않는다 — 자른 자리 하나만.
    const AppearanceGeometry openTube    = Internal::makeCylinder( 0.05f, 0.0f, 1.0f, 16, 10, false );
    AppearanceGeometry       skinnedTube = openTube;
    test::CharacterTestUtil::skinBySplit( skinnedTube, 0.6f, 1, 2 );
    tables.assignRegions( skinnedTube, bones, listRegion );
    SW_ASSERT_TRUE( DismembermentUtil::severRegions( skinnedTube, listRegion, vector_reference<const uint16>( arrSevered, 1 ), result ) );
    vector<vector<uint32>> listLoop;
    GeometryCutUtil::findBoundaryLoops( result._remaining, listLoop );
    SW_ASSERT_EQUAL( size_t( 2 ), listLoop.size() ); // 아래 끝 + 자른 자리
    const bool            bFirstIsBottom = result._remaining._listPosition[listLoop[0][0]]._y < 0.25f;
    const vector<uint32>& cutLoop        = bFirstIsBottom ? listLoop[1] : listLoop[0];
    SW_EXPECT_EQUAL( static_cast<uint32>( cutLoop.size() ), result._remainingCap.getTriangleCount() ); // 자른 자리 고리만 부채꼴로
}

/**
 * @brief [SurfaceStateTest] 표면 채널은 데이터 — 영역 값 정하기 · 감쇠, 머티리얼 파라미터, 모르는 채널은 실패
 */
SW_TEST_CASE( SurfaceStateTest, RegionChannelsSetAndDecay )
{
    SurfaceChannelTable channels;
    SW_ASSERT_TRUE( channels.loadFromXMLText( "<SurfaceChannels>"
                                              "  <Channel name='Wet' decayPerSecond='0.1'/>"
                                              "  <Channel name='Blood' decayPerSecond='0.05' maskResolution='32'/>"
                                              "  <Channel name='Tear' maskResolution='64' clip='true'/>"
                                              "</SurfaceChannels>",
                                              "test.surfacechannels.xml" ) );
    {
        test::ScopedLogCollector logs;
        SW_TEST_DEFENSIVE_SCOPE( "unknown attributes in surface channel data are load errors" );
        SurfaceChannelTable broken;
        SW_EXPECT_FALSE( broken.loadFromXMLText( "<SurfaceChannels><Channel name='Wet' fade='1'/></SurfaceChannels>", "broken.surfacechannels.xml" ) );
        SW_EXPECT_TRUE( logs.countContaining( "unknown attribute 'fade'" ) > 0 );
    }
    CharacterSurfaceState state;
    state.initialize( channels, 3, 2 );
    SW_EXPECT_TRUE( state.setRegionValue( 1, hashed_string( "Wet" ), 2.0f ) ); // 최댓값 1 로 묶인다
    SW_EXPECT_NEAR_EQUAL( 1.0f, state.getRegionValue( 1, hashed_string( "Wet" ) ), 1.0e-6f );
    SW_EXPECT_FALSE( state.setRegionValue( 1, hashed_string( "Mud" ), 1.0f ) );
    SW_EXPECT_FALSE( state.setRegionValue( 5, hashed_string( "Wet" ), 1.0f ) );
    state.tick( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 0.8f, state.getRegionValue( 1, hashed_string( "Wet" ) ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, state.getRegionValue( 0, hashed_string( "Wet" ) ), 1.0e-6f );
    vector<float32> listParameter;
    state.getRegionParameters( 1, listParameter );
    SW_ASSERT_EQUAL( size_t( 3 ), listParameter.size() );
    SW_EXPECT_NEAR_EQUAL( 0.8f, listParameter[0], 1.0e-5f );
}

/**
 * @brief [SurfaceStateTest] 맞은 자리 도장 — UV 원 안의 칸이 오르고 밖은 그대로, 감쇠 채널은 줄고, 찢김(감쇠 0)은 남는다
 */
SW_TEST_CASE( SurfaceStateTest, HitStampWritesUvMaskAndDecays )
{
    SurfaceChannelTable channels;
    SurfaceChannelDef   blood;
    blood._name           = hashed_string( "Blood" );
    blood._decayPerSecond = 0.25f;
    blood._maskResolution = 32;
    channels.addChannel( blood );
    SurfaceChannelDef tear;
    tear._name           = hashed_string( "Tear" );
    tear._maskResolution = 32;
    tear._bClip          = SW_TRUE;
    channels.addChannel( tear );

    CharacterSurfaceState state;
    state.initialize( channels, 2, 1 );
    SW_ASSERT_TRUE( state.stampHit( 0, 1, hashed_string( "Blood" ), float2( 0.25f, 0.5f ), 0.1f, 1.0f ) );
    SW_ASSERT_TRUE( state.stampHit( 0, CharacterGeometryConstant::kNoGroup, hashed_string( "Tear" ), float2( 0.75f, 0.5f ), 0.1f, 1.0f ) );
    SW_EXPECT_FALSE( state.stampHit( 3, 1, hashed_string( "Blood" ), float2( 0.5f, 0.5f ), 0.1f, 1.0f ) );
    const SurfaceMask* pBloodMask = state.findMask( 0, hashed_string( "Blood" ) );
    const SurfaceMask* pTearMask  = state.findMask( 0, hashed_string( "Tear" ) );
    SW_ASSERT_NOT_NULL( pBloodMask );
    SW_ASSERT_NOT_NULL( pTearMask );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pBloodMask->sample( float2( 0.25f, 0.5f ) ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pBloodMask->sample( float2( 0.25f, 0.56f ) ), 1.0e-6f ); // 반지름 안
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBloodMask->sample( float2( 0.25f, 0.75f ) ), 1.0e-6f ); // 반지름 밖
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBloodMask->sample( float2( 0.75f, 0.5f ) ), 1.0e-6f );  // 다른 채널의 자리
    SW_EXPECT_NEAR_EQUAL( 1.0f, state.getRegionValue( 1, hashed_string( "Blood" ) ), 1.0e-6f );

    state.tick( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pBloodMask->sample( float2( 0.25f, 0.5f ) ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pTearMask->sample( float2( 0.75f, 0.5f ) ), 1.0e-6f ); // 찢김은 그대로
}

/**
 * @brief [SurfaceStateTest] 다 찢긴 삼각형 — UV 마스크가 덮은 삼각형만 표시되고, 잘라 내기와 같은 보임 마스크 길로 빠진다
 */
SW_TEST_CASE( SurfaceStateTest, TornTrianglesLeaveTheVisibleMask )
{
    using Internal                  = CharacterGeometryTestInternal;
    const AppearanceGeometry sleeve = Internal::makeCylinder( 0.06f, 0.0f, 1.0f, 16, 16, false );
    SurfaceMask              tearMask;
    tearMask.initialize( 64 );
    (void)tearMask.stampCircle( float2( 0.5f, 0.5f ), 0.2f, 1.0f );
    vector<uint8> listTorn;
    const uint32  tornCount = SurfaceMaskUtil::markTornTriangles( sleeve, tearMask, 0.5f, listTorn );
    SW_EXPECT_TRUE( tornCount > 0 );
    for ( uint32 triangle = 0; triangle < sleeve.getTriangleCount(); ++triangle )
    {
        float2 centroid( 0.0f, 0.0f );
        for ( uint32 corner = 0; corner < 3; ++corner )
        {
            const float2& uv = sleeve._listUv[sleeve._listIndex[triangle * 3 + corner]];
            centroid         = float2( centroid._x + uv._x / 3.0f, centroid._y + uv._y / 3.0f );
        }
        const float32 distance = MathUtil::sqrt( ( centroid._x - 0.5f ) * ( centroid._x - 0.5f ) + ( centroid._y - 0.5f ) * ( centroid._y - 0.5f ) );
        if ( distance > 0.25f )
            SW_EXPECT_TRUE_MSG( listTorn[triangle] == SW_FALSE, "triangles outside the tear stay" );
        if ( distance < 0.08f )
            SW_EXPECT_TRUE_MSG( listTorn[triangle] == SW_TRUE, "triangles deep inside the tear are torn" );
    }
    FitPartResult sleeveFit;
    sleeveFit.initialize( sleeve.getTriangleCount(), sleeve.getVertexCount() );
    SW_EXPECT_EQUAL( tornCount, sleeveFit.hideTriangles( listTorn ) );
    SW_EXPECT_EQUAL( tornCount, sleeveFit._cutTriangleCount );

    // 병합이 다 찢긴 삼각형을 뺀다.
    MeshMergeSource source;
    source._name      = hashed_string( "Sleeve" );
    source._pGeometry = &sleeve;
    source._pFit      = &sleeveFit;
    MergedMesh merged;
    SW_ASSERT_TRUE( MeshMerger::merge( vector_reference<const MeshMergeSource>( &source, 1 ), nullptr, merged, nullptr ) );
    SW_EXPECT_EQUAL( sleeve.getTriangleCount() - tornCount, merged._geometry.getTriangleCount() );
}

/**
 * @brief [GeometryCutTest] 부호만 다른 대칭 꼭짓점(±1 · ±0.5 · ±0.25 상자)을 서로 다른 정점으로 용접한다 — 키가 겹치면 닫힌 상자가 열린 것으로 나온다
 */
SW_TEST_CASE( GeometryCutTest, WeldKeepsSymmetricCornersApart )
{
    SW_EXPECT_TRUE( GeometryCutUtil::isClosed( test::CharacterTestUtil::makeBox( float3( 0.0f, 0.0f, 0.0f ), float3( 1.0f, 0.5f, 0.25f ) ) ) );
    SW_EXPECT_TRUE( GeometryCutUtil::isClosed( test::CharacterTestUtil::makeBox( float3( 0.0f, 0.0f, 0.0f ), float3( 2.0f, 1.0f, 0.5f ) ) ) );
    SW_EXPECT_TRUE( GeometryCutUtil::isClosed( test::CharacterTestUtil::makeBox( float3( 0.0f, 0.0f, 0.0f ), float3( 0.5f, 0.5f, 0.5f ) ) ) );
}
