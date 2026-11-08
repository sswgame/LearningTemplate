#include "pch.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Math/OctahedralNormal.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/Serialization/LittleEndianUtil.h"

#include "Engine/Content/AssetIO.h"

namespace sw
{
    namespace
    {
        /**
         * @struct MeshAssetFormatInternal
         * @brief 리틀 엔디언 바이트 읽기 · 쓰기입니다. 호스트 바이트 순서에 기대지 않도록 값을 바이트로 나눠 씁니다.
         */
        struct MeshAssetFormatInternal
        {
            /** @brief 파일 머리의 매직(`SWMS`)입니다. */
            static constexpr uint8 kArrMagic[4] = { 'S', 'W', 'M', 'S' };
            /** @brief 정점 플래그의 비트 0 입니다 — 바이탄젠트 부호가 -1 이면 켭니다(탄젠트 w). 나머지 비트는 0 이어야 합니다. */
            static constexpr uint32 kFlagBitangentNegative = 1u;
            /** @brief UV 경계(`_min` · `_max` 의 u · v)입니다 — 머리에 싣고 정점의 `unorm16x2` UV 를 이 사각형 안으로 정규화합니다. */
            struct UVBounds
            {
                float32 _arrMin[2]{};
                float32 _arrMax[2]{};
            };

            /** @brief 정점 목록들의 UV 를 모두 덮는 경계입니다. 목록이 모두 비었으면 0 입니다. */
            static UVBounds computeUVBounds( const vector<RHIVertex>& listFirst, const vector<MeshLODLevel>& listLevel )
            {
                UVBounds   bounds{};
                bool       bAny    = false;
                const auto include = [&]( const vector<RHIVertex>& listVertex )
                {
                    for ( const RHIVertex& vertex : listVertex )
                    {
                        for ( uint32 axis = 0; axis < 2; ++axis )
                        {
                            const float32 value = vertex._arrUV[axis];
                            if ( MathUtil::isFinite( value ) == false )
                                continue;
                            bounds._arrMin[axis] = bAny ? MathUtil::min( bounds._arrMin[axis], value ) : value;
                            bounds._arrMax[axis] = bAny ? MathUtil::max( bounds._arrMax[axis], value ) : value;
                        }
                        bAny = true;
                    }
                };
                include( listFirst );
                for ( const MeshLODLevel& level : listLevel )
                {
                    include( level._listVertex );
                }
                return bounds;
            }

            /** @brief UV 경계를 머리 자리에 씁니다(최소 u · v, 최대 u · v). */
            static void appendUVBounds( vector<uint8>& outBytes, const UVBounds& bounds )
            {
                LittleEndianUtil::appendFloat32( outBytes, bounds._arrMin[0] );
                LittleEndianUtil::appendFloat32( outBytes, bounds._arrMin[1] );
                LittleEndianUtil::appendFloat32( outBytes, bounds._arrMax[0] );
                LittleEndianUtil::appendFloat32( outBytes, bounds._arrMax[1] );
            }

            /** @brief UV 경계를 머리에서 읽습니다. 유한하지 않거나 최대가 최소보다 작으면 false 입니다. */
            [[nodiscard]] static bool readUVBounds( ByteReader& reader, UVBounds& outBounds )
            {
                outBounds._arrMin[0] = reader.readFloat32();
                outBounds._arrMin[1] = reader.readFloat32();
                outBounds._arrMax[0] = reader.readFloat32();
                outBounds._arrMax[1] = reader.readFloat32();
                return reader.hasOverflowed() == false && outBounds._arrMin[0] <= outBounds._arrMax[0] && outBounds._arrMin[1] <= outBounds._arrMax[1];
            }

            /** @brief UV 한 성분을 경계 안의 [0, 1] 로 옮겨 `unorm16` 으로 담습니다. 경계 폭이 0 이면 0 입니다. */
            static uint16 packUV( float32 value, float32 minValue, float32 maxValue )
            {
                const float32 extent = maxValue - minValue;
                return unorm16::fromFloat( extent > 0.0f ? ( value - minValue ) / extent : 0.0f ).getRaw();
            }

            /** @brief `unorm16` UV 한 성분을 경계 안의 값으로 풉니다. */
            static float32 unpackUV( uint16 raw, float32 minValue, float32 maxValue )
            {
                return minValue + unorm16::fromRaw( raw ).toFloat() * ( maxValue - minValue );
            }

            /** @brief 옥타헤드럴 좌표를 쓰기 위해 `snorm16` 두 개를 리틀 엔디언으로 붙입니다. */
            static void appendOctahedral( vector<uint8>& outBytes, const float3& direction )
            {
                const OctahedralNormal packed = OctahedralNormal::fromNormal( direction );
                LittleEndianUtil::appendUint16( outBytes, static_cast<uint16>( packed._u.getRaw() ) );
                LittleEndianUtil::appendUint16( outBytes, static_cast<uint16>( packed._v.getRaw() ) );
            }

