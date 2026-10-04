#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SerializerUtil.h"

#include "TestFramework/TestFramework.h"

// MeshAssetTest — 메시 에셋 파일(`.mesh`) 형식 · 경로 캐시 · MeshComponent 해석. 디바이스 없음(nogpu).

namespace
{
    /** @brief 정점 하나를 위치 · 색으로 만듭니다(노멀 +Y, UV 는 위치 xy). */
    sw::RHIVertex makeTestVertex( float32 x, float32 y, float32 z, float32 red )
    {
        sw::RHIVertex vertex{};
        vertex._arrPosition[0] = x;
        vertex._arrPosition[1] = y;
        vertex._arrPosition[2] = z;
        vertex._arrNormal[1]   = 1.0f;
        vertex._arrUv[0]       = x;
        vertex._arrUv[1]       = y;
        vertex._arrColor[0]    = red;
        vertex._arrColor[3]    = 1.0f;
        return vertex;
    }

    /** @brief 삼각형 @p triangleCount 개짜리 정점 배열입니다. 삼각형마다 x 가 1 씩 밀립니다. */
    sw::vector<sw::RHIVertex> makeTestTriangles( uint32 triangleCount, float32 red )
    {
        sw::vector<sw::RHIVertex> listVertex;
        for ( uint32 triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex )
        {
            const float32 offset = static_cast<float32>( triangleIndex );
            listVertex.push_back( makeTestVertex( offset, 0.0f, 0.0f, red ) );
            listVertex.push_back( makeTestVertex( offset, 1.0f, 0.0f, red ) );
            listVertex.push_back( makeTestVertex( offset + 1.0f, 0.0f, 0.0f, red ) );
        }
        return listVertex;
    }
} // namespace

/**
 * @brief [MeshAssetTest] 쓴 정점을 그대로 다시 읽는다 — 위치 · 노멀 · UV · 색과 경계 반지름까지
 */
SW_TEST_CASE( MeshAssetTest, WriteThenReadRoundTripsEveryField )
{
    const sw::vector<sw::RHIVertex> listWritten = makeTestTriangles( 2, 0.25f );
    sw::vector<uint8>               bytes;
    sw::MeshAssetFormat::makeBytes( listWritten, bytes );
    SW_ASSERT_EQUAL( size_t( sw::MeshAssetFormat::kHeaderSize + listWritten.size() * sw::MeshAssetFormat::kVertexSize ), bytes.size() );

    sw::vector<sw::RHIVertex> listRead;
    float32                   boundingRadius = -1.0f;
    SW_ASSERT_TRUE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), listRead, &boundingRadius ) );
    SW_ASSERT_EQUAL( listWritten.size(), listRead.size() );
    for ( size_t vertexIndex = 0; vertexIndex < listRead.size(); ++vertexIndex )
    {
        SW_EXPECT_TRUE( sw::Memory::compare( &listWritten[vertexIndex], &listRead[vertexIndex], sizeof( sw::RHIVertex ) ) == 0 );
    }
    // 가장 먼 정점은 (2, 0, 0) 이다.
    SW_EXPECT_NEAR_EQUAL( 2.0f, boundingRadius, 1e-6f );

    // 파일로도 같다.
    const sw::string path = test::makeTempPath( "roundtrip.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, listWritten ) );
    sw::vector<sw::RHIVertex> listLoaded;
    SW_ASSERT_TRUE( sw::MeshAssetFormat::loadFromResource( path, listLoaded ) );
    SW_EXPECT_EQUAL( listWritten.size(), listLoaded.size() );
}

/**
 * @brief [MeshAssetTest] 머리와 길이가 맞지 않는 파일은 거절한다 — 짧은 파일 · 남는 바이트 · 다른 버전 · 매직 없음 · 삼각형이 아닌 정점 수
 * @details 거절하지 않으면 깨진 파일의 정점 수만큼 버퍼 밖을 읽거나, 반쯤 쓴 파일을 조용히 일부만 그린다.
 */
