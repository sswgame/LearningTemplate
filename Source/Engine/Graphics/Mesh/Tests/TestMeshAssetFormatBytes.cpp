// `.mesh` 바이트는 신뢰 경계다 — 어느 길이로 잘려도 · 어느 개수를 꾸며도 · NaN 을 넣어도 할당 전에 거절하고 반쯤 읽은 데이터를 남기지 않는다(nogpu).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

#include <limits>

using namespace sw;

namespace
{
    struct XTestUtil
    {
        // 바이트 자리: 매직(0) · 버전(4) · 정점 수(8) · 인덱스 수(12) · 정점 크기(16) · 경계 반지름(20) · 뼈 수(24) · UV 경계(28) · 정점(44).
        static constexpr size_t kOffsetVertexCount = 8;
        static constexpr size_t kOffsetIndexCount  = 12;
        static constexpr size_t kOffsetRadius      = 20;
        static constexpr size_t kOffsetUVBounds    = 28;
        static constexpr uint32 kVertexCount       = 4;
        static constexpr uint32 kIndexCount        = 6;
        /** @brief 스킨 없는 메시에서 첫 덩어리가 시작하는 자리입니다. */
        static constexpr size_t kFirstChunkOffset = MeshAssetFormat::kHeaderSize + kVertexCount * MeshAssetFormat::kVertexSize + kIndexCount * sizeof( uint32 );
        /** @brief 덩어리 내용의 첫 칸(개수) 자리입니다(이름 4 · 길이 4 뒤). */
        static constexpr size_t kFirstChunkCountOffset = kFirstChunkOffset + 8;

        static MeshAssetData makeQuad()
        {
            MeshAssetData data{};
            data._listVertex.resize( kVertexCount );
            const float32 arrPosition[kVertexCount][2] = {
                {0.0f, 0.0f},
                {1.0f, 0.0f},
                {1.0f, 1.0f},
                {0.0f, 1.0f}
            };
            for ( uint32 index = 0; index < kVertexCount; ++index )
            {
                data._listVertex[index]._arrPosition[0] = arrPosition[index][0];
                data._listVertex[index]._arrPosition[1] = arrPosition[index][1];
                data._listVertex[index]._arrNormal[2]   = 1.0f;
                data._listVertex[index]._arrUV[0]       = arrPosition[index][0];
                data._listVertex[index]._arrColor[3]    = 1.0f;
            }
            data._listIndex = vector<uint32>{ 0, 1, 2, 0, 2, 3 };
            return data;
        }

        static void addSection( MeshAssetData& inoutData ) { inoutData._listSection.push_back( MeshSection{ hashed_string( "Left" ), 0, 3, MeshPersonVisibility::Both } ); }

        static void addMorph( MeshAssetData& inoutData )
        {
            MeshMorphTarget target{};
            target._name = hashed_string( "Smile" );
            MeshMorphDelta delta{};
            delta._vertexIndex = 1;
            delta._position    = float3{ 0.5f, 0.0f, 0.0f };
            target._listDelta.push_back( delta );
            inoutData._listMorphTarget.push_back( target );
        }

        static void addLOD( MeshAssetData& inoutData )
        {
            MeshLODLevel level{};
            level._screenSize = 0.25f;
            level._listVertex = inoutData._listVertex;
            level._listIndex  = inoutData._listIndex;
            inoutData._listLOD.push_back( level );
        }

        static vector<uint8> makeBytes( const MeshAssetData& data )
        {
            vector<uint8> bytes;
            MeshAssetFormat::makeBytes( data, bytes );
            return bytes;
        }

        template <typename T>
        static void patch( vector<uint8>& inoutBytes, size_t offset, const T& value )
        {
            Memory::copy( inoutBytes.data() + offset, &value, sizeof( T ) );
        }