            /** @brief `appendOctahedral` 로 쓴 4 바이트를 단위 벡터로 풉니다. */
            static float3 readOctahedral( ByteReader& reader )
            {
                const int16            u = static_cast<int16>( reader.readUint16() );
                const int16            v = static_cast<int16>( reader.readUint16() );
                const OctahedralNormal packed{ snorm16::fromRaw( u ), snorm16::fromRaw( v ) };
                return packed.toNormal();
            }
            /** @brief 모프 타깃 덩어리의 이름입니다. */
            static constexpr uint8 kArrMorphChunk[4] = { 'M', 'R', 'P', 'H' };
            /** @brief 모프 차이 하나의 바이트 수입니다(정점 번호 uint32 + float32 여섯). */
            static constexpr uint32 kMorphDeltaSize = 4 + 6 * 4;
            /** @brief 구간 덩어리의 이름입니다. */
            static constexpr uint8 kArrSectionChunk[4] = { 'S', 'E', 'C', 'T' };
            /** @brief 구간 날 이름 덩어리의 이름입니다(`SECT` 뒤 — 구간마다 원본 머티리얼 · 메시 이름). */
            static constexpr uint8 kArrSectionSourceChunk[4] = { 'S', 'S', 'R', 'C' };
            /** @brief 낮은 상세 단계 덩어리의 이름입니다. */
            static constexpr uint8 kArrLODChunk[4] = { 'L', 'O', 'D', 'S' };
            /** @brief 스킨드 메시의 낮은 상세 단계 스킨 덩어리의 이름입니다(`LODS` 덩어리 뒤에 온다). */
            static constexpr uint8 kArrLODSkinChunk[4] = { 'L', 'S', 'K', 'N' };

            /**
             * @brief 정점 배열을 씁니다. 탄젠트는 받은 값이 아니라 기준(`Mesh::computeTangents`)으로 구해 싣는다 — 파일의 탄젠트가 런타임 메시와 늘 같다.
             * @details LOD 단계는 단계마다 따로 부른다 — 탄젠트를 묶는 이웃은 같은 단계의 삼각형뿐이다.
             */
            static void appendVertices( vector<uint8>& outBytes, const vector<RHIVertex>& listSource, const vector<uint32>& listIndex, const UVBounds& uvBounds )
            {
                vector<RHIVertex> listVertex = listSource;
                Mesh::computeTangents( listVertex, listIndex );
                // 정점 하나(32 바이트): 위치 float32 3 · 노멀 옥타헤드럴 4 · 탄젠트 옥타헤드럴 4 · UV unorm16x2 4 · 색 unorm8x4 4 · 플래그 uint32 4.
                for ( const RHIVertex& vertex : listVertex )
                {
                    for ( const float32 value : vertex._arrPosition )
                    {
                        LittleEndianUtil::appendFloat32( outBytes, value );
                    }
                    appendOctahedral( outBytes, float3{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] } );
                    appendOctahedral( outBytes, float3{ vertex._arrTangent[0], vertex._arrTangent[1], vertex._arrTangent[2] } );
                    for ( uint32 axis = 0; axis < 2; ++axis )
                    {
                        LittleEndianUtil::appendUint16( outBytes, packUV( vertex._arrUV[axis], uvBounds._arrMin[axis], uvBounds._arrMax[axis] ) );
                    }
                    for ( const float32 value : vertex._arrColor )
                    {
                        outBytes.push_back( unorm8::fromFloat( value ).getRaw() );
                    }
                    LittleEndianUtil::appendUint32( outBytes, vertex._arrTangent[3] < 0.0f ? kFlagBitangentNegative : 0u );
                }
            }

            /** @brief 인덱스(uint32)를 씁니다. */
            static void appendIndices( vector<uint8>& outBytes, const vector<uint32>& listIndex )
            {
                for ( const uint32 index : listIndex )
                {
                    LittleEndianUtil::appendUint32( outBytes, index );
                }
            }

            /** @brief 인덱스 @p indexCount 개를 읽습니다. 삼각형이 아니거나 번호가 @p vertexCount 를 넘으면 false 입니다. */
            [[nodiscard]] static bool readIndices( ByteReader& reader, uint32 indexCount, uint32 vertexCount, vector<uint32>& outListIndex )
            {
                if ( indexCount == 0 || indexCount % 3 != 0 )
                    return false;
                return reader.readList( outListIndex, indexCount, sizeof( uint32 ), [&]( uint32& index ) -> bool
                {
                    index = reader.readUint32();
                    return index < vertexCount;
                } );
            }

