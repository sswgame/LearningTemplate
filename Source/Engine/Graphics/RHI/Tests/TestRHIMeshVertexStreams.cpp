/**
 * @file TestRHIMeshVertexStreams.cpp
 * @brief CPU 정점 → GPU 메시 정점 스트림 변환(`RHIMeshVertexStreams`) — 위치 구간 · 속성 구간 배치, snorm8 반올림 · 부호, 전체 정밀도 형식이 값을 그대로 두는지.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"

using namespace sw;

namespace
{
    RHIVertex makeVertex( float32 x, float32 normalY, float32 tangentW, float32 dataInTangent )
    {
        RHIVertex vertex{};
        vertex._arrPosition[0] = x;
        vertex._arrPosition[1] = 2.0f;
        vertex._arrPosition[2] = 3.0f;
        vertex._arrNormal[1]   = normalY;
        vertex._arrUv[0]       = 1024.5f; // 물은 월드 좌표를 UV 로 싣는다 — 줄이지 않는다
        vertex._arrColor[0]    = 16.0f;   // 지형은 색에 데이터를 싣는다 — 줄이지 않는다
        vertex._arrTangent[0]  = dataInTangent;
        vertex._arrTangent[3]  = tangentW;
        return vertex;
    }
} // namespace

SW_TEST_CASE( RHIMeshVertexStreamsTest, SnormRoundsAndClamps )
{
    SW_EXPECT_EQUAL( int8{ 127 }, RHIMeshVertexStreams::packSnorm8( 1.0f ) );
    SW_EXPECT_EQUAL( int8{ -127 }, RHIMeshVertexStreams::packSnorm8( -1.0f ) );
    SW_EXPECT_EQUAL( int8{ 127 }, RHIMeshVertexStreams::packSnorm8( 5.0f ) );
    SW_EXPECT_EQUAL( int8{ 0 }, RHIMeshVertexStreams::packSnorm8( 0.001f ) );
    SW_EXPECT_EQUAL( int8{ 64 }, RHIMeshVertexStreams::packSnorm8( 0.5f ) );
    SW_EXPECT_EQUAL( int8{ -64 }, RHIMeshVertexStreams::packSnorm8( -0.5f ) );
}

SW_TEST_CASE( RHIMeshVertexStreamsTest, PackedSplitsPositionAndAttributes )
{
    const RHIVertex arrVertex[2] = { makeVertex( 1.0f, 1.0f, -1.0f, 0.5f ), makeVertex( 4.0f, -1.0f, 1.0f, 0.0f ) };
    const uint32    capacity     = 2;
    vector<uint8>   listByte( RHIMeshVertexStreams::computeBufferBytes( RHIMeshVertexFormat::Packed, capacity ) );
    const uint32    attributeAt = RHIMeshVertexStreams::computeAttributeOffset( capacity );
    RHIMeshVertexStreams::write( RHIMeshVertexFormat::Packed, &arrVertex[0], 1, 0, capacity, listByte.data() );
    RHIMeshVertexStreams::write( RHIMeshVertexFormat::Packed, &arrVertex[1], 1, 1, capacity, listByte.data() ); // 자리를 받아 이어 쓴다(LOD 단계 · 풀의 메시)

    SW_EXPECT_EQUAL( 2u * ( 12u + 32u ), static_cast<uint32>( listByte.size() ) );
    SW_EXPECT_EQUAL( 24u, attributeAt );
    const float32* pPosition = reinterpret_cast<const float32*>( listByte.data() );
    SW_EXPECT_NEAR( 4.0f, pPosition[3], 0.0f ); // 두 번째 정점의 x — 위치는 12 B 씩 붙어 있다
    const RHIPackedMeshAttributes* pAttributes = reinterpret_cast<const RHIPackedMeshAttributes*>( listByte.data() + attributeAt );
    SW_EXPECT_EQUAL( int8{ 127 }, pAttributes[0]._arrNormal[1] );
    SW_EXPECT_EQUAL( int8{ -127 }, pAttributes[1]._arrNormal[1] );
    SW_EXPECT_EQUAL( int8{ -127 }, pAttributes[0]._arrTangent[3] ); // 바이탄젠트 부호
    SW_EXPECT_EQUAL( int8{ 127 }, pAttributes[1]._arrTangent[3] );
    SW_EXPECT_NEAR( 1024.5f, pAttributes[0]._arrUv[0], 0.0f );
    SW_EXPECT_NEAR( 16.0f, pAttributes[0]._arrColor[0], 0.0f );
}

SW_TEST_CASE( RHIMeshVertexStreamsTest, FullKeepsDataCarriedInTangent )
{
    const RHIVertex vertex = makeVertex( 1.0f, 1.0f, 1.0f, 4096.0f ); // 지형 형태 변환이 탄젠트에 싣는 경계 좌표
    vector<uint8>   listByte( RHIMeshVertexStreams::computeBufferBytes( RHIMeshVertexFormat::HighPrecision, 1 ) );
    RHIMeshVertexStreams::write( RHIMeshVertexFormat::HighPrecision, &vertex, 1, 0, 1, listByte.data() );

    SW_EXPECT_EQUAL( 12u + 52u, static_cast<uint32>( listByte.size() ) );
    const RHIHighPrecisionMeshAttributes* pAttributes = reinterpret_cast<const RHIHighPrecisionMeshAttributes*>( listByte.data() + 12 );
    SW_EXPECT_NEAR( 4096.0f, pAttributes->_arrTangent[0], 0.0f );
    SW_EXPECT_NEAR( 1.0f, pAttributes->_arrNormal[1], 0.0f );
    SW_EXPECT_NEAR( 16.0f, pAttributes->_arrColor[0], 0.0f );
}
