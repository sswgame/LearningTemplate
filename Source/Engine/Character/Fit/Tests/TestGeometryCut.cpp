#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/CharacterGeometry.h"

using namespace sw;

// 형상 자르기(GeometryCut) — 삼각형 뽑기 · 나누기 · 붙이기 · 경계 고리 · 캡 · 닫힘 판정. 디바이스 없음(nogpu).

namespace
{
    struct GeometryCutTestUtil
    {
        /** @brief x 로 늘어선 사각형 @p quadCount 개(z = 0, 윗면 +z)입니다. 열 x 의 정점은 (x, 0) 이 2x, (x, 1) 이 2x + 1 입니다. 넓이는 사각형 수와 같습니다. */
        static AppearanceGeometry makeStrip( uint32 quadCount, float32 originX = 0.0f )
        {
            AppearanceGeometry geometry;
            for ( uint32 column = 0; column <= quadCount; ++column )
            {
                for ( uint32 row = 0; row <= 1u; ++row )
                {
                    geometry._listPosition.push_back( float3{ originX + static_cast<float32>( column ), static_cast<float32>( row ), 0.0f } );
                }
            }
            for ( uint32 quad = 0; quad < quadCount; ++quad )
            {
                const uint32 corner = quad * 2u;
                geometry._listIndex.insert( geometry._listIndex.end(), { corner, corner + 2u, corner + 3u, corner, corner + 3u, corner + 1u } );
            }
            return geometry;
        }

        /** @brief 선택 필드를 모두 채웁니다. 정점 v 의 법선은 +z, UV 는 (v, 0), 스킨은 본 v, 그룹은 0 이고, 삼각형 t 의 부품은 t 입니다. 모프 "m" 의 델타는 (v, 0, 0) 입니다. */
        static void fillOptionalProperties( AppearanceGeometry& inoutGeometry )
        {
            inoutGeometry._listGroupName = { hashed_string( "torso" ) };
            GeometryMorphTarget morph;
            morph._name = hashed_string( "m" );
            for ( uint32 vertex = 0; vertex < inoutGeometry.getVertexCount(); ++vertex )
            {
                SkinInfluence skin;
                skin._arrJoint[0]  = static_cast<uint16>( vertex );
                skin._arrWeight[0] = 1.0f;
                inoutGeometry._listNormal.push_back( float3::UnitZ );
                inoutGeometry._listUV.push_back( float2( static_cast<float32>( vertex ), 0.0f ) );
                inoutGeometry._listSkin.push_back( skin );
                inoutGeometry._listVertexGroup.push_back( static_cast<uint16>( 0 ) );
                morph._listPositionDelta.push_back( float3{ static_cast<float32>( vertex ), 0.0f, 0.0f } );
                morph._listNormalDelta.push_back( float3{ 0.0f, static_cast<float32>( vertex ), 0.0f } );
            }
            inoutGeometry._listMorph.push_back( std::move( morph ) );
            for ( uint32 triangle = 0; triangle < inoutGeometry.getTriangleCount(); ++triangle )
            {
                inoutGeometry._listTrianglePart.push_back( static_cast<uint16>( triangle ) );
            }
        }

        /**
         * @brief 한 변 1 의 정육면체입니다. 면은 바깥에서 봤을 때 (b - a) × (c - a) 가 바깥을 향합니다.
         * @param bTop · bBottom 윗면(y = 1) · 아랫면(y = 0)을 넣을지입니다. 빼면 그 자리가 열린 경계입니다. 윗면 고리의 정점은 4..7, 아랫면 고리의 정점은 0..3 입니다.
         */
        static AppearanceGeometry makeBox( bool bTop, bool bBottom )
        {
            AppearanceGeometry geometry;
            geometry._listPosition = {
                float3{0.0f, 0.0f, 0.0f},
                float3{1.0f, 0.0f, 0.0f},
                float3{1.0f, 0.0f, 1.0f},
                float3{0.0f, 0.0f, 1.0f},
                float3{0.0f, 1.0f, 0.0f},
                float3{1.0f, 1.0f, 0.0f},
                float3{1.0f, 1.0f, 1.0f},
                float3{0.0f, 1.0f, 1.0f}
            };
            addQuad( geometry, 0, 4, 5, 1 ); // z = 0
            addQuad( geometry, 3, 2, 6, 7 ); // z = 1
            addQuad( geometry, 0, 3, 7, 4 ); // x = 0
            addQuad( geometry, 1, 5, 6, 2 ); // x = 1
            if ( bTop )
                addQuad( geometry, 4, 7, 6, 5 );
            if ( bBottom )
                addQuad( geometry, 0, 1, 2, 3 );
            return geometry;
        }

        static void addQuad( AppearanceGeometry& inoutGeometry, uint32 a, uint32 b, uint32 c, uint32 d )
        {
            inoutGeometry._listIndex.insert( inoutGeometry._listIndex.end(), { a, b, c, a, c, d } );
        }