SW_TEST_CASE( MeshAssetTest, MalformedBytesAreRejected )
{
    sw::vector<uint8> valid;
    sw::MeshAssetFormat::makeBytes( makeTestTriangles( 1, 1.0f ), valid );
    sw::vector<sw::RHIVertex>    listRead;
    test::ScopedDefensiveTestLog expected( "malformed mesh assets are rejected with a warning" );

    // 머리보다 짧다.
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( valid.data(), sw::MeshAssetFormat::kHeaderSize - 1, listRead ) );
    // 정점 배열이 잘렸다.
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( valid.data(), valid.size() - 4, listRead ) );
    SW_EXPECT_TRUE( listRead.empty() );

    // 뒤에 바이트가 남는다.
    sw::vector<uint8> trailing = valid;
    trailing.push_back( 0 );
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( trailing.data(), trailing.size(), listRead ) );

    // 다른 버전.
    sw::vector<uint8> otherVersion = valid;
    otherVersion[4]                = static_cast<uint8>( sw::MeshAssetFormat::kVersion + 1 );
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( otherVersion.data(), otherVersion.size(), listRead ) );

    // 매직이 없다.
    sw::vector<uint8> noMagic = valid;
    noMagic[0]                = 'X';
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( noMagic.data(), noMagic.size(), listRead ) );

    // 정점 수가 3 의 배수가 아니다(길이는 머리에 맞춘다).
    sw::vector<sw::RHIVertex> listTwoVertex = makeTestTriangles( 1, 1.0f );
    listTwoVertex.pop_back();
    sw::vector<uint8> notTriangles;
    sw::MeshAssetFormat::makeBytes( listTwoVertex, notTriangles );
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( notTriangles.data(), notTriangles.size(), listRead ) );

    // 원본은 여전히 읽힌다(위 거절이 우연이 아니다).
    SW_EXPECT_TRUE( sw::MeshAssetFormat::readFromBytes( valid.data(), valid.size(), listRead ) );
}

/**
 * @brief [MeshAssetTest] 같은 경로는 같은 Mesh 하나를 나눠 받고, 다시 읽기는 그 Mesh 를 제자리에서 바꾼다
 * @details 컴포넌트마다 제 Mesh 를 받으면 배치 키(메시 포인터)가 갈린다. 다시 읽기가 새 Mesh 를 만들면 이미 든 컴포넌트는 옛 정점을 그린다.
 */
SW_TEST_CASE( MeshAssetTest, CacheSharesOneMeshPerPathAndReloadsInPlace )
{
    const sw::string path = test::makeTempPath( "shared.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, makeTestTriangles( 1, 1.0f ) ) );

    const sw::shared_ptr<sw::Mesh> first  = sw::MeshCache::acquire( path );
    const sw::shared_ptr<sw::Mesh> second = sw::MeshCache::acquire( path );
    SW_ASSERT_NOT_NULL( first.get() );
    SW_EXPECT_TRUE( first == second );
    SW_EXPECT_EQUAL( 3u, first->getVertexCount() );

    sw::MeshCache cache;
    SW_EXPECT_TRUE( cache.isCached( path ) );

    const uint64 contentBefore = first->getContentId();
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, makeTestTriangles( 3, 0.5f ) ) );
    SW_EXPECT_TRUE( sw::MeshCache::reloadShared( path, nullptr ) );
    SW_EXPECT_EQUAL( 9u, first->getVertexCount() );
    SW_EXPECT_TRUE( first->getContentId() != contentBefore ); // 정점 풀이 다시 올릴 근거
    SW_EXPECT_TRUE( sw::MeshCache::acquire( path ) == first );

    // 깨진 파일로 다시 읽으면 옛 정점을 지킨다.
    const uint8 arrGarbage[3] = { 1, 2, 3 };
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, arrGarbage, sizeof( arrGarbage ) ) );
    {
        test::ScopedDefensiveTestLog expected( "reloading a malformed mesh keeps the old vertices" );
        SW_EXPECT_FALSE( sw::MeshCache::reloadShared( path, nullptr ) );
    }
    SW_EXPECT_EQUAL( 9u, first->getVertexCount() );
}

/**
 * @brief [MeshAssetTest] `_meshId` 가 `.mesh` 경로면 MeshComponent 는 그 에셋의 메시를 그린다 — 내장 도형 이름은 그대로 통한다
 */