            /** @brief 낮은 상세 단계 덩어리(`LODS`)를 씁니다 — 단계 수, 단계마다 임계값 · 정점 수 · 인덱스 수 · 정점 · 인덱스. 단계가 없으면 쓰지 않습니다. */
            static void appendLODChunk( vector<uint8>& outBytes, const vector<MeshLODLevel>& listLevel, const UVBounds& uvBounds )
            {
                if ( listLevel.empty() )
                    return;
                const size_t lengthOffset = beginChunk( outBytes, kArrLODChunk );
                LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( listLevel.size() ) );
                for ( const MeshLODLevel& level : listLevel )
                {
                    LittleEndianUtil::appendFloat32( outBytes, level._screenSize );
                    LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( level._listVertex.size() ) );
                    LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( level._listIndex.size() ) );
                    appendVertices( outBytes, level._listVertex, level._listIndex, uvBounds );
                    appendIndices( outBytes, level._listIndex );
                }
                finishChunk( outBytes, lengthOffset );
            }

            /**
             * @brief 낮은 상세 단계 덩어리 내용을 읽습니다. 길이 · 탄젠트가 맞지 않으면 false 입니다.
             * @details 정점 수와 임계값의 뜻(삼각형 · 내려가는 임계값 · 스킨 없는 메시)은 메시가 다시 본다(`Mesh::setLODs`) — 규칙은 한 곳에 둔다.
             */
            [[nodiscard]] static bool readLODChunk( ByteReader& chunk, const UVBounds& uvBounds, vector<MeshLODLevel>& outListLevel )
            {
                const bool bRead = chunk.readCountedList( outListLevel, sizeof( uint32 ) * 3, [&]( MeshLODLevel& level ) -> bool
                {
                    level._screenSize        = chunk.readFloat32();
                    const uint32 vertexCount = chunk.readUint32();
                    const uint32 indexCount  = chunk.readUint32();
                    return readVertices( chunk, vertexCount, uvBounds, level._listVertex ) && readIndices( chunk, indexCount, vertexCount, level._listIndex );
                } );
                return bRead && chunk.isFullyRead();
            }

            /** @brief 스킨 정점을 씁니다(뼈 번호 uint16 넷 + 가중치 float32 넷 — `MeshAssetFormat::kSkinVertexSize`). */
            static void appendSkinVertices( vector<uint8>& outBytes, const vector<MeshSkinVertex>& listSkin )
            {
                for ( const MeshSkinVertex& skin : listSkin )
                {
                    for ( const uint16 joint : skin._arrJoint )
                    {
                        LittleEndianUtil::appendUint16( outBytes, joint );
                    }
                    for ( const float32 weight : skin._arrWeight )
                    {
                        LittleEndianUtil::appendFloat32( outBytes, weight );
                    }
                }
            }

            /** @brief 낮은 상세 단계의 스킨 덩어리(`LSKN`)를 씁니다 — 단계 수, 단계마다 정점 수 · 스킨 정점. 스킨 있는 단계가 없으면 쓰지 않습니다(스킨 없는 메시는 이전 파일과 바이트까지 같다). */
            static void appendLODSkinChunk( vector<uint8>& outBytes, const vector<MeshLODLevel>& listLevel )
            {
                bool bAnySkin = false;
                for ( const MeshLODLevel& level : listLevel )
                {
                    bAnySkin = bAnySkin || level._listSkinVertex.empty() == false;
                }
                if ( bAnySkin == false )
                    return;
                const size_t lengthOffset = beginChunk( outBytes, kArrLODSkinChunk );
                LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( listLevel.size() ) );
                for ( const MeshLODLevel& level : listLevel )
                {
                    LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( level._listSkinVertex.size() ) );
                    appendSkinVertices( outBytes, level._listSkinVertex );
                }
                finishChunk( outBytes, lengthOffset );
            }

            /**
             * @brief 낮은 상세 단계의 스킨 덩어리 내용을 읽어 이미 읽은 단계(`LODS`)에 붙입니다. 단계 수 · 정점 수가 맞지 않거나 뼈 번호가 @p skinBoneCount 를 넘으면 false 입니다.
             */
            [[nodiscard]] static bool readLODSkinChunk( ByteReader& chunk, uint32 skinBoneCount, vector<MeshLODLevel>& ioListLevel )
            {
                const uint32 levelCount = chunk.readUint32();
                if ( chunk.hasOverflowed() || skinBoneCount == 0 || levelCount != ioListLevel.size() )
                    return false;
                for ( MeshLODLevel& level : ioListLevel )
                {
                    const uint32 vertexCount = chunk.readUint32();
                    if ( chunk.hasOverflowed() || vertexCount != level._listVertex.size() )
                        return false;
                    if ( readSkinVertices( chunk, vertexCount, skinBoneCount, level._listSkinVertex ) == false )
                        return false;
                }
                return chunk.isFullyRead();
            }

            /** @brief 덩어리 이름 넷과 길이 자리를 쓰고, 길이 자리의 위치를 돌려줍니다(내용을 쓴 뒤 `finishChunk` 가 채운다). */
            static size_t beginChunk( vector<uint8>& outBytes, const uint8 ( &arrName )[4] )
            {
                for ( const uint8 letter : arrName )
                {
                    outBytes.push_back( letter );
                }
                const size_t lengthOffset = outBytes.size();
                LittleEndianUtil::appendUint32( outBytes, 0u );
                return lengthOffset;
            }

            static void finishChunk( vector<uint8>& outBytes, size_t lengthOffset )
            {
                const uint32 contentLength = static_cast<uint32>( outBytes.size() - lengthOffset - 4 );
                for ( uint32 byteIndex = 0; byteIndex < 4; ++byteIndex )
                {
                    outBytes[lengthOffset + byteIndex] = static_cast<uint8>( ( contentLength >> ( byteIndex * 8 ) ) & 0xFFu );
                }
            }

            /** @brief 구간 덩어리(`SECT`)를 씁니다 — 구간 수, 구간마다 이름 길이 · 이름 · 첫 인덱스 · 인덱스 수 · 시점 가시성. 구간이 없으면 쓰지 않습니다. */
            static void appendSectionChunk( vector<uint8>& outBytes, const vector<MeshSection>& listSection )
            {
                if ( listSection.empty() )
                    return;
                const size_t lengthOffset = beginChunk( outBytes, kArrSectionChunk );
                LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( listSection.size() ) );
                for ( const MeshSection& section : listSection )
                {
                    const string_view name = section._name.c_str();
                    LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( name.size() ) );
                    outBytes.insert( outBytes.end(), reinterpret_cast<const uint8*>( name.data() ), reinterpret_cast<const uint8*>( name.data() ) + name.size() );
                    LittleEndianUtil::appendUint32( outBytes, section._firstIndex );
                    LittleEndianUtil::appendUint32( outBytes, section._indexCount );
                    LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( section._personVisibility ) );
                }
                finishChunk( outBytes, lengthOffset );
            }

            static void appendString( vector<uint8>& outBytes, string_view text )
            {
                LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( text.size() ) );
                outBytes.insert( outBytes.end(), reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
            }

            /**
             * @brief 구간 날 이름 덩어리(`SSRC`)를 씁니다 — 구간 수, 구간마다 머티리얼 이름 · 메시 이름. 날 이름이 있는 구간이 없으면 쓰지 않습니다(옛 파일과 같은 바이트).
             * @details 덩어리를 따로 두어 판을 올리지 않는다 — 옛 `.mesh` 는 이 덩어리 없이 그대로 읽힌다.
             */
            static void appendSectionSourceChunk( vector<uint8>& outBytes, const vector<MeshSection>& listSection )
            {
                const bool bAnySource = VectorUtil::containsIf( listSection, []( const MeshSection& section )
                { return section.hasSourceNames(); } );
                if ( bAnySource == false )
                    return;
                const size_t lengthOffset = beginChunk( outBytes, kArrSectionSourceChunk );
                LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( listSection.size() ) );
                for ( const MeshSection& section : listSection )
                {
                    appendString( outBytes, section._sourceMaterial.c_str() );
                    appendString( outBytes, section._sourceNode.c_str() );
                }
                finishChunk( outBytes, lengthOffset );
            }

            /** @brief 구간 날 이름 덩어리를 읽습니다. 구간 덩어리 뒤에 와야 하고 구간 수가 같아야 합니다. */
            [[nodiscard]] static bool readSectionSourceChunk( ByteReader& chunk, vector<MeshSection>& inoutListSection )
            {
                if ( inoutListSection.empty() || chunk.readUint32() != static_cast<uint32>( inoutListSection.size() ) )
                    return false;
                for ( MeshSection& section : inoutListSection )
                {
                    string_view material;
                    string_view node;
                    if ( chunk.readStringView( material ) == false || chunk.readStringView( node ) == false )
                        return false;
                    section._sourceMaterial = hashed_string( string( material ) );
                    section._sourceNode     = hashed_string( string( node ) );
                }
                return chunk.isFullyRead();
            }

            /** @brief 구간 덩어리 내용을 읽습니다. 길이 · 범위 · 가시성 값이 맞지 않으면 false 입니다(범위와 이름 중복은 `Mesh::setSections` 가 다시 본다). */
            [[nodiscard]] static bool readSectionChunk( ByteReader& chunk, uint32 indexCount, vector<MeshSection>& outListSection )
            {
                const bool bRead = chunk.readCountedList( outListSection, sizeof( uint32 ) * 4, [&]( MeshSection& section ) -> bool
                {
                    string_view name;
                    if ( chunk.readStringView( name ) == false )
                        return false;
                    section._name              = hashed_string( string( name ) );
                    section._firstIndex        = chunk.readUint32();
                    section._indexCount        = chunk.readUint32();
                    const uint32 visibility    = chunk.readUint32();
                    const bool   bValidSection = visibility < static_cast<uint32>( MeshPersonVisibility::Count ) && section._firstIndex <= indexCount &&
                                               section._indexCount <= indexCount - section._firstIndex;
                    if ( bValidSection == false )
                        return false;
                    section._personVisibility = static_cast<MeshPersonVisibility>( visibility );
                    return true;
                } );
                return bRead && chunk.isFullyRead();
            }

            // 파일의 정점은 압축 배치이고 메모리의 `RHIVertex`(float32 16 개)와 크기가 다르다 — 배치를 바꾸면 `kVersion` 을 올리고 모델을 다시 임포트한다.
            static_assert( MeshAssetFormat::kVertexSize == 3 * sizeof( float32 ) + 2 * sizeof( OctahedralNormal ) + sizeof( unorm16x2 ) + sizeof( unorm8x4 ) + sizeof( uint32 ),
                           "kVertexSize must match the packed layout (position, normal, tangent, uv, color, flags)" );
            static_assert( MeshAssetFormat::kHeaderSize == 28 + sizeof( UVBounds ), "kHeaderSize must hold the fixed fields and the UV bounds" );
            static_assert( MeshAssetFormat::kSkinVertexSize == 4 * sizeof( uint16 ) + 4 * sizeof( float32 ), "kSkinVertexSize must match the skin layout" );

            /** @brief 원점에서 가장 먼 정점까지의 거리입니다(`Mesh::getBoundingRadius` 와 같은 정의). */
            static float32 computeBoundingRadius( const vector<RHIVertex>& listVertex )
            {
                float32 maxLengthSquared = 0.0f;
                for ( const RHIVertex& vertex : listVertex )
                {
                    const float32 lengthSquared = vertex._arrPosition[0] * vertex._arrPosition[0] + vertex._arrPosition[1] * vertex._arrPosition[1] +
                                                  vertex._arrPosition[2] * vertex._arrPosition[2];
                    maxLengthSquared = MathUtil::max( maxLengthSquared, lengthSquared );
                }
                return MathUtil::sqrt( maxLengthSquared );
            }

            /**
             * @brief 정점 @p vertexCount 개를 읽어 풉니다. 잘렸거나 유한하지 않은 위치가 있거나 플래그에 모르는 비트가 있으면 false 입니다.
             * @details 법선 · 탄젠트는 옥타헤드럴을 풀어 단위 길이이고 탄젠트 w 는 플래그에서 ±1 로 복원한다.
             */
            [[nodiscard]] static bool readVertices( ByteReader& reader, uint32 vertexCount, const UVBounds& uvBounds, vector<RHIVertex>& outListVertex )
            {
                return reader.readList( outListVertex, vertexCount, MeshAssetFormat::kVertexSize, [&]( RHIVertex& vertex ) -> bool
                {
                    for ( float32& value : vertex._arrPosition )
                    {
                        value = reader.readFloat32();
                    }
                    const float3 normal  = readOctahedral( reader );
                    const float3 tangent = readOctahedral( reader );
                    vertex._arrNormal[0] = normal._x;
                    vertex._arrNormal[1] = normal._y;
                    vertex._arrNormal[2] = normal._z;
                    for ( uint32 axis = 0; axis < 2; ++axis )
                    {
                        vertex._arrUV[axis] = unpackUV( reader.readUint16(), uvBounds._arrMin[axis], uvBounds._arrMax[axis] );
                    }
                    for ( float32& value : vertex._arrColor )
                    {
                        value = unorm8::fromRaw( reader.readUint8() ).toFloat();
                    }
                    const uint32 flags = reader.readUint32();
                    if ( ( flags & ~kFlagBitangentNegative ) != 0u )
                    {
                        SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetUnknownVertex", "Mesh asset has unknown vertex flags %# - re-import the model", flags );
                        return false;
                    }
                    vertex._arrTangent[0] = tangent._x;
                    vertex._arrTangent[1] = tangent._y;
                    vertex._arrTangent[2] = tangent._z;
                    vertex._arrTangent[3] = ( flags & kFlagBitangentNegative ) != 0u ? -1.0f : 1.0f;
                    return true;
                } );
            }

            /** @brief 본 배치 지문과 역 바인드를 씁니다. */
            static void appendSkinBinding( vector<uint8>& outBytes, const MeshSkinBinding& skinBinding )
            {
                LittleEndianUtil::appendUint64( outBytes, skinBinding._layoutHash );
                for ( const float4x4& inverseBind : skinBinding._listInverseBind )
                {
                    for ( uint32 element = 0; element < 16; ++element )
                    {
                        LittleEndianUtil::appendFloat32( outBytes, ( &inverseBind._11 )[element] );
                    }
                }
            }

            /** @brief 본 배치 지문과 역 바인드 @p boneCount 개를 읽습니다. */
            [[nodiscard]] static bool readSkinBinding( ByteReader& reader, uint32 boneCount, MeshSkinBinding& outSkinBinding )
            {
                outSkinBinding._layoutHash = reader.readUint64();
                return reader.readList( outSkinBinding._listInverseBind, boneCount, MeshAssetFormat::kInverseBindSize, [&]( float4x4& inverseBind ) -> bool
                {
                    for ( uint32 element = 0; element < 16; ++element )
                    {
                        ( &inverseBind._11 )[element] = reader.readFloat32(); // 유한하지 않은 값은 읽기가 넘침으로 거절한다(`ByteReader::readFloat32`)
                    }
                    return true;
                } );
            }

            /** @brief 스킨 정점 @p vertexCount 개를 읽습니다. 뼈 번호가 뼈 수를 넘으면 false 입니다. */
            [[nodiscard]] static bool readSkinVertices( ByteReader& reader, uint32 vertexCount, uint32 skinBoneCount, vector<MeshSkinVertex>& outListSkin )
            {
                return reader.readList( outListSkin, vertexCount, MeshAssetFormat::kSkinVertexSize, [&]( MeshSkinVertex& skin ) -> bool
                {
                    for ( uint16& joint : skin._arrJoint )
                    {
                        joint = reader.readUint16();
                        if ( joint >= skinBoneCount )
                        {
                            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetSkinNames", "Mesh asset skin names bone %# but the skeleton has %# bones", joint, skinBoneCount );
                            return false;
                        }
                    }
                    for ( float32& weight : skin._arrWeight )
                    {
                        weight = reader.readFloat32();
                    }
                    return true;
                } );
            }

            /** @brief 모프 타깃 덩어리 본문을 읽습니다. 길이 · 정점 번호가 맞지 않거나 본문을 다 쓰지 않으면 false 입니다. */
            [[nodiscard]] static bool readMorphChunk( ByteReader& chunk, uint32 vertexCount, vector<MeshMorphTarget>& outListTarget )
            {
                const bool bRead = chunk.readCountedList( outListTarget, sizeof( uint32 ) * 2, [&]( MeshMorphTarget& target ) -> bool
                {
                    string_view name;
                    if ( chunk.readStringView( name ) == false )
                        return false;
                    target._name = hashed_string( string( name ) );
                    return chunk.readCountedList( target._listDelta, kMorphDeltaSize, [&]( MeshMorphDelta& delta ) -> bool
                    {
                        delta._vertexIndex = chunk.readUint32();
                        delta._position._x = chunk.readFloat32();
                        delta._position._y = chunk.readFloat32();
                        delta._position._z = chunk.readFloat32();
                        delta._normal._x   = chunk.readFloat32();
                        delta._normal._y   = chunk.readFloat32();
                        delta._normal._z   = chunk.readFloat32();
                        return delta._vertexIndex < vertexCount;
                    } );
                } );
                return bRead && chunk.isFullyRead();
            }

            /** @brief 정점 · 스킨 뒤의 선택 덩어리를 모두 읽습니다. 모르는 이름 · 길이가 맞지 않는 덩어리 · 같은 덩어리가 두 번 오면 깨진 파일로 보고 false 입니다. */
            [[nodiscard]] static bool readOptionalChunks( ByteReader& reader, uint32 vertexCount, uint32 indexCount, const UVBounds& uvBounds, MeshAssetData& ioData )
            {
                while ( reader.getRemainingByteCount() > 0 )
                {
                    const uint8* pChunkName = nullptr;
                    if ( reader.readView( sizeof( kArrMorphChunk ), pChunkName ) == false || reader.getRemainingByteCount() < sizeof( uint32 ) )
                    {
                        SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetTruncatedChunk", "Mesh asset has a truncated chunk header" );
                        return false;
                    }
                    const bool   bMorph      = Memory::compare( pChunkName, kArrMorphChunk, 4 ) == 0;
                    const bool   bSection    = Memory::compare( pChunkName, kArrSectionChunk, 4 ) == 0;
                    const bool   bSource     = Memory::compare( pChunkName, kArrSectionSourceChunk, 4 ) == 0;
                    const bool   bLOD        = Memory::compare( pChunkName, kArrLODChunk, 4 ) == 0;
                    const bool   bLODSkin    = Memory::compare( pChunkName, kArrLODSkinChunk, 4 ) == 0;
                    const uint32 chunkLength = reader.readUint32();
                    const uint8* pChunk      = nullptr;
                    const bool   bKnownChunk = bMorph || bSection || bSource || bLOD || bLODSkin;
                    if ( bKnownChunk == false || reader.readView( chunkLength, pChunk ) == false )
                    {
                        SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetUnknownTruncated", "Mesh asset has an unknown or truncated chunk" );
                        return false;
                    }
                    ByteReader chunk{ pChunk, chunkLength };
                    if ( bLOD )
                    {
                        if ( ioData._listLOD.empty() == false || readLODChunk( chunk, uvBounds, ioData._listLOD ) == false )
                        {
                            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetMalformedLOD", "Mesh asset has a malformed LOD chunk" );
                            return false;
                        }
                        continue;
                    }
                    if ( bLODSkin )
                    {
                        if ( readLODSkinChunk( chunk, ioData._skinBinding.getBoneCount(), ioData._listLOD ) == false )
                        {
                            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetMalformedLOD2", "Mesh asset has a malformed LOD skin chunk (it must follow the LOD chunk and match its levels)" );
                            return false;
                        }
                        continue;
                    }
                    if ( bSource )
                    {
                        if ( readSectionSourceChunk( chunk, ioData._listSection ) == false )
                        {
                            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetMalformedSectionSource", "Mesh asset has a malformed section source chunk (it must follow the section chunk and match its count)" );
                            return false;
                        }
                        continue;
                    }
                    if ( bSection )
                    {
                        if ( ioData._listSection.empty() == false || readSectionChunk( chunk, indexCount, ioData._listSection ) == false )
                        {
                            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetMalformedSection", "Mesh asset has a malformed section chunk" );
                            return false;
                        }
                        continue;
                    }
                    if ( ioData._listMorphTarget.empty() == false || readMorphChunk( chunk, vertexCount, ioData._listMorphTarget ) == false )
                    {
                        SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetMalformedMorph", "Mesh asset has a malformed morph target chunk" );
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool MeshAssetFormat::isMeshAssetPath( string_view path )
    {
        return FileUtil::hasExtension( path, kExtension );
    }

    void MeshAssetFormat::makeBytes( const MeshAssetData& data, vector<uint8>& outBytes )
    {
        const bool   bSkin         = data.hasSkin();
        const uint32 skinBoneCount = bSkin ? data._skinBinding.getBoneCount() : 0u;
        const size_t skinByteCount = bSkin ? data._listSkinVertex.size() * kSkinVertexSize + sizeof( uint64 ) + skinBoneCount * kInverseBindSize : 0u;
        outBytes.clear();
        outBytes.reserve( kHeaderSize + data._listVertex.size() * kVertexSize + skinByteCount + data._listIndex.size() * sizeof( uint32 ) );
        for ( const uint8 magic : MeshAssetFormatInternal::kArrMagic )
        {
            outBytes.push_back( magic );
        }
        LittleEndianUtil::appendUint32( outBytes, kVersion );
        LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( data._listVertex.size() ) );
        LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( data._listIndex.size() ) );
        LittleEndianUtil::appendUint32( outBytes, kVertexSize );
        LittleEndianUtil::appendFloat32( outBytes, MeshAssetFormatInternal::computeBoundingRadius( data._listVertex ) );
        LittleEndianUtil::appendUint32( outBytes, skinBoneCount );
        // UV 경계는 기본 정점과 모든 LOD 를 덮는다(LOD 의 UV 가 같은 사각형으로 정규화된다).
        const MeshAssetFormatInternal::UVBounds uvBounds = MeshAssetFormatInternal::computeUVBounds( data._listVertex, data._listLOD );
        MeshAssetFormatInternal::appendUVBounds( outBytes, uvBounds );

        MeshAssetFormatInternal::appendVertices( outBytes, data._listVertex, data._listIndex, uvBounds );
        if ( bSkin )
        {
            for ( const MeshSkinVertex& skin : data._listSkinVertex )
            {
                for ( const uint16 joint : skin._arrJoint )
                {
                    LittleEndianUtil::appendUint16( outBytes, joint );
                }
                for ( const float32 weight : skin._arrWeight )
                {
                    LittleEndianUtil::appendFloat32( outBytes, weight );
                }
            }
            MeshAssetFormatInternal::appendSkinBinding( outBytes, data._skinBinding );
        }
        MeshAssetFormatInternal::appendIndices( outBytes, data._listIndex );
        // 선택 덩어리 — 모프 타깃(있을 때). 길이는 내용을 다 쓴 뒤 채운다.
        if ( data._listMorphTarget.empty() == false )
        {
            for ( const uint8 letter : MeshAssetFormatInternal::kArrMorphChunk )
            {
                outBytes.push_back( letter );
            }
            const size_t lengthOffset = outBytes.size();
            LittleEndianUtil::appendUint32( outBytes, 0u );
            const size_t contentStart = outBytes.size();
            LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( data._listMorphTarget.size() ) );
            for ( const MeshMorphTarget& target : data._listMorphTarget )
            {
                const string_view name = target._name.c_str();
                LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( name.size() ) );
                outBytes.insert( outBytes.end(), reinterpret_cast<const uint8*>( name.data() ), reinterpret_cast<const uint8*>( name.data() ) + name.size() );
                LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( target._listDelta.size() ) );
                for ( const MeshMorphDelta& delta : target._listDelta )
                {
                    LittleEndianUtil::appendUint32( outBytes, delta._vertexIndex );
                    const float32 arrValue[6] = { delta._position._x, delta._position._y, delta._position._z, delta._normal._x, delta._normal._y, delta._normal._z };
                    for ( const float32 value : arrValue )
                    {
                        LittleEndianUtil::appendFloat32( outBytes, value );
                    }
                }
            }
            const uint32 contentLength = static_cast<uint32>( outBytes.size() - contentStart );
            for ( uint32 byteIndex = 0; byteIndex < 4; ++byteIndex )
            {
                outBytes[lengthOffset + byteIndex] = static_cast<uint8>( ( contentLength >> ( byteIndex * 8 ) ) & 0xFFu );
            }
        }
        // 선택 덩어리 — 구간(있을 때), 낮은 상세 단계(있을 때).
        MeshAssetFormatInternal::appendSectionChunk( outBytes, data._listSection );
        MeshAssetFormatInternal::appendSectionSourceChunk( outBytes, data._listSection );
        MeshAssetFormatInternal::appendLODChunk( outBytes, data._listLOD, uvBounds );
        MeshAssetFormatInternal::appendLODSkinChunk( outBytes, data._listLOD );
    }

    bool MeshAssetFormat::readFromBytes( const uint8* pData, size_t size, MeshAssetData& outData, float32* pOutBoundingRadius )
    {
        using Internal = MeshAssetFormatInternal;
        outData        = MeshAssetData{};
        if ( pData == nullptr || size < kHeaderSize )
        {
            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetShorterHeader", "Mesh asset is shorter than its header (%# bytes)", size );
            return false;
        }
        if ( Memory::compare( pData, Internal::kArrMagic, sizeof( Internal::kArrMagic ) ) != 0 )
        {
            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetNoSWMS", "Mesh asset has no SWMS magic" );
            return false;
        }

        ByteReader reader{ pData, size };
        (void)reader.skipBytes( sizeof( Internal::kArrMagic ) ); // 위에서 길이와 매직을 봤다
        const uint32  version        = reader.readUint32();
        const uint32  vertexCount    = reader.readUint32();
        const uint32  indexCount     = reader.readUint32();
        const uint32  vertexSize     = reader.readUint32();
        const float32 boundingRadius = reader.readFloat32();
        const uint32  skinBoneCount  = reader.readUint32();
        if ( version != kVersion || vertexSize != kVertexSize )
        {
            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetVersionVertex", "Mesh asset version %# / vertex size %# is not the current %# / %# - re-import the model", version, vertexSize, kVersion, kVertexSize );
            return false;
        }

        const bool bWholeTriangles = vertexCount > 0 && indexCount > 0 && indexCount % 3 == 0;
        // 정점 수 × 크기를 64 비트로 셈한다 — 깨진 머리의 큰 정점 수가 32 비트로 넘쳐 작은 파일과 "맞아" 보이지 않게 한다.
        const uint64 skinSize     = skinBoneCount > 0 ? static_cast<uint64>( vertexCount ) * kSkinVertexSize + sizeof( uint64 ) + static_cast<uint64>( skinBoneCount ) * kInverseBindSize : 0u;
        const uint64 indexSize    = static_cast<uint64>( indexCount ) * sizeof( uint32 );
        const uint64 expectedSize = static_cast<uint64>( kHeaderSize ) + static_cast<uint64>( vertexCount ) * kVertexSize + skinSize + indexSize;
        // 정점 · 스킨 · 인덱스 뒤의 남은 바이트는 선택 덩어리다(아래에서 하나씩 읽는다). 모자라면 깨진 파일이다.
        if ( bWholeTriangles == false || expectedSize > static_cast<uint64>( size ) )
        {
            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetDeclaresVertices", "Mesh asset declares %# vertices and %# indices but holds %# bytes (expected %#)", vertexCount, indexCount, size,
                               expectedSize );
            return false;
        }
        // 머리 길이는 위에서 봤다 — 여기까지의 실패는 유한하지 않은 경계 반지름이다.
        if ( reader.hasOverflowed() || boundingRadius < 0.0f )
        {
            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetInvalidBounding", "Mesh asset has an invalid bounding radius" );
            return false;
        }

        Internal::UVBounds uvBounds{};
        if ( Internal::readUVBounds( reader, uvBounds ) == false )
        {
            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetInvalidUV", "Mesh asset has invalid UV bounds" );
            return false;
        }

        // 본문이 하나라도 깨졌으면 반쯤 읽은 데이터를 남기지 않는다.
        bool bRead = Internal::readVertices( reader, vertexCount, uvBounds, outData._listVertex );
        if ( bRead && skinBoneCount > 0 )
        {
            bRead = Internal::readSkinVertices( reader, vertexCount, skinBoneCount, outData._listSkinVertex ) && Internal::readSkinBinding( reader, skinBoneCount, outData._skinBinding );
        }
        if ( bRead && Internal::readIndices( reader, indexCount, vertexCount, outData._listIndex ) == false )
        {
            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetIndexOutOfRange", "Mesh asset has an index past its %# vertices - re-import the model", vertexCount );
            bRead = false;
        }
        // 길이는 머리에서 맞췄다 — 여기까지의 넘침은 정점 · 스킨의 유한하지 않은 값이다(모르는 플래그 · 뼈 번호는 넘침이 아니고 위에서 이미 알렸다).
        if ( reader.hasOverflowed() )
            SW_LOG_WARNING_ID( "MeshAssetFormat.MeshAssetNonFinite", "Mesh asset holds a non-finite value - re-import the model" );
        bRead = bRead && Internal::readOptionalChunks( reader, vertexCount, indexCount, uvBounds, outData );
        if ( bRead == false )
        {
            outData = MeshAssetData{};
            return false;
        }
        if ( pOutBoundingRadius != nullptr )
            *pOutBoundingRadius = boundingRadius;
        return true;
    }

    bool MeshAssetFormat::saveToFile( const AbsolutePath& absPath, const MeshAssetData& data )
    {
        vector<uint8> bytes;
        makeBytes( data, bytes );
        if ( FileUtil::ensureParentDirectoryExists( absPath.str() ) == false )
            return false;
        return FileUtil::writeFile( absPath.str(), bytes.data(), bytes.size() );
    }

    bool MeshAssetFormat::loadFromAsset( const AssetID& assetID, MeshAssetData& outData )
    {
        outData = MeshAssetData{};
        vector<uint8> bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false )
            return false;
        return readFromBytes( bytes.data(), bytes.size(), outData );
    }

} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( MeshAssetFormat, MeshAssetFormat::kAssetType );
} // namespace sw