        /** @brief 정사면체 모양의 닫힌 형상입니다(삼각형 넷, 감음이 맞습니다). */
        static AppearanceGeometry makeTetrahedron()
        {
            AppearanceGeometry geometry;
            geometry._listPosition = {
                float3{0.0f, 0.0f, 0.0f},
                float3{1.0f, 0.0f, 0.0f},
                float3{0.0f, 1.0f, 0.0f},
                float3{0.0f, 0.0f, 1.0f}
            };
            geometry._listIndex = { 0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u };
            return geometry;
        }

        static float32 computeArea( const AppearanceGeometry& geometry )
        {
            float32 area = 0.0f;
            for ( uint32 triangle = 0; triangle < geometry.getTriangleCount(); ++triangle )
            {
                const float3& a = geometry._listPosition[geometry._listIndex[triangle * 3]];
                const float3& b = geometry._listPosition[geometry._listIndex[triangle * 3 + 1]];
                const float3& c = geometry._listPosition[geometry._listIndex[triangle * 3 + 2]];
                area += ( b - a ).cross( c - a ).getLength() * 0.5f;
            }
            return area;
        }

        static void expectNear( const float3& expected, const float3& actual, float32 tolerance )
        {
            SW_EXPECT_NEAR( expected._x, actual._x, tolerance );
            SW_EXPECT_NEAR( expected._y, actual._y, tolerance );
            SW_EXPECT_NEAR( expected._z, actual._z, tolerance );
        }
    };
} // namespace

/**
 * @brief [GeometryCutTest] 뽑은 삼각형이 쓰는 정점만 원래 순서로 남고, 번호 표와 모든 선택 필드가 그 정점을 따라간다
 */
SW_TEST_CASE( GeometryCutTest, ExtractKeepsUsedVerticesInOriginalOrder )
{
    using Util                = GeometryCutTestUtil;
    AppearanceGeometry source = Util::makeStrip( 3 );
    Util::fillOptionalProperties( source );
    vector<uint8> listSelected( source.getTriangleCount(), SW_FALSE );
    listSelected[2] = 7; // 0 이 아니면 뽑는다
    listSelected[3] = SW_TRUE;

    AppearanceGeometry out;
    out._listPosition.push_back( float3{ 9.0f, 9.0f, 9.0f } ); // 받는 쪽의 옛 내용은 지운다
    vector<uint32> listRemap;
    AppearanceGeometry::extractTriangles( source, listSelected, out, &listRemap );

    SW_REQUIRE_TRUE( out.isValid() );
    SW_REQUIRE_EQUAL( 4u, out.getVertexCount() );
    SW_REQUIRE_EQUAL( 2u, out.getTriangleCount() );
    const uint32 arrExpectedRemap[] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 1u, 2u, 3u, 0xFFFFFFFFu, 0xFFFFFFFFu };
    SW_REQUIRE_EQUAL( std::size( arrExpectedRemap ), listRemap.size() );
    for ( size_t vertex = 0; vertex < listRemap.size(); ++vertex )
    {
        SW_EXPECT_EQUAL( arrExpectedRemap[vertex], listRemap[vertex] );
    }
    const uint32 arrExpectedIndex[] = { 0u, 2u, 3u, 0u, 3u, 1u };
    for ( size_t corner = 0; corner < std::size( arrExpectedIndex ); ++corner )
    {
        SW_EXPECT_EQUAL( arrExpectedIndex[corner], out._listIndex[corner] );
    }
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( out._listSkin.size() ) );
    SW_EXPECT_NEAR( 1.0f, Util::computeArea( out ), 1.0e-5f );
    for ( uint32 vertex = 0; vertex < 4u; ++vertex )
    {
        const uint32 original = vertex + 2u;
        SW_EXPECT_TRUE( out._listPosition[vertex] == source._listPosition[original] );
        SW_EXPECT_NEAR( static_cast<float32>( original ), out._listUV[vertex]._x, 1.0e-6f );
        SW_EXPECT_EQUAL( static_cast<uint32>( original ), static_cast<uint32>( out._listSkin[vertex]._arrJoint[0] ) );
        SW_EXPECT_NEAR( static_cast<float32>( original ), out._listMorph[0]._listPositionDelta[vertex]._x, 1.0e-6f );
        SW_EXPECT_NEAR( static_cast<float32>( original ), out._listMorph[0]._listNormalDelta[vertex]._y, 1.0e-6f );
    }
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( out._listTrianglePart[0] ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( out._listTrianglePart[1] ) );
    SW_EXPECT_TRUE( out.findGroup( hashed_string( "torso" ) ) == uint16{ 0 } );
}