SW_TEST_CASE( MeshAssetTest, MeshComponentResolvesMeshAssetPath )
{
    const sw::string path = test::makeTempPath( "component.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, makeTestTriangles( 2, 1.0f ) ) );

    const sw::SerializeContext& ctx = sw::SerializeContext::getDefault();
    sw::GameObjectManager       manager;
    sw::GameObject*             pProp = manager.createGameObject( sw::hashed_string( "Model" ) );
    sw::MeshComponent*          pMesh = pProp->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    const sw::PropertyInfo* pMeshId = pMesh->getTypeInfo()->findPropertyInHierarchy( sw::hashed_string( "_meshId" ) );
    SW_ASSERT_NOT_NULL( pMeshId );

    SW_ASSERT_TRUE( sw::SerializerUtil::applyPropertyText( *pMeshId, pMesh, path, ctx ) );
    pMesh->onPropertyChanged( pMeshId->_name );
    SW_ASSERT_NOT_NULL( pMesh->getRawMesh() );
    SW_EXPECT_EQUAL( 6u, pMesh->getRawMesh()->getVertexCount() );
    SW_EXPECT_TRUE( pMesh->getRawMesh() == sw::MeshCache::acquire( path ).get() );

    // 세터도 같은 길이다.
    pMesh->setMeshId( "Sphere" );
    SW_EXPECT_TRUE( pMesh->getRawMesh() == sw::MeshUtil::acquirePrimitive( "Sphere" ).get() );

    // 없는 에셋은 메시 없이 남는다(그리지 않는다).
    {
        test::ScopedDefensiveTestLog expected( "a missing mesh asset leaves the component without a mesh" );
        pMesh->setMeshId( test::makeTempPath( "missing.mesh" ) );
    }
    SW_EXPECT_NULL( pMesh->getRawMesh() );
}

/**
 * @brief [MeshAssetTest] 스킨 스트림 왕복 — 본 번호 · 가중치 · 본 수가 남고, 캐시가 메시에 스킨을 건다. 본 수를 넘는 본 번호는 거절한다
 */
SW_TEST_CASE( MeshAssetTest, SkinStreamRoundTripsAndRejectsOutOfRangeJoints )
{
    sw::MeshAssetData written{};
    written._listVertex    = makeTestTriangles( 2, 0.5f );
    written._skinBoneCount = 3;
    for ( size_t vertexIndex = 0; vertexIndex < written._listVertex.size(); ++vertexIndex )
    {
        sw::MeshSkinVertex skin{};
        skin._arrJoint[0]  = static_cast<uint16>( vertexIndex % 3 );
        skin._arrJoint[1]  = 2;
        skin._arrWeight[0] = 0.75f;
        skin._arrWeight[1] = 0.25f;
        written._listSkinVertex.push_back( skin );
    }
    sw::vector<uint8> bytes;
    sw::MeshAssetFormat::makeBytes( written, bytes );
    SW_ASSERT_EQUAL( size_t( sw::MeshAssetFormat::kHeaderSize + written._listVertex.size() * ( sw::MeshAssetFormat::kVertexSize + sw::MeshAssetFormat::kSkinVertexSize ) ),
                     bytes.size() );

    sw::MeshAssetData read{};
    SW_ASSERT_TRUE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
    SW_ASSERT_TRUE( read.hasSkin() );
    SW_EXPECT_EQUAL( 3u, read._skinBoneCount );
    SW_EXPECT_EQUAL( uint16( 1 ), read._listSkinVertex[1]._arrJoint[0] );
    SW_EXPECT_EQUAL( uint16( 2 ), read._listSkinVertex[4]._arrJoint[1] );
    SW_EXPECT_NEAR_EQUAL( 0.25f, read._listSkinVertex[4]._arrWeight[1], 0.0f );

    const sw::string path = test::makeTempPath( "skinned.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, written ) );
    sw::shared_ptr<sw::Mesh> mesh = sw::MeshCache::acquire( path );
    SW_ASSERT_NOT_NULL( mesh.get() );
    SW_EXPECT_TRUE( mesh->hasSkin() );
    SW_EXPECT_EQUAL( 3u, mesh->getSkinBoneCount() );

    test::ScopedDefensiveTestLog expected( "a skin joint beyond the bone count is rejected" );
    written._listSkinVertex[2]._arrJoint[3] = 3;
    sw::MeshAssetFormat::makeBytes( written, bytes );
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
    SW_EXPECT_FALSE( read.hasSkin() );
}
