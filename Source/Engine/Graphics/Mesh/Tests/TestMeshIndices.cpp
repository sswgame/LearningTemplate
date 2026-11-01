// MeshIndicesTest — 메시의 인덱스(고유 정점 + 삼각형마다 번호 셋): 검사, LOD 인덱스의 정점 흐름 번호, `.mesh` 왕복. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

namespace
{
    /** @brief 정점 넷을 나눠 쓰는 사각형(삼각형 둘)입니다. */
    sw::vector<sw::RHIVertex> makeQuadVertices()
    {
        sw::vector<sw::RHIVertex> listVertex( 4 );
        const float               arrPosition[4][2] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        };
        for ( size_t index = 0; index < listVertex.size(); ++index )
        {
            listVertex[index]._arrPosition[0] = arrPosition[index][0];
            listVertex[index]._arrPosition[1] = arrPosition[index][1];
            listVertex[index]._arrNormal[2]   = 1.0f;
            listVertex[index]._arrUv[0]       = arrPosition[index][0];
            listVertex[index]._arrUv[1]       = 1.0f - arrPosition[index][1];
            listVertex[index]._arrColor[3]    = 1.0f;
        }
        return listVertex;
    }

    sw::vector<uint32> makeQuadIndices()
    {
        return sw::vector<uint32>{ 0, 1, 2, 0, 2, 3 };
    }
} // namespace

/**
 * @brief [MeshIndicesTest] 공유 정점 메시는 인덱스를 그대로 받고, 범위 밖 번호 · 덜 찬 삼각형은 오류로 거절해 메시를 비우는지 검증
 */
SW_TEST_CASE( MeshIndicesTest, SetGeometryKeepsSharedVerticesAndRejectsBadIndices )
{
    sw::shared_ptr<sw::Mesh> mesh = sw::Mesh::create();
    SW_REQUIRE_TRUE( mesh->setGeometry( makeQuadVertices(), makeQuadIndices() ) );
    SW_EXPECT_EQUAL( 4u, mesh->getVertexCount() );
    SW_EXPECT_EQUAL( 6u, mesh->getIndexCount() );
    // 두 삼각형이 나눠 쓰는 정점(0 · 2)도 탄젠트가 +X 다(UV u 가 x 를 따른다).
    SW_EXPECT_NEAR( 1.0f, mesh->getVertices()[0]._arrTangent[0], 1e-5f );
    SW_EXPECT_NEAR( 1.0f, mesh->getVertices()[2]._arrTangent[0], 1e-5f );

    SW_EXPECT_LOG( "Mesh.MeshIndicesOutOfRange" );
    SW_EXPECT_FALSE( mesh->setGeometry( makeQuadVertices(), sw::vector<uint32>{ 0, 1, 4 } ) );
    SW_EXPECT_EQUAL( 0u, mesh->getVertexCount() );
    SW_EXPECT_FALSE( mesh->setGeometry( makeQuadVertices(), sw::vector<uint32>{ 0, 1 } ) );
    SW_EXPECT_EQUAL( 0u, mesh->getIndexCount() );

    // 삼각형 목록은 인덱스 0..n-1 이다.
    mesh->setVertices( sw::vector<sw::RHIVertex>( 6 ) );
    SW_EXPECT_TRUE( mesh->getIndices() == sw::Mesh::makeTriangleListIndices( 6 ) );
}

/**
 * @brief [MeshIndicesTest] LOD 단계의 인덱스(그 단계 정점 안 번호)가 정점 흐름 번호로 옮겨지고, 범위가 인덱스 흐름 기준인지 검증
 */