/**
 * @brief [GeometryCutTest] 마스크가 짧으면 모자란 삼각형은 뽑지 않고, 빈 마스크는 빈 형상이다. 길이가 맞지 않는 선택 필드는 없는 것으로 친다
 */
SW_TEST_CASE( GeometryCutTest, ExtractToleratesShortMaskAndPartialProperties )
{
    using Util                = GeometryCutTestUtil;
    AppearanceGeometry source = Util::makeStrip( 2 );
    source._listNormal.push_back( float3::UnitZ ); // 정점 수와 맞지 않다
    AppearanceGeometry out;

    const vector<uint8> listShort( 2, SW_TRUE );
    AppearanceGeometry::extractTriangles( source, listShort, out );
    SW_EXPECT_EQUAL( 2u, out.getTriangleCount() );
    SW_EXPECT_EQUAL( 4u, out.getVertexCount() );
    SW_EXPECT_TRUE( out._listNormal.empty() );
    SW_EXPECT_TRUE( out.isValid() );

    const vector<uint8> listEmpty;
    vector<uint32>      listRemap;
    AppearanceGeometry::extractTriangles( source, listEmpty, out, &listRemap );
    SW_EXPECT_EQUAL( 0u, out.getTriangleCount() );
    SW_EXPECT_EQUAL( 0u, out.getVertexCount() );
    SW_REQUIRE_EQUAL( source._listPosition.size(), listRemap.size() );
    SW_EXPECT_EQUAL( 0xFFFFFFFFu, listRemap[0] );
}

/**
 * @brief [GeometryCutTest] 나누면 넓이와 삼각형이 보존되고, 잘린 곳의 정점만 양쪽에 하나씩 복사된다
 */
SW_TEST_CASE( GeometryCutTest, SplitConservesAreaAndDuplicatesSeamVertices )
{
    using Util                      = GeometryCutTestUtil;
    const AppearanceGeometry source = Util::makeStrip( 4 );
    vector<uint8>            listSevered( source.getTriangleCount(), SW_FALSE );
    for ( uint32 triangle = 0; triangle < 4u; ++triangle )
    {
        listSevered[triangle] = SW_TRUE;
    }
    AppearanceGeometry kept;
    AppearanceGeometry severed;
    AppearanceGeometry::splitByTriangleMask( source, listSevered, kept, severed );

    SW_EXPECT_TRUE( kept.isValid() );
    SW_EXPECT_TRUE( severed.isValid() );
    SW_EXPECT_EQUAL( source.getTriangleCount(), kept.getTriangleCount() + severed.getTriangleCount() );
    SW_EXPECT_EQUAL( source.getVertexCount() + 2u, kept.getVertexCount() + severed.getVertexCount() ); // 잘린 열 하나(정점 둘)
    SW_EXPECT_NEAR( Util::computeArea( source ), Util::computeArea( kept ) + Util::computeArea( severed ), 1.0e-5f );
    SW_EXPECT_NEAR( 2.0f, Util::computeArea( kept ), 1.0e-5f );
    SW_EXPECT_NEAR( 2.0f, severed._listPosition[severed.getVertexCount() - 1]._x, 1.0e-6f ); // 떨어진 쪽의 끝은 x = 2 열
    SW_EXPECT_NEAR( 2.0f, kept._listPosition[0]._x, 1.0e-6f );                               // 남은 쪽의 시작도 x = 2 열
}

/**
 * @brief [GeometryCutTest] 마스크가 비거나 짧으면 없는 칸은 남는 쪽이고, 전부 표시하면 남는 쪽이 비어 있다
 */
SW_TEST_CASE( GeometryCutTest, SplitTreatsMissingMaskEntriesAsKept )
{
    using Util                      = GeometryCutTestUtil;
    const AppearanceGeometry source = Util::makeStrip( 2 );
    AppearanceGeometry       kept;
    AppearanceGeometry       severed;

    const vector<uint8> listEmpty;
    AppearanceGeometry::splitByTriangleMask( source, listEmpty, kept, severed );
    SW_EXPECT_EQUAL( source.getTriangleCount(), kept.getTriangleCount() );
    SW_EXPECT_EQUAL( 0u, severed.getTriangleCount() );
    SW_EXPECT_EQUAL( 0u, severed.getVertexCount() );

    const vector<uint8> listShort( 1, SW_TRUE );
    AppearanceGeometry::splitByTriangleMask( source, listShort, kept, severed );
    SW_EXPECT_EQUAL( 3u, kept.getTriangleCount() );
    SW_EXPECT_EQUAL( 1u, severed.getTriangleCount() );

    const vector<uint8> listAll( source.getTriangleCount(), SW_TRUE );
    AppearanceGeometry::splitByTriangleMask( source, listAll, kept, severed );
    SW_EXPECT_EQUAL( 0u, kept.getTriangleCount() );
    SW_EXPECT_EQUAL( source.getTriangleCount(), severed.getTriangleCount() );
}