        /** @brief 읽기가 지고 내용을 비워야 합니다. */
        static void expectRefused( const vector<uint8>& bytes )
        {
            MeshAssetData read = makeQuad();
            SW_EXPECT_FALSE( MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
            SW_EXPECT_TRUE( read._listVertex.empty() );
            SW_EXPECT_TRUE( read._listIndex.empty() );
            SW_EXPECT_TRUE( read._listSection.empty() );
        }
    };
} // namespace

/** @brief [MeshAssetFormatBytesTest] 구간의 날 이름(원본 머티리얼 · 메시)이 `SSRC` 덩어리로 왕복하고, 구간 수가 다른 덩어리는 깨진 파일이다 */
SW_TEST_CASE( MeshAssetFormatBytesTest, SectionSourceNamesRoundTrip )
{
    MeshAssetData data = XTestUtil::makeQuad();
    data._listSection.push_back( MeshSection{ hashed_string( "wood" ), 0, 3, MeshPersonVisibility::Both, hashed_string( "Wood" ), hashed_string( "Table" ) } );
    data._listSection.push_back( MeshSection{ hashed_string( "stone" ), 3, 3, MeshPersonVisibility::Both } );
    const vector<uint8> bytes = XTestUtil::makeBytes( data );
    MeshAssetData       read{};
    SW_REQUIRE_TRUE( MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, read._listSection.size() );
    SW_EXPECT_TRUE( read._listSection[0]._sourceMaterial == hashed_string( "Wood" ) );
    SW_EXPECT_TRUE( read._listSection[0]._sourceNode == hashed_string( "Table" ) );
    SW_EXPECT_TRUE( read._listSection[1].hasSourceNames() == false );
}

/** @brief [MeshAssetFormatBytesTest] 모든 덩어리가 왕복한다 — 읽기를 공통 도구로 옮겨도 형식은 그대로다 */
SW_TEST_CASE( MeshAssetFormatBytesTest, EveryChunkRoundTrips )
{
    MeshAssetData data = XTestUtil::makeQuad();
    XTestUtil::addSection( data );
    XTestUtil::addMorph( data );
    XTestUtil::addLOD( data );
    data._skinBinding._listInverseBind.resize( 2 );
    data._listSkinVertex.resize( XTestUtil::kVertexCount );
    data._listSkinVertex[3]._arrJoint[0] = 1;
    data._listLOD[0]._listSkinVertex.resize( XTestUtil::kVertexCount );
    const vector<uint8> bytes = XTestUtil::makeBytes( data );

    MeshAssetData read{};
    float32       boundingRadius = 0.0f;
    SW_REQUIRE_TRUE( MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read, &boundingRadius ) );
    SW_EXPECT_NEAR( MathUtil::sqrt( 2.0f ), boundingRadius, 1.0e-5f );
    SW_REQUIRE_EQUAL( size_t{ XTestUtil::kVertexCount }, read._listVertex.size() );
    SW_EXPECT_NEAR( 1.0f, read._listVertex[2]._arrPosition[1], 0.0f );
    SW_EXPECT_NEAR( 1.0f, read._listVertex[2]._arrUV[0], 1.0e-4f );
    SW_EXPECT_TRUE( read._listIndex == data._listIndex );
    SW_REQUIRE_EQUAL( size_t{ XTestUtil::kVertexCount }, read._listSkinVertex.size() );
    SW_EXPECT_EQUAL( uint16{ 1 }, read._listSkinVertex[3]._arrJoint[0] );
    SW_REQUIRE_EQUAL( size_t{ 1 }, read._listSection.size() );
    SW_EXPECT_TRUE( read._listSection[0]._name == hashed_string( "Left" ) );
    SW_EXPECT_TRUE( read._listSection[0]._sourceMaterial.empty() ); // 날 이름이 없으면 `SSRC` 덩어리를 쓰지 않는다(옛 파일과 같은 바이트)
    SW_REQUIRE_EQUAL( size_t{ 1 }, read._listMorphTarget.size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, read._listMorphTarget[0]._listDelta.size() );
    SW_EXPECT_NEAR( 0.5f, read._listMorphTarget[0]._listDelta[0]._position._x, 0.0f );
    SW_REQUIRE_EQUAL( size_t{ 1 }, read._listLOD.size() );
    SW_EXPECT_NEAR( 0.25f, read._listLOD[0]._screenSize, 0.0f );
    SW_EXPECT_EQUAL( size_t{ XTestUtil::kVertexCount }, read._listLOD[0]._listSkinVertex.size() );
}

/** @brief [MeshAssetFormatBytesTest] 어느 길이로 잘려도 멈추지 않고 거절한다. 덩어리는 선택이라 덩어리 경계에서 끊긴 것만 읽힌다 */
SW_TEST_CASE( MeshAssetFormatBytesTest, EveryTruncatedLengthIsRefused )
{
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetShorterHeader" );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetDeclaresVertices" );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetTruncatedChunk" );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetUnknownTruncated" );

    MeshAssetData data = XTestUtil::makeQuad();
    XTestUtil::addSection( data );
    XTestUtil::addLOD( data );
    const vector<uint8> bytes = XTestUtil::makeBytes( data );

    uint32 readCount = 0;
    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        MeshAssetData read{};
        if ( MeshAssetFormat::readFromBytes( bytes.data(), length, read ) )
        {
            ++readCount;
            SW_EXPECT_EQUAL( size_t{ XTestUtil::kVertexCount }, read._listVertex.size() );
            continue;
        }
        SW_EXPECT_TRUE( read._listVertex.empty() );
    }
    SW_EXPECT_EQUAL( 2u, readCount ); // 덩어리 없음 · 구간만
}