SW_TEST_CASE( MeshIndicesTest, LODIndicesFollowTheVertexStream )
{
    sw::shared_ptr<sw::Mesh> mesh = sw::Mesh::create();
    SW_REQUIRE_TRUE( mesh->setGeometry( makeQuadVertices(), makeQuadIndices() ) );
    sw::MeshLODLevel level{};
    level._listVertex = makeQuadVertices();
    level._listVertex.resize( 3 );
    sw::Mesh::computeTangents( level._listVertex );
    level._listIndex  = sw::vector<uint32>{ 0, 1, 2 };
    level._screenSize = 0.5f;
    sw::vector<sw::MeshLODLevel> listLevel;
    listLevel.push_back( level );
    SW_REQUIRE_TRUE( mesh->setLODs( std::move( listLevel ) ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( mesh->getLODs().size() ) );
    const sw::MeshLOD& lod = mesh->getLODs()[0];
    SW_EXPECT_EQUAL( 6u, lod._firstIndex );
    SW_EXPECT_EQUAL( 3u, lod._indexCount );
    SW_EXPECT_EQUAL( 4u, lod._firstVertex );
    SW_EXPECT_TRUE( mesh->getLODIndices() == ( sw::vector<uint32>{ 4, 5, 6 } ) );
    SW_EXPECT_EQUAL( 9u, mesh->getDrawIndexCount() );
    SW_EXPECT_EQUAL( 7u, mesh->getDrawVertexCount() );
}

/**
 * @brief [MeshIndicesTest] `.mesh` 가 인덱스와 인덱스 범위의 구간을 그대로 싣고 읽는지, 범위 밖 번호를 깨진 파일로 거절하는지 검증
 */
SW_TEST_CASE( MeshIndicesTest, AssetRoundTripKeepsIndicesAndSections )
{
    sw::MeshAssetData data{};
    data._listVertex = makeQuadVertices();
    data._listIndex  = makeQuadIndices();
    data._listSection.push_back( sw::MeshSection{ sw::hashed_string( "Left" ), 0, 3, sw::MeshPersonVisibility::Both } );
    data._listSection.push_back( sw::MeshSection{ sw::hashed_string( "Right" ), 3, 3, sw::MeshPersonVisibility::ThirdPersonOnly } );
    sw::vector<uint8> bytes;
    sw::MeshAssetFormat::makeBytes( data, bytes );

    sw::MeshAssetData read{};
    SW_REQUIRE_TRUE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( read._listVertex.size() ) );
    SW_EXPECT_TRUE( read._listIndex == makeQuadIndices() );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( read._listSection.size() ) );
    SW_EXPECT_EQUAL( 3u, read._listSection[1]._firstIndex );
    SW_EXPECT_EQUAL( 3u, read._listSection[1]._indexCount );

    // 마지막 인덱스(파일의 인덱스 끝 = 덩어리 앞)를 정점 밖 번호로 바꾸면 읽기가 진다.
    const size_t lastIndexOffset = sw::MeshAssetFormat::kHeaderSize + 4u * sw::MeshAssetFormat::kVertexSize + 5u * sizeof( uint32 );
    bytes[lastIndexOffset]       = 9;
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetIndexOutOfRange" );
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
}

/**
 * @brief [MeshIndicesTest] 삼각형 목록을 합치면 모든 칸이 비트까지 같은 정점만 하나가 되고(UV 만 다른 정점은 남는다), 고유 정점은 처음 쓰인 순서인지 검증
 */
SW_TEST_CASE( MeshIndicesTest, WeldTriangleListMergesOnlyBitIdenticalVertices )
{
    // 사각형 두 삼각형 (0, 1, 2) · (0, 2, 3) 을 풀어 쓴 것 — 마지막 모서리만 UV 를 조금 바꿔 다른 정점으로 둔다.
    const sw::vector<sw::RHIVertex> listQuad = makeQuadVertices();
    sw::vector<sw::RHIVertex>       listTriangle{ listQuad[0], listQuad[1], listQuad[2], listQuad[0], listQuad[2], listQuad[3] };
    listTriangle[4]._arrUv[0] += 0.25f;
    sw::vector<sw::RHIVertex> listVertex;
    sw::vector<uint32>        listIndex;
    sw::Mesh::weldTriangleList( listTriangle, listVertex, listIndex );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( listVertex.size() ) );
    SW_EXPECT_TRUE( listIndex == ( sw::vector<uint32>{ 0, 1, 2, 0, 3, 4 } ) );

    // 탄젠트를 구한 뒤 합치는 메시는 그림이 같다 — 모서리마다 다시 풀면 원래 정점(탄젠트 포함)과 비트까지 같다.
    sw::shared_ptr<sw::Mesh> welded = sw::Mesh::create();
    welded->setTriangleListWelded( sw::vector<sw::RHIVertex>( listTriangle ) );
    sw::vector<sw::RHIVertex> listExpected = listTriangle;
    sw::Mesh::computeTangents( listExpected );
    SW_REQUIRE_EQUAL( 6u, welded->getIndexCount() );
    for ( uint32 corner = 0; corner < 6; ++corner )
    {
        SW_EXPECT_TRUE( sw::Memory::compare( &welded->getVertices()[welded->getIndices()[corner]], &listExpected[corner], sizeof( sw::RHIVertex ) ) == 0 );
    }
}