/**
 * @brief [GeometryCutTest] 붙이면 인덱스가 정점 수만큼 밀리고 넓이가 더해지며, 부품 번호는 준 값이 소스의 값보다 앞선다
 */
SW_TEST_CASE( GeometryCutTest, AppendOffsetsIndicesAndFillsParts )
{
    using Util                = GeometryCutTestUtil;
    AppearanceGeometry target = Util::makeStrip( 1 );
    AppearanceGeometry source = Util::makeStrip( 2, 5.0f );
    AppearanceGeometry::appendGeometry( target, source, 7 );

    SW_REQUIRE_TRUE( target.isValid() );
    SW_EXPECT_EQUAL( 10u, target.getVertexCount() );
    SW_EXPECT_EQUAL( 6u, target.getTriangleCount() );
    SW_EXPECT_EQUAL( 4u + source._listIndex[0], target._listIndex[6] );
    SW_EXPECT_NEAR( 3.0f, Util::computeArea( target ), 1.0e-5f );
    SW_REQUIRE_EQUAL( 6u, static_cast<uint32>( target._listTrianglePart.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( CharacterGeometryConstant::kNoPart ), static_cast<uint32>( target._listTrianglePart[0] ) ); // 앞쪽은 부품이 없었다
    SW_EXPECT_EQUAL( 7u, static_cast<uint32>( target._listTrianglePart[2] ) );

    // 번호를 주지 않으면 소스의 부품 번호를 이어 간다.
    for ( uint32 triangle = 0; triangle < source.getTriangleCount(); ++triangle )
    {
        source._listTrianglePart.push_back( static_cast<uint16>( 100u + triangle ) );
    }
    AppearanceGeometry::appendGeometry( target, source );
    SW_REQUIRE_TRUE( target.isValid() );
    SW_EXPECT_EQUAL( 100u, static_cast<uint32>( target._listTrianglePart[6] ) );
    SW_EXPECT_EQUAL( 103u, static_cast<uint32>( target._listTrianglePart[9] ) );
}

/**
 * @brief [GeometryCutTest] 선택 필드가 한쪽에만 있으면 없는 쪽을 기본값으로 채워 길이를 맞춘다
 */
SW_TEST_CASE( GeometryCutTest, AppendFillsMissingOptionalProperties )
{
    using Util                = GeometryCutTestUtil;
    AppearanceGeometry target = Util::makeStrip( 1 );
    AppearanceGeometry source = Util::makeStrip( 1, 5.0f );
    Util::fillOptionalProperties( source );
    AppearanceGeometry::appendGeometry( target, source );

    SW_REQUIRE_TRUE( target.isValid() );
    SW_REQUIRE_EQUAL( 8u, static_cast<uint32>( target._listNormal.size() ) );
    SW_EXPECT_TRUE( target._listNormal[0] == float3::UnitY );
    SW_EXPECT_TRUE( target._listNormal[4] == float3::UnitZ );
    SW_EXPECT_NEAR( 0.0f, target._listUV[3]._x, 1.0e-6f );
    SW_EXPECT_NEAR( 2.0f, target._listUV[6]._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, target._listSkin[0]._arrWeight[0], 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, target._listSkin[4]._arrWeight[0], 1.0e-6f );
    SW_EXPECT_EQUAL( static_cast<uint32>( CharacterGeometryConstant::kNoGroup ), static_cast<uint32>( target._listVertexGroup[0] ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( target._listVertexGroup[4] ) );

    // 반대 방향: 필드가 있는 쪽에 없는 소스를 붙인다.
    AppearanceGeometry bare = Util::makeStrip( 1, 9.0f );
    AppearanceGeometry::appendGeometry( target, bare );
    SW_REQUIRE_TRUE( target.isValid() );
    SW_EXPECT_TRUE( target._listNormal[8] == float3::UnitY );
    SW_EXPECT_EQUAL( static_cast<uint32>( CharacterGeometryConstant::kNoGroup ), static_cast<uint32>( target._listVertexGroup[8] ) );
}

/**
 * @brief [GeometryCutTest] 정점 그룹은 번호가 아니라 이름으로 합치고, 소스 이름 표 밖의 번호는 그룹 없음이다
 */
SW_TEST_CASE( GeometryCutTest, AppendMergesGroupsByName )
{
    using Util                = GeometryCutTestUtil;
    AppearanceGeometry target = Util::makeStrip( 1 );
    target._listGroupName     = { hashed_string( "torso" ), hashed_string( "head" ) };
    target._listVertexGroup.assign( target.getVertexCount(), static_cast<uint16>( 0 ) );
    AppearanceGeometry source = Util::makeStrip( 1, 5.0f );
    source._listGroupName     = { hashed_string( "head" ), hashed_string( "arm" ) };
    source._listVertexGroup   = { 0u, 1u, 1u, 99u }; // head, arm, arm, 표 밖

    AppearanceGeometry::appendGeometry( target, source );
    SW_REQUIRE_TRUE( target.isValid() );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( target._listGroupName.size() ) );
    SW_EXPECT_TRUE( target._listGroupName[2] == hashed_string( "arm" ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( target._listVertexGroup[4] ) ); // head 는 대상의 번호 1 이다
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( target._listVertexGroup[5] ) ); // arm 은 새 번호 2 다
    SW_EXPECT_EQUAL( static_cast<uint32>( CharacterGeometryConstant::kNoGroup ), static_cast<uint32>( target._listVertexGroup[7] ) );
}

/**
 * @brief [GeometryCutTest] 모프는 이름으로 합치고, 한쪽에만 있는 모프의 다른 쪽 구간은 0 이다
 */
SW_TEST_CASE( GeometryCutTest, AppendMergesMorphsByName )
{
    using Util                 = GeometryCutTestUtil;
    AppearanceGeometry  target = Util::makeStrip( 1 );
    GeometryMorphTarget shared;
    shared._name = hashed_string( "shared" );
    shared._listPositionDelta.assign( 4, float3::UnitX );
    GeometryMorphTarget onlyTarget;
    onlyTarget._name = hashed_string( "onlyTarget" );
    onlyTarget._listPositionDelta.assign( 4, float3::UnitY );
    target._listMorph.push_back( shared );
    target._listMorph.push_back( onlyTarget );

    AppearanceGeometry  source = Util::makeStrip( 1, 5.0f );
    GeometryMorphTarget sharedSource;
    sharedSource._name = hashed_string( "shared" );
    sharedSource._listPositionDelta.assign( 4, float3::UnitZ );
    GeometryMorphTarget onlySource;
    onlySource._name = hashed_string( "onlySource" );
    onlySource._listPositionDelta.assign( 4, float3::UnitZ );
    source._listMorph.push_back( sharedSource );
    source._listMorph.push_back( onlySource );

    AppearanceGeometry::appendGeometry( target, source );
    SW_REQUIRE_TRUE( target.isValid() ); // 모든 모프의 길이가 합친 정점 수다
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( target._listMorph.size() ) );
    const GeometryMorphTarget* pShared     = target.findMorph( hashed_string( "shared" ) );
    const GeometryMorphTarget* pOnlyTarget = target.findMorph( hashed_string( "onlyTarget" ) );
    const GeometryMorphTarget* pOnlySource = target.findMorph( hashed_string( "onlySource" ) );
    SW_REQUIRE_NOT_NULL( pShared );
    SW_REQUIRE_NOT_NULL( pOnlyTarget );
    SW_REQUIRE_NOT_NULL( pOnlySource );
    SW_EXPECT_TRUE( pShared->_listPositionDelta[0] == float3::UnitX );
    SW_EXPECT_TRUE( pShared->_listPositionDelta[4] == float3::UnitZ );
    SW_EXPECT_TRUE( pOnlyTarget->_listPositionDelta[4] == float3::Zero );
    SW_EXPECT_TRUE( pOnlySource->_listPositionDelta[0] == float3::Zero );
    SW_EXPECT_TRUE( pOnlySource->_listPositionDelta[4] == float3::UnitZ );
    SW_EXPECT_TRUE( pOnlySource->_listNormalDelta.empty() );
}

