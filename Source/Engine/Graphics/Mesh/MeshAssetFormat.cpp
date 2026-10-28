#include "pch.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Math/OctahedralNormal.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    SW_LOG_CALLER( "MeshAssetFormat" );

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
            struct UvBounds
            {
                float32 _arrMin[2]{};
                float32 _arrMax[2]{};
            };

            /** @brief 정점 목록들의 UV 를 모두 덮는 경계입니다. 목록이 모두 비었으면 0 입니다. */
            static UvBounds computeUvBounds( const vector<RHIVertex>& listFirst, const vector<MeshLODLevel>& listLevel )
            {
                UvBounds   bounds{};
                bool       bAny    = false;
                const auto include = [&]( const vector<RHIVertex>& listVertex )
                {
                    for ( const RHIVertex& vertex : listVertex )
                    {
                        for ( uint32 axis = 0; axis < 2; ++axis )
                        {
                            const float32 value = vertex._arrUv[axis];
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
            static void appendUvBounds( vector<uint8>& outBytes, const UvBounds& bounds )
            {
                LittleEndianUtil::appendFloat32( outBytes, bounds._arrMin[0] );
                LittleEndianUtil::appendFloat32( outBytes, bounds._arrMin[1] );
                LittleEndianUtil::appendFloat32( outBytes, bounds._arrMax[0] );
                LittleEndianUtil::appendFloat32( outBytes, bounds._arrMax[1] );
            }

            /** @brief UV 경계를 머리에서 읽습니다. 유한하지 않거나 최대가 최소보다 작으면 false 입니다. */
            [[nodiscard]] static bool readUvBounds( const uint8* pData, UvBounds& outBounds )
            {
                outBounds._arrMin[0] = LittleEndianUtil::readFloat32( pData + 0 );
                outBounds._arrMin[1] = LittleEndianUtil::readFloat32( pData + 4 );
                outBounds._arrMax[0] = LittleEndianUtil::readFloat32( pData + 8 );
                outBounds._arrMax[1] = LittleEndianUtil::readFloat32( pData + 12 );
                for ( uint32 axis = 0; axis < 2; ++axis )
                {
                    const bool bValid = MathUtil::isFinite( outBounds._arrMin[axis] ) && MathUtil::isFinite( outBounds._arrMax[axis] ) && outBounds._arrMin[axis] <= outBounds._arrMax[axis];
                    if ( bValid == false )
                        return false;
                }
                return true;
            }

            /** @brief UV 한 성분을 경계 안의 [0, 1] 로 옮겨 `unorm16` 으로 담습니다. 경계 폭이 0 이면 0 입니다. */
            static uint16 packUv( float32 value, float32 minValue, float32 maxValue )
            {
                const float32 extent = maxValue - minValue;
                return unorm16::fromFloat( extent > 0.0f ? ( value - minValue ) / extent : 0.0f ).getRaw();
            }

            /** @brief `unorm16` UV 한 성분을 경계 안의 값으로 풉니다. */
            static float32 unpackUv( uint16 raw, float32 minValue, float32 maxValue )
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

            /** @brief `appendOctahedral` 로 쓴 4 바이트를 단위 벡터로 풀고 @p pCursor 를 옮깁니다. */
            static float3 readOctahedral( const uint8*& pCursor )
            {
                const OctahedralNormal packed{ snorm16::fromRaw( static_cast<int16>( LittleEndianUtil::readUint16( pCursor ) ) ), snorm16::fromRaw( static_cast<int16>( LittleEndianUtil::readUint16( pCursor + 2 ) ) ) };
                pCursor += 4;
                return packed.toNormal();
            }
            /** @brief 모프 타깃 덩어리의 이름입니다. */
            static constexpr uint8 kArrMorphChunk[4] = { 'M', 'R', 'P', 'H' };
            /** @brief 모프 차이 하나의 바이트 수입니다(정점 번호 uint32 + float32 여섯). */
            static constexpr uint32 kMorphDeltaSize = 4 + 6 * 4;
            /** @brief 구간 덩어리의 이름입니다. */
            static constexpr uint8 kArrSectionChunk[4] = { 'S', 'E', 'C', 'T' };
            /** @brief 낮은 상세 단계 덩어리의 이름입니다. */
            static constexpr uint8 kArrLODChunk[4] = { 'L', 'O', 'D', 'S' };
            /** @brief 스킨드 메시의 낮은 상세 단계 스킨 덩어리의 이름입니다(`LODS` 덩어리 뒤에 온다). */
            static constexpr uint8 kArrLODSkinChunk[4] = { 'L', 'S', 'K', 'N' };

            /**
             * @brief 정점 배열을 씁니다. 탄젠트는 받은 값이 아니라 기준(`Mesh::computeTangents`)으로 구해 싣는다 — 파일의 탄젠트가 런타임 메시와 늘 같다.
             * @details LOD 단계는 단계마다 따로 부른다 — 탄젠트를 묶는 이웃은 같은 단계의 삼각형뿐이다.
             */
            static void appendVertices( vector<uint8>& outBytes, const vector<RHIVertex>& listSource, const UvBounds& uvBounds )
            {
                vector<RHIVertex> listVertex = listSource;
                Mesh::computeTangents( listVertex );
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
                        LittleEndianUtil::appendUint16( outBytes, packUv( vertex._arrUv[axis], uvBounds._arrMin[axis], uvBounds._arrMax[axis] ) );
                    }
                    for ( const float32 value : vertex._arrColor )
                    {
                        outBytes.push_back( unorm8::fromFloat( value ).getRaw() );
                    }
                    LittleEndianUtil::appendUint32( outBytes, vertex._arrTangent[3] < 0.0f ? kFlagBitangentNegative : 0u );
                }
            }

            /** @brief 낮은 상세 단계 덩어리(`LODS`)를 씁니다 — 단계 수, 단계마다 임계값 · 정점 수 · 정점. 단계가 없으면 쓰지 않습니다. */
            static void appendLODChunk( vector<uint8>& outBytes, const vector<MeshLODLevel>& listLevel, const UvBounds& uvBounds )
            {
                if ( listLevel.empty() )
                    return;
                const size_t lengthOffset = beginChunk( outBytes, kArrLODChunk );
                LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( listLevel.size() ) );
                for ( const MeshLODLevel& level : listLevel )
                {
                    LittleEndianUtil::appendFloat32( outBytes, level._screenSize );
                    LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( level._listVertex.size() ) );
                    appendVertices( outBytes, level._listVertex, uvBounds );
                }
                finishChunk( outBytes, lengthOffset );
            }

            /**
             * @brief 낮은 상세 단계 덩어리 내용을 읽습니다. 길이 · 탄젠트가 맞지 않으면 false 입니다.
             * @details 정점 수와 임계값의 뜻(삼각형 · 내려가는 임계값 · 스킨 없는 메시)은 메시가 다시 본다(`Mesh::setLODs`) — 규칙은 한 곳에 둔다.
             */
            [[nodiscard]] static bool readLODChunk( const uint8* pChunk, const uint8* pChunkEnd, const UvBounds& uvBounds, vector<MeshLODLevel>& outListLevel )
            {
                if ( pChunkEnd - pChunk < 4 )
                    return false;
                const uint32 levelCount = LittleEndianUtil::readUint32( pChunk );
                pChunk += 4;
                for ( uint32 levelIndex = 0; levelIndex < levelCount; ++levelIndex )
                {
                    if ( pChunkEnd - pChunk < 8 )
                        return false;
                    MeshLODLevel level{};
                    level._screenSize        = LittleEndianUtil::readFloat32( pChunk );
                    const uint32 vertexCount = LittleEndianUtil::readUint32( pChunk + 4 );
                    pChunk += 8;
                    if ( static_cast<uint64>( pChunkEnd - pChunk ) < static_cast<uint64>( vertexCount ) * MeshAssetFormat::kVertexSize )
                        return false;
                    if ( readVertices( pChunk, vertexCount, uvBounds, level._listVertex ) == false )
                        return false;
                    outListLevel.push_back( std::move( level ) );
                }
                return pChunk == pChunkEnd;
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
            [[nodiscard]] static bool readLODSkinChunk( const uint8* pChunk, const uint8* pChunkEnd, uint32 skinBoneCount, vector<MeshLODLevel>& ioListLevel )
            {
                if ( pChunkEnd - pChunk < 4 || skinBoneCount == 0 )
                    return false;
                const uint32 levelCount = LittleEndianUtil::readUint32( pChunk );
                pChunk += 4;
                if ( levelCount != ioListLevel.size() )
                    return false;
                for ( MeshLODLevel& level : ioListLevel )
                {
                    if ( pChunkEnd - pChunk < 4 )
                        return false;
                    const uint32 vertexCount = LittleEndianUtil::readUint32( pChunk );
                    pChunk += 4;
                    if ( vertexCount != level._listVertex.size() || static_cast<uint64>( pChunkEnd - pChunk ) < static_cast<uint64>( vertexCount ) * MeshAssetFormat::kSkinVertexSize )
                        return false;
                    if ( readSkinVertices( pChunk, vertexCount, skinBoneCount, level._listSkinVertex ) == false )
                        return false;
                }
                return pChunk == pChunkEnd;
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

            /** @brief 구간 덩어리(`SECT`)를 씁니다 — 구간 수, 구간마다 이름 길이 · 이름 · 첫 정점 · 정점 수 · 시점 가시성. 구간이 없으면 쓰지 않습니다. */
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
                    LittleEndianUtil::appendUint32( outBytes, section._firstVertex );
                    LittleEndianUtil::appendUint32( outBytes, section._vertexCount );
                    LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( section._personVisibility ) );
                }
                finishChunk( outBytes, lengthOffset );
            }

            /** @brief 구간 덩어리 내용을 읽습니다. 길이 · 범위 · 가시성 값이 맞지 않으면 false 입니다(범위와 이름 중복은 `Mesh::setSections` 가 다시 본다). */
            [[nodiscard]] static bool readSectionChunk( const uint8* pChunk, const uint8* pChunkEnd, uint32 vertexCount, vector<MeshSection>& outListSection )
            {
                if ( pChunkEnd - pChunk < 4 )
                    return false;
                const uint32 sectionCount = LittleEndianUtil::readUint32( pChunk );
                pChunk += 4;
                for ( uint32 sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex )
                {
                    if ( pChunkEnd - pChunk < 4 )
                        return false;
                    const uint32 nameLength = LittleEndianUtil::readUint32( pChunk );
                    if ( static_cast<size_t>( pChunkEnd - pChunk ) < 16u + nameLength )
                        return false;
                    MeshSection section{};
                    section._name = hashed_string( string( reinterpret_cast<const utf8*>( pChunk + 4 ), nameLength ) );
                    pChunk += 4 + nameLength;
                    section._firstVertex    = LittleEndianUtil::readUint32( pChunk );
                    section._vertexCount    = LittleEndianUtil::readUint32( pChunk + 4 );
                    const uint32 visibility = LittleEndianUtil::readUint32( pChunk + 8 );
                    pChunk += 12;
                    const bool bValidSection = visibility < static_cast<uint32>( MeshPersonVisibility::Count ) && section._firstVertex <= vertexCount &&
                                               section._vertexCount <= vertexCount - section._firstVertex;
                    if ( bValidSection == false )
                        return false;
                    section._personVisibility = static_cast<MeshPersonVisibility>( visibility );
                    outListSection.push_back( std::move( section ) );
                }
                return pChunk == pChunkEnd;
            }

            // 파일의 정점은 압축 배치이고 메모리의 `RHIVertex`(float32 16 개)와 크기가 다르다 — 배치를 바꾸면 `kVersion` 을 올리고 모델을 다시 임포트한다.
            static_assert( MeshAssetFormat::kVertexSize == 3 * sizeof( float32 ) + 2 * sizeof( OctahedralNormal ) + sizeof( unorm16x2 ) + sizeof( unorm8x4 ) + sizeof( uint32 ),
                           "kVertexSize must match the packed layout (position, normal, tangent, uv, color, flags)" );
            static_assert( MeshAssetFormat::kHeaderSize == 24 + sizeof( UvBounds ), "kHeaderSize must hold the fixed fields and the UV bounds" );
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
             * @brief 정점 @p vertexCount 개를 읽어 풀고 @p pCursor 를 옮깁니다. 플래그에 모르는 비트가 있으면 false 입니다.
             * @details 법선 · 탄젠트는 옥타헤드럴을 풀어 단위 길이이고 탄젠트 w 는 플래그에서 ±1 로 복원한다.
             */
            [[nodiscard]] static bool readVertices( const uint8*& pCursor, uint32 vertexCount, const UvBounds& uvBounds, vector<RHIVertex>& outListVertex )
            {
                outListVertex.resize( vertexCount );
                for ( RHIVertex& vertex : outListVertex )
                {
                    for ( float32& value : vertex._arrPosition )
                    {
                        value = LittleEndianUtil::readFloat32( pCursor );
                        pCursor += sizeof( float32 );
                    }
                    const float3 normal  = readOctahedral( pCursor );
                    const float3 tangent = readOctahedral( pCursor );
                    vertex._arrNormal[0] = normal._x;
                    vertex._arrNormal[1] = normal._y;
                    vertex._arrNormal[2] = normal._z;
                    for ( uint32 axis = 0; axis < 2; ++axis )
                    {
                        vertex._arrUv[axis] = unpackUv( LittleEndianUtil::readUint16( pCursor ), uvBounds._arrMin[axis], uvBounds._arrMax[axis] );
                        pCursor += sizeof( uint16 );
                    }
                    for ( float32& value : vertex._arrColor )
                    {
                        value = unorm8::fromRaw( *pCursor ).toFloat();
                        ++pCursor;
                    }
                    const uint32 flags = LittleEndianUtil::readUint32( pCursor );
                    pCursor += sizeof( uint32 );
                    if ( ( flags & ~kFlagBitangentNegative ) != 0u )
                    {
                        SW_LOG_WARNING( "Mesh asset has unknown vertex flags %# - re-import the model", flags );
                        return false;
                    }
                    vertex._arrTangent[0] = tangent._x;
                    vertex._arrTangent[1] = tangent._y;
                    vertex._arrTangent[2] = tangent._z;
                    vertex._arrTangent[3] = ( flags & kFlagBitangentNegative ) != 0u ? -1.0f : 1.0f;
                }
                return true;
            }

            /** @brief 스킨 정점 @p vertexCount 개를 읽어 @p pCursor 를 옮깁니다. 뼈 번호가 뼈 수를 넘으면 false 입니다. */
            [[nodiscard]] static bool readSkinVertices( const uint8*& pCursor, uint32 vertexCount, uint32 skinBoneCount, vector<MeshSkinVertex>& outListSkin )
            {
                outListSkin.resize( vertexCount );
                for ( MeshSkinVertex& skin : outListSkin )
                {
                    for ( uint16& joint : skin._arrJoint )
                    {
                        joint = LittleEndianUtil::readUint16( pCursor );
                        pCursor += sizeof( uint16 );
                        if ( joint >= skinBoneCount )
                        {
                            SW_LOG_WARNING( "Mesh asset skin names bone %# but the skeleton has %# bones", joint, skinBoneCount );
                            return false;
                        }
                    }
                    for ( float32& weight : skin._arrWeight )
                    {
                        weight = LittleEndianUtil::readFloat32( pCursor );
                        pCursor += sizeof( float32 );
                    }
                }
                return true;
            }

            /** @brief 모프 타깃 덩어리 본문([@p pChunk, @p pChunkEnd))을 읽습니다. 길이 · 정점 번호가 맞지 않거나 본문을 다 쓰지 않으면 false 입니다. */
            [[nodiscard]] static bool readMorphChunk( const uint8* pChunk, const uint8* pChunkEnd, uint32 vertexCount, vector<MeshMorphTarget>& outListTarget )
            {
                bool   bValid      = pChunkEnd - pChunk >= 4;
                uint32 targetCount = bValid ? LittleEndianUtil::readUint32( pChunk ) : 0u;
                pChunk += 4;
                for ( uint32 targetIndex = 0; bValid && targetIndex < targetCount; ++targetIndex )
                {
                    bValid                  = pChunkEnd - pChunk >= 4;
                    const uint32 nameLength = bValid ? LittleEndianUtil::readUint32( pChunk ) : 0u;
                    bValid                  = bValid && static_cast<size_t>( pChunkEnd - pChunk ) >= 8u + nameLength;
                    if ( bValid == false )
                        break;
                    MeshMorphTarget target{};
                    target._name = hashed_string( string( reinterpret_cast<const utf8*>( pChunk + 4 ), nameLength ) );
                    pChunk += 4 + nameLength;
                    const uint32 deltaCount = LittleEndianUtil::readUint32( pChunk );
                    pChunk += 4;
                    bValid = static_cast<uint64>( pChunkEnd - pChunk ) >= static_cast<uint64>( deltaCount ) * kMorphDeltaSize;
                    for ( uint32 deltaIndex = 0; bValid && deltaIndex < deltaCount; ++deltaIndex )
                    {
                        MeshMorphDelta delta{};
                        delta._vertexIndex = LittleEndianUtil::readUint32( pChunk );
                        float32 arrValue[6]{};
                        for ( uint32 valueIndex = 0; valueIndex < 6; ++valueIndex )
                        {
                            arrValue[valueIndex] = LittleEndianUtil::readFloat32( pChunk + 4 + valueIndex * 4 );
                        }
                        delta._position = float3{ arrValue[0], arrValue[1], arrValue[2] };
                        delta._normal   = float3{ arrValue[3], arrValue[4], arrValue[5] };
                        pChunk += kMorphDeltaSize;
                        bValid = delta._vertexIndex < vertexCount;
                        target._listDelta.push_back( delta );
                    }
                    outListTarget.push_back( std::move( target ) );
                }
                return bValid && pChunk == pChunkEnd;
            }

            /** @brief 정점 · 스킨 뒤의 선택 덩어리를 모두 읽습니다. 모르는 이름 · 길이가 맞지 않는 덩어리는 깨진 파일로 보고 false 입니다. */
            [[nodiscard]] static bool readOptionalChunks( const uint8* pCursor, const uint8* pEnd, uint32 vertexCount, const UvBounds& uvBounds, MeshAssetData& ioData )
            {
                while ( pCursor < pEnd )
                {
                    if ( static_cast<size_t>( pEnd - pCursor ) < 8 )
                    {
                        SW_LOG_WARNING( "Mesh asset has a truncated chunk header" );
                        return false;
                    }
                    const bool   bMorph      = Memory::compare( pCursor, kArrMorphChunk, 4 ) == 0;
                    const bool   bSection    = Memory::compare( pCursor, kArrSectionChunk, 4 ) == 0;
                    const bool   bLOD        = Memory::compare( pCursor, kArrLODChunk, 4 ) == 0;
                    const bool   bLODSkin    = Memory::compare( pCursor, kArrLODSkinChunk, 4 ) == 0;
                    const uint32 chunkLength = LittleEndianUtil::readUint32( pCursor + 4 );
                    const uint8* pChunk      = pCursor + 8;
                    const bool   bKnownChunk = bMorph || bSection || bLOD || bLODSkin;
                    if ( bKnownChunk == false || chunkLength > static_cast<size_t>( pEnd - pChunk ) )
                    {
                        SW_LOG_WARNING( "Mesh asset has an unknown or truncated chunk" );
                        return false;
                    }
                    const uint8* pChunkEnd = pChunk + chunkLength;
                    if ( bLOD )
                    {
                        if ( readLODChunk( pChunk, pChunkEnd, uvBounds, ioData._listLOD ) == false )
                        {
                            SW_LOG_WARNING( "Mesh asset has a malformed LOD chunk" );
                            return false;
                        }
                        pCursor = pChunkEnd;
                        continue;
                    }
                    if ( bLODSkin )
                    {
                        if ( readLODSkinChunk( pChunk, pChunkEnd, ioData._skinBoneCount, ioData._listLOD ) == false )
                        {
                            SW_LOG_WARNING( "Mesh asset has a malformed LOD skin chunk (it must follow the LOD chunk and match its levels)" );
                            return false;
                        }
                        pCursor = pChunkEnd;
                        continue;
                    }
                    if ( bSection )
                    {
                        if ( readSectionChunk( pChunk, pChunkEnd, vertexCount, ioData._listSection ) == false )
                        {
                            SW_LOG_WARNING( "Mesh asset has a malformed section chunk" );
                            return false;
                        }
                        pCursor = pChunkEnd;
                        continue;
                    }
                    if ( readMorphChunk( pChunk, pChunkEnd, vertexCount, ioData._listMorphTarget ) == false )
                    {
                        SW_LOG_WARNING( "Mesh asset has a malformed morph target chunk" );
                        return false;
                    }
                    pCursor = pChunkEnd;
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
        const uint32 skinBoneCount = bSkin ? data._skinBoneCount : 0u;
        const size_t skinByteCount = bSkin ? data._listSkinVertex.size() * kSkinVertexSize : 0u;
        outBytes.clear();
        outBytes.reserve( kHeaderSize + data._listVertex.size() * kVertexSize + skinByteCount );
        for ( const uint8 magic : MeshAssetFormatInternal::kArrMagic )
        {
            outBytes.push_back( magic );
        }
        LittleEndianUtil::appendUint32( outBytes, kVersion );
        LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( data._listVertex.size() ) );
        LittleEndianUtil::appendUint32( outBytes, kVertexSize );
        LittleEndianUtil::appendFloat32( outBytes, MeshAssetFormatInternal::computeBoundingRadius( data._listVertex ) );
        LittleEndianUtil::appendUint32( outBytes, skinBoneCount );
        // UV 경계는 기본 정점과 모든 LOD 를 덮는다(LOD 의 UV 가 같은 사각형으로 정규화된다).
        const MeshAssetFormatInternal::UvBounds uvBounds = MeshAssetFormatInternal::computeUvBounds( data._listVertex, data._listLOD );
        MeshAssetFormatInternal::appendUvBounds( outBytes, uvBounds );

        MeshAssetFormatInternal::appendVertices( outBytes, data._listVertex, uvBounds );
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
        }
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
        MeshAssetFormatInternal::appendLODChunk( outBytes, data._listLOD, uvBounds );
        MeshAssetFormatInternal::appendLODSkinChunk( outBytes, data._listLOD );
    }

    void MeshAssetFormat::makeBytes( const vector<RHIVertex>& listVertex, vector<uint8>& outBytes )
    {
        MeshAssetData data{};
        data._listVertex = listVertex;
        makeBytes( data, outBytes );
    }

    bool MeshAssetFormat::readFromBytes( const uint8* pData, size_t size, MeshAssetData& outData, float32* pOutBoundingRadius )
    {
        using Internal = MeshAssetFormatInternal;
        outData        = MeshAssetData{};
        if ( pData == nullptr || size < kHeaderSize )
        {
            SW_LOG_WARNING( "Mesh asset is shorter than its header (%# bytes)", size );
            return false;
        }
        if ( Memory::compare( pData, Internal::kArrMagic, sizeof( Internal::kArrMagic ) ) != 0 )
        {
            SW_LOG_WARNING( "Mesh asset has no SWMS magic" );
            return false;
        }

        const uint32  version        = LittleEndianUtil::readUint32( pData + 4 );
        const uint32  vertexCount    = LittleEndianUtil::readUint32( pData + 8 );
        const uint32  vertexSize     = LittleEndianUtil::readUint32( pData + 12 );
        const float32 boundingRadius = LittleEndianUtil::readFloat32( pData + 16 );
        const uint32  skinBoneCount  = LittleEndianUtil::readUint32( pData + 20 );
        if ( version != kVersion || vertexSize != kVertexSize )
        {
            SW_LOG_WARNING( "Mesh asset version %# / vertex size %# is not the current %# / %# - re-import the model", version, vertexSize, kVersion, kVertexSize );
            return false;
        }

        const bool bWholeTriangles = vertexCount > 0 && vertexCount % 3 == 0;
        // 정점 수 × 크기를 64 비트로 셈한다 — 깨진 머리의 큰 정점 수가 32 비트로 넘쳐 작은 파일과 "맞아" 보이지 않게 한다.
        const uint64 skinSize     = skinBoneCount > 0 ? static_cast<uint64>( vertexCount ) * kSkinVertexSize : 0u;
        const uint64 expectedSize = static_cast<uint64>( kHeaderSize ) + static_cast<uint64>( vertexCount ) * kVertexSize + skinSize;
        // 정점 · 스킨 뒤의 남은 바이트는 선택 덩어리다(아래에서 하나씩 읽는다). 모자라면 깨진 파일이다.
        if ( bWholeTriangles == false || expectedSize > static_cast<uint64>( size ) )
        {
            SW_LOG_WARNING( "Mesh asset declares %# vertices but holds %# bytes (expected %#)", vertexCount, size, expectedSize );
            return false;
        }
        if ( MathUtil::isFinite( boundingRadius ) == false || boundingRadius < 0.0f )
        {
            SW_LOG_WARNING( "Mesh asset has an invalid bounding radius" );
            return false;
        }

        Internal::UvBounds uvBounds{};
        if ( Internal::readUvBounds( pData + 24, uvBounds ) == false )
        {
            SW_LOG_WARNING( "Mesh asset has invalid UV bounds" );
            return false;
        }

        // 본문이 하나라도 깨졌으면 반쯤 읽은 데이터를 남기지 않는다.
        const uint8* pCursor = pData + kHeaderSize;
        bool         bRead   = Internal::readVertices( pCursor, vertexCount, uvBounds, outData._listVertex );
        if ( bRead && skinBoneCount > 0 )
        {
            bRead                  = Internal::readSkinVertices( pCursor, vertexCount, skinBoneCount, outData._listSkinVertex );
            outData._skinBoneCount = skinBoneCount;
        }
        bRead = bRead && Internal::readOptionalChunks( pCursor, pData + size, vertexCount, uvBounds, outData );
        if ( bRead == false )
        {
            outData = MeshAssetData{};
            return false;
        }
        if ( pOutBoundingRadius != nullptr )
            *pOutBoundingRadius = boundingRadius;
        return true;
    }

    bool MeshAssetFormat::readFromBytes( const uint8* pData, size_t size, vector<RHIVertex>& outListVertex, float32* pOutBoundingRadius )
    {
        MeshAssetData data{};
        const bool    bRead = readFromBytes( pData, size, data, pOutBoundingRadius );
        outListVertex       = std::move( data._listVertex );
        return bRead;
    }

    bool MeshAssetFormat::saveToFile( string_view path, const MeshAssetData& data )
    {
        vector<uint8> bytes;
        makeBytes( data, bytes );
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeFile( path, bytes.data(), bytes.size() );
    }

    bool MeshAssetFormat::saveToFile( string_view path, const vector<RHIVertex>& listVertex )
    {
        MeshAssetData data{};
        data._listVertex = listVertex;
        return saveToFile( path, data );
    }

    bool MeshAssetFormat::loadFromResource( const ResourceID& resourceID, MeshAssetData& outData )
    {
        outData = MeshAssetData{};
        vector<uint8> bytes;
        if ( ResourceIO::readBinaryResource( resourceID, bytes ) == false )
            return false;
        return readFromBytes( bytes.data(), bytes.size(), outData );
    }

    bool MeshAssetFormat::loadFromResource( const ResourceID& resourceID, vector<RHIVertex>& outListVertex )
    {
        MeshAssetData data{};
        const bool    bRead = loadFromResource( resourceID, data );
        outListVertex       = std::move( data._listVertex );
        return bRead;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( MeshAssetFormat, MeshAssetFormat::kAssetType );
} // namespace sw