/** @brief [MeshAssetFormatBytesTest] 머리와 덩어리의 꾸민 개수는 할당 전에 거절한다(잡으면 이 테스트가 메모리에서 멈춘다) */
SW_TEST_CASE( MeshAssetFormatBytesTest, CraftedCountsAreRefused )
{
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetDeclaresVertices" );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetMalformedSection" );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetMalformedMorph" );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetMalformedLOD" );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetUnknownTruncated" );
    constexpr uint32 kHuge = 0xFFFFFFFFu; // 3 의 배수라 삼각형 검사를 지난다

    const vector<uint8> plain = XTestUtil::makeBytes( XTestUtil::makeQuad() );
    for ( const size_t offset : { XTestUtil::kOffsetVertexCount, XTestUtil::kOffsetIndexCount } )
    {
        vector<uint8> crafted = plain;
        XTestUtil::patch( crafted, offset, kHuge );
        XTestUtil::expectRefused( crafted );
    }

    MeshAssetData withSection = XTestUtil::makeQuad();
    XTestUtil::addSection( withSection );
    vector<uint8> sectionCount = XTestUtil::makeBytes( withSection );
    XTestUtil::patch( sectionCount, XTestUtil::kFirstChunkCountOffset, kHuge );
    XTestUtil::expectRefused( sectionCount );

    vector<uint8> sectionNameLength = XTestUtil::makeBytes( withSection );
    XTestUtil::patch( sectionNameLength, XTestUtil::kFirstChunkCountOffset + sizeof( uint32 ), kHuge );
    XTestUtil::expectRefused( sectionNameLength );

    vector<uint8> chunkLength = XTestUtil::makeBytes( withSection );
    XTestUtil::patch( chunkLength, XTestUtil::kFirstChunkOffset + sizeof( uint32 ), kHuge );
    XTestUtil::expectRefused( chunkLength );

    MeshAssetData withMorph = XTestUtil::makeQuad();
    XTestUtil::addMorph( withMorph );
    vector<uint8> morphCount = XTestUtil::makeBytes( withMorph );
    XTestUtil::patch( morphCount, XTestUtil::kFirstChunkCountOffset, kHuge );
    XTestUtil::expectRefused( morphCount );

    // 대상 수 · 이름 길이 · 이름 "Smile" 뒤가 차이 수다.
    vector<uint8> deltaCount = XTestUtil::makeBytes( withMorph );
    XTestUtil::patch( deltaCount, XTestUtil::kFirstChunkCountOffset + sizeof( uint32 ) * 2 + 5, kHuge );
    XTestUtil::expectRefused( deltaCount );

    MeshAssetData withLOD = XTestUtil::makeQuad();
    XTestUtil::addLOD( withLOD );
    vector<uint8> levelCount = XTestUtil::makeBytes( withLOD );
    XTestUtil::patch( levelCount, XTestUtil::kFirstChunkCountOffset, kHuge );
    XTestUtil::expectRefused( levelCount );

    // 단계 수 · 임계값 뒤가 단계의 정점 수다.
    vector<uint8> levelVertexCount = XTestUtil::makeBytes( withLOD );
    XTestUtil::patch( levelVertexCount, XTestUtil::kFirstChunkCountOffset + sizeof( uint32 ) * 2, kHuge );
    XTestUtil::expectRefused( levelVertexCount );
}

/** @brief [MeshAssetFormatBytesTest] 같은 덩어리가 두 번 오면 앞의 내용을 조용히 덮거나 잇지 않고 거절한다 */
SW_TEST_CASE( MeshAssetFormatBytesTest, RepeatedChunkIsRefused )
{
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetMalformedSection" );
    MeshAssetData data = XTestUtil::makeQuad();
    XTestUtil::addSection( data );
    vector<uint8>       bytes = XTestUtil::makeBytes( data );
    const vector<uint8> chunk( bytes.begin() + static_cast<ptrdiff_t>( XTestUtil::kFirstChunkOffset ), bytes.end() );
    bytes.insert( bytes.end(), chunk.begin(), chunk.end() );
    XTestUtil::expectRefused( bytes );
}

/** @brief [MeshAssetFormatBytesTest] NaN · 무한대는 어느 칸에서든 거절한다 — 위치 · 경계 반지름 · UV 경계 */
SW_TEST_CASE( MeshAssetFormatBytesTest, NonFiniteValuesAreRefused )
{
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetNonFinite" );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetInvalidBounding" );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetInvalidUV" );
    const float32       kNaN  = std::numeric_limits<float32>::quiet_NaN();
    const vector<uint8> plain = XTestUtil::makeBytes( XTestUtil::makeQuad() );
    for ( const size_t offset : { size_t{ XTestUtil::kOffsetRadius }, size_t{ XTestUtil::kOffsetUVBounds }, size_t{ MeshAssetFormat::kHeaderSize } } )
    {
        vector<uint8> crafted = plain;
        XTestUtil::patch( crafted, offset, kNaN );
        XTestUtil::expectRefused( crafted );
    }
}