/**
 * @brief [GeometryCutTest] 모프의 법선 델타는 붙이는 순서와 무관하게 남는다(결함: 앞쪽에 법선 델타가 없으면 뒤 부품의 델타를 버렸다)
 */
SW_TEST_CASE( GeometryCutTest, AppendKeepsMorphNormalDeltaInEitherOrder )
{
    using Util            = GeometryCutTestUtil;
    AppearanceGeometry  a = Util::makeStrip( 1 );
    GeometryMorphTarget morphA;
    morphA._name = hashed_string( "m" );
    morphA._listPositionDelta.assign( 4, float3::UnitX );
    a._listMorph.push_back( morphA );

    AppearanceGeometry  b      = Util::makeStrip( 1, 5.0f );
    GeometryMorphTarget morphB = morphA;
    morphB._listNormalDelta.assign( 4, float3::UnitZ );
    b._listMorph.push_back( morphB );

    // 법선 델타 없는 부품 뒤에 있는 부품을 붙인다.
    AppearanceGeometry bareFirst = a;
    AppearanceGeometry::appendGeometry( bareFirst, b );
    const GeometryMorphTarget* pBareFirst = bareFirst.findMorph( hashed_string( "m" ) );
    SW_REQUIRE_NOT_NULL( pBareFirst );
    SW_REQUIRE_EQUAL( 8u, static_cast<uint32>( pBareFirst->_listNormalDelta.size() ) );
    SW_EXPECT_TRUE( pBareFirst->_listNormalDelta[0] == float3::Zero );
    SW_EXPECT_TRUE( pBareFirst->_listNormalDelta[4] == float3::UnitZ );

    // 반대 순서.
    AppearanceGeometry normalFirst = b;
    AppearanceGeometry::appendGeometry( normalFirst, a );
    const GeometryMorphTarget* pNormalFirst = normalFirst.findMorph( hashed_string( "m" ) );
    SW_REQUIRE_NOT_NULL( pNormalFirst );
    SW_REQUIRE_EQUAL( 8u, static_cast<uint32>( pNormalFirst->_listNormalDelta.size() ) );
    SW_EXPECT_TRUE( pNormalFirst->_listNormalDelta[0] == float3::UnitZ );
    SW_EXPECT_TRUE( pNormalFirst->_listNormalDelta[4] == float3::Zero );
}

