/**
 * @file TestMeshVertexCheck.cpp
 * @brief GPU 정점 대조(`MeshVertexCheck`)의 차이 찾기입니다 — 되읽은 사본 두 벌을 바이트로 견주어 구간 · 정점 · 크기를 낸다(GPU 없음).
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/Mesh/MeshVertex.h"
#include "Engine/Graphics/RHI/RHIMeshVertexLayout.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"
#include "Engine/Renderer/Debug/MeshVertexCheck.h"

using namespace sw;

/**
 * @brief [MeshVertexCheckTest] 속성 구간의 노멀 한 칸이 0 으로 바뀌면 그 정점 · 속성 구간 · 다른 바이트 수를 낸다
 * @details 변이: 속성 구간 시작을 빼지 않으면 정점 번호가 어긋나 진다.
 */
SW_TEST_CASE( MeshVertexCheckTest, FindsTheVertexWhoseNormalWasLost )
{
    constexpr uint32  kVertexCount = 8;
    vector<RHIVertex> listVertex( kVertexCount );
    for ( RHIVertex& vertex : listVertex )
    {
        vertex._arrNormal[1] = 1.0f;
    }
    vector<uint8> listExpected( RHIMeshVertexStreams::computeBufferBytes( RHIMeshVertexFormat::Packed, kVertexCount ) );
    RHIMeshVertexStreams::write( RHIMeshVertexFormat::Packed, listVertex.data(), kVertexCount, 0, kVertexCount, listExpected.data() );
    vector<uint8> listActual = listExpected;
    SW_EXPECT_FALSE( MeshVertexCheck::findDifference( RHIMeshVertexFormat::Packed, kVertexCount, listExpected.data(), listActual.data() ).has_value() );

    constexpr uint32 kLostVertex                           = 6;
    const uint32     normalY                               = RHIMeshVertexStreams::computeAttributeOffset( kVertexCount ) + kLostVertex * constant::render::getMeshAttributeStride( RHIMeshVertexFormat::Packed ) + 1u;
    listActual[normalY]                                    = 0;
    const optional<MeshVertexCheck::Difference> difference = MeshVertexCheck::findDifference( RHIMeshVertexFormat::Packed, kVertexCount, listExpected.data(), listActual.data() );
    SW_REQUIRE_TRUE( difference.has_value() );
    SW_EXPECT_TRUE( difference->_bAttribute != SW_FALSE );
    SW_EXPECT_EQUAL( kLostVertex, difference->_vertexIndex );
    SW_EXPECT_EQUAL( normalY, difference->_byteOffset );
    SW_EXPECT_EQUAL( 1u, difference->_differingByteCount );
}

/** @brief [MeshVertexCheckTest] 마지막 정점의 위치가 다르면 위치 구간의 차이로 낸다. 변이: 구간 경계를 위치 보폭만큼 앞당기면 진다. */
SW_TEST_CASE( MeshVertexCheckTest, SeparatesPositionsFromAttributes )
{
    constexpr uint32 kVertexCount = 4;
    vector<uint8>    listExpected( RHIMeshVertexStreams::computeBufferBytes( RHIMeshVertexFormat::Packed, kVertexCount ) );
    vector<uint8>    listActual                                                   = listExpected;
    listActual[RHIMeshVertexStreams::computeAttributeOffset( kVertexCount ) - 1u] = 1;
    const optional<MeshVertexCheck::Difference> difference                        = MeshVertexCheck::findDifference( RHIMeshVertexFormat::Packed, kVertexCount, listExpected.data(), listActual.data() );
    SW_REQUIRE_TRUE( difference.has_value() );
    SW_EXPECT_TRUE( difference->_bAttribute == SW_FALSE );
    SW_EXPECT_EQUAL( kVertexCount - 1u, difference->_vertexIndex );
}