/**
 * @brief [GeometryCutTest] 경계 고리는 삼각형 감음 방향으로 나오고, 닫힌 형상 · 빈 형상에는 고리가 없다
 */
SW_TEST_CASE( GeometryCutTest, BoundaryLoopFollowsWinding )
{
    using Util = GeometryCutTestUtil;
    vector<vector<uint32>> listLoop;

    AppearanceGeometry triangle;
    triangle._listPosition = {
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 0.0f, 0.0f},
        float3{0.0f, 1.0f, 0.0f}
    };
    triangle._listIndex = { 0u, 1u, 2u };
    AppearanceGeometry::findBoundaryLoops( triangle, listLoop );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listLoop.size() ) );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( listLoop[0].size() ) );
    SW_EXPECT_EQUAL( 0u, listLoop[0][0] );
    SW_EXPECT_EQUAL( 1u, listLoop[0][1] );
    SW_EXPECT_EQUAL( 2u, listLoop[0][2] );

    AppearanceGeometry::findBoundaryLoops( Util::makeBox( false, false ), listLoop );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listLoop.size() ) ); // 위아래 구멍

    AppearanceGeometry::findBoundaryLoops( Util::makeTetrahedron(), listLoop );
    SW_EXPECT_TRUE( listLoop.empty() );

    const AppearanceGeometry empty{};
    AppearanceGeometry::findBoundaryLoops( empty, listLoop );
    SW_EXPECT_TRUE( listLoop.empty() );
}

/**
 * @brief [GeometryCutTest] UV 이음매로 갈라진 정점은 한 정점으로 보여 가짜 경계를 만들지 않는다 — 고리에는 처음 본 정점 번호가 나온다
 */
SW_TEST_CASE( GeometryCutTest, BoundaryLoopWeldsUVSeam )
{
    // 사각형 둘이 가운데 열을 위치만 같은 다른 정점(2 · 3 과 4 · 5)으로 나눈다.
    AppearanceGeometry geometry;
    geometry._listPosition = {
        float3{0.0f, 0.0f, 0.0f},
        float3{0.0f, 1.0f, 0.0f},
        float3{1.0f, 0.0f, 0.0f},
        float3{1.0f, 1.0f, 0.0f},
        float3{1.0f, 0.0f, 0.0f},
        float3{1.0f, 1.0f, 0.0f},
        float3{2.0f, 0.0f, 0.0f},
        float3{2.0f, 1.0f, 0.0f}
    };
    geometry._listIndex = { 0u, 2u, 3u, 0u, 3u, 1u, 4u, 6u, 7u, 4u, 7u, 5u };

    vector<vector<uint32>> listLoop;
    AppearanceGeometry::findBoundaryLoops( geometry, listLoop );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listLoop.size() ) );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( listLoop[0].size() ) );
    for ( const uint32 vertex : listLoop[0] )
    {
        SW_EXPECT_TRUE( vertex != 4u && vertex != 5u );
    }
}

/**
 * @brief [GeometryCutTest] 한 점에서 만나는 나비 모양은 첫 경계만 잇고(문서화한 한계), 멈추지 않거나 깨진 고리를 내지 않는다. 퇴화 삼각형은 경계가 아니다
 */
SW_TEST_CASE( GeometryCutTest, BoundaryLoopHandlesDegenerateInput )
{
    AppearanceGeometry bowtie;
    bowtie._listPosition = {
        float3{ 0.0f,  0.0f, 0.0f},
        float3{ 1.0f,  0.0f, 0.0f},
        float3{ 0.0f,  1.0f, 0.0f},
        float3{-1.0f,  0.0f, 0.0f},
        float3{ 0.0f, -1.0f, 0.0f}
    };
    bowtie._listIndex = { 0u, 1u, 2u, 0u, 3u, 4u };
    vector<vector<uint32>> listLoop;
    AppearanceGeometry::findBoundaryLoops( bowtie, listLoop );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listLoop.size() ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listLoop[0].size() ) );

    AppearanceGeometry degenerate;
    degenerate._listPosition = {
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 0.0f, 0.0f}
    };
    degenerate._listIndex = { 0u, 1u, 0u };
    AppearanceGeometry::findBoundaryLoops( degenerate, listLoop );
    SW_EXPECT_TRUE( listLoop.empty() );
}

/**
 * @brief [GeometryCutTest] 캡을 붙이면 닫히고 넓이가 구멍만큼 늘며, 캡의 감음 · 법선은 바깥을 향하고 UV 는 [0, 1] 안이다
 */
SW_TEST_CASE( GeometryCutTest, CapClosesHoleFacingOutward )
{
    using Util                   = GeometryCutTestUtil;
    const AppearanceGeometry box = Util::makeBox( false, true );
    SW_EXPECT_FALSE( AppearanceGeometry::isClosed( box ) );

    AppearanceGeometry cap;
    SW_REQUIRE_EQUAL( 1u, AppearanceGeometry::createCaps( box, cap ) );
    SW_REQUIRE_TRUE( cap.isValid() );
    SW_REQUIRE_EQUAL( 5u, cap.getVertexCount() ); // 중심 + 고리 넷(복사)
    SW_REQUIRE_EQUAL( 4u, cap.getTriangleCount() );
    SW_EXPECT_NEAR( 1.0f, Util::computeArea( cap ), 1.0e-5f );
    Util::expectNear( float3{ 0.5f, 1.0f, 0.5f }, cap._listPosition[0], 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, cap._listUV[0]._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.5f, cap._listUV[0]._y, 1.0e-6f );
    for ( uint32 vertex = 0; vertex < cap.getVertexCount(); ++vertex )
    {
        Util::expectNear( float3::UnitY, cap._listNormal[vertex], 1.0e-5f );
        SW_EXPECT_TRUE( cap._listUV[vertex]._x >= -1.0e-5f && cap._listUV[vertex]._x <= 1.0f + 1.0e-5f );
        SW_EXPECT_TRUE( cap._listUV[vertex]._y >= -1.0e-5f && cap._listUV[vertex]._y <= 1.0f + 1.0e-5f );
    }
    for ( uint32 triangle = 0; triangle < cap.getTriangleCount(); ++triangle )
    {
        const float3& a = cap._listPosition[cap._listIndex[triangle * 3]];
        const float3& b = cap._listPosition[cap._listIndex[triangle * 3 + 1]];
        const float3& c = cap._listPosition[cap._listIndex[triangle * 3 + 2]];
        SW_EXPECT_TRUE( ( b - a ).cross( c - a )._y > 0.0f );
    }

    AppearanceGeometry closed = box;
    AppearanceGeometry::appendGeometry( closed, cap );
    SW_EXPECT_TRUE( AppearanceGeometry::isClosed( closed ) );
    SW_EXPECT_NEAR( Util::computeArea( box ) + 1.0f, Util::computeArea( closed ), 1.0e-5f );
}

/**
 * @brief [GeometryCutTest] 캡의 스킨은 고리 정점 것을 복사하고 중심은 고리 첫 정점 것이다
 */
SW_TEST_CASE( GeometryCutTest, CapCopiesSkinFromLoop )
{
    using Util             = GeometryCutTestUtil;
    AppearanceGeometry box = Util::makeBox( false, true );
    Util::fillOptionalProperties( box );
    vector<vector<uint32>> listLoop;
    AppearanceGeometry::findBoundaryLoops( box, listLoop );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listLoop.size() ) );

    AppearanceGeometry cap;
    SW_REQUIRE_EQUAL( 1u, AppearanceGeometry::createCaps( box, cap ) );
    SW_REQUIRE_EQUAL( 5u, static_cast<uint32>( cap._listSkin.size() ) );
    SW_EXPECT_EQUAL( listLoop[0][0], static_cast<uint32>( cap._listSkin[0]._arrJoint[0] ) );
    for ( uint32 corner = 0; corner < 4u; ++corner )
    {
        SW_EXPECT_EQUAL( listLoop[0][corner], static_cast<uint32>( cap._listSkin[corner + 1u]._arrJoint[0] ) );
        SW_EXPECT_TRUE( cap._listPosition[corner + 1u] == box._listPosition[listLoop[0][corner]] );
    }
}

/**
 * @brief [GeometryCutTest] 이음매 표시가 있으면 표시가 선 고리만 막는다 — 자른 구멍만 막고 원래 구멍(목, 소매 끝)은 둔다
 */
SW_TEST_CASE( GeometryCutTest, CapSeamFilterSelectsLoops )
{
    using Util                   = GeometryCutTestUtil;
    const AppearanceGeometry box = Util::makeBox( false, false );
    AppearanceGeometry       cap;
    SW_EXPECT_EQUAL( 2u, AppearanceGeometry::createCaps( box, cap ) );
    SW_EXPECT_EQUAL( 10u, cap.getVertexCount() );

    vector<uint8> listSeam( box.getVertexCount(), SW_FALSE );
    SW_EXPECT_EQUAL( 0u, AppearanceGeometry::createCaps( box, cap, &listSeam ) );
    SW_EXPECT_EQUAL( 0u, cap.getVertexCount() );

    listSeam[6] = SW_TRUE; // 윗면 고리의 정점
    SW_EXPECT_EQUAL( 1u, AppearanceGeometry::createCaps( box, cap, &listSeam ) );
    SW_REQUIRE_EQUAL( 5u, cap.getVertexCount() );
    SW_EXPECT_NEAR( 1.0f, cap._listPosition[0]._y, 1.0e-5f );

    // 표시가 고리보다 짧아도 읽지 않을 뿐 오류가 아니다.
    const vector<uint8> listShort( 2, SW_TRUE );
    SW_EXPECT_EQUAL( 1u, AppearanceGeometry::createCaps( box, cap, &listShort ) );
    SW_EXPECT_NEAR( 0.0f, cap._listPosition[0]._y, 1.0e-5f );
}

/**
 * @brief [GeometryCutTest] 한 줄로 늘어선 퇴화 고리도 NaN 없이 대체 법선으로 막는다. 경계가 없으면 캡도 없다
 */
SW_TEST_CASE( GeometryCutTest, CapHandlesDegenerateLoop )
{
    using Util = GeometryCutTestUtil;
    AppearanceGeometry collinear;
    collinear._listPosition = {
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 0.0f, 0.0f},
        float3{2.0f, 0.0f, 0.0f}
    };
    collinear._listIndex = { 0u, 1u, 2u };
    AppearanceGeometry cap;
    SW_REQUIRE_EQUAL( 1u, AppearanceGeometry::createCaps( collinear, cap ) );
    SW_REQUIRE_TRUE( cap.isValid() );
    SW_EXPECT_TRUE( cap._listNormal[0] == float3::UnitY );
    for ( const float2& uv : cap._listUV )
    {
        SW_EXPECT_TRUE( MathUtil::isFinite( uv._x ) && MathUtil::isFinite( uv._y ) );
    }

    SW_EXPECT_EQUAL( 0u, AppearanceGeometry::createCaps( Util::makeTetrahedron(), cap ) );
    SW_EXPECT_EQUAL( 0u, cap.getVertexCount() );
    const AppearanceGeometry empty{};
    SW_EXPECT_EQUAL( 0u, AppearanceGeometry::createCaps( empty, cap ) );
}

/**
 * @brief [GeometryCutTest] 닫힘은 방향 있는 모서리가 각각 정확히 한 번, 반대 방향이 한 번일 때다 — 감음이 뒤집히거나 면이 겹치거나 퇴화하면 닫히지 않았다
 */
SW_TEST_CASE( GeometryCutTest, IsClosedRequiresConsistentManifold )
{
    using Util = GeometryCutTestUtil;
    SW_EXPECT_TRUE( AppearanceGeometry::isClosed( Util::makeTetrahedron() ) );
    SW_EXPECT_TRUE( AppearanceGeometry::isClosed( Util::makeBox( true, true ) ) );
    SW_EXPECT_FALSE( AppearanceGeometry::isClosed( Util::makeBox( true, false ) ) );

    const AppearanceGeometry empty{};
    SW_EXPECT_FALSE( AppearanceGeometry::isClosed( empty ) );

    AppearanceGeometry flipped = Util::makeTetrahedron();
    std::swap( flipped._listIndex[0], flipped._listIndex[1] );
    SW_EXPECT_FALSE( AppearanceGeometry::isClosed( flipped ) );

    AppearanceGeometry duplicated = Util::makeTetrahedron();
    duplicated._listIndex.insert( duplicated._listIndex.end(), { 0u, 2u, 1u } ); // 같은 면이 두 번
    SW_EXPECT_FALSE( AppearanceGeometry::isClosed( duplicated ) );

    AppearanceGeometry degenerate = Util::makeTetrahedron();
    degenerate._listIndex.insert( degenerate._listIndex.end(), { 0u, 0u, 1u } );
    SW_EXPECT_FALSE( AppearanceGeometry::isClosed( degenerate ) );
}

/**
 * @brief [GeometryCutTest] 닫힘 판정은 위치로 용접한다 — 정점을 복사해 나눠도 닫힌 형상이다
 */
SW_TEST_CASE( GeometryCutTest, IsClosedWeldsDuplicatedVertices )
{
    using Util                  = GeometryCutTestUtil;
    AppearanceGeometry geometry = Util::makeTetrahedron();
    geometry._listPosition.push_back( geometry._listPosition[0] ); // 정점 0 의 복사본 4
    geometry._listIndex[0] = 4u;
    SW_EXPECT_TRUE( AppearanceGeometry::isClosed( geometry ) );
}
