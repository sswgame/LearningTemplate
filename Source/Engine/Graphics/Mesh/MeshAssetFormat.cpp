#include "pch.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/ResourceUtil.h"

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
            /** @brief 정점 하나의 float32 개수입니다(위치 3 · 노멀 3 · UV 2 · 색 4). */
            static constexpr uint32 kFloatPerVertex = 12;
            /** @brief 모프 타깃 덩어리의 이름입니다. */
            static constexpr uint8 kArrMorphChunk[4] = { 'M', 'R', 'P', 'H' };
            /** @brief 모프 차이 하나의 바이트 수입니다(정점 번호 uint32 + float32 여섯). */
            static constexpr uint32 kMorphDeltaSize = 4 + 6 * 4;

            static_assert( MeshAssetFormat::kVertexSize == kFloatPerVertex * sizeof( float32 ), "kVertexSize must match the float layout" );
            static_assert( sizeof( RHIVertex ) == MeshAssetFormat::kVertexSize, "RHIVertex changed - bump MeshAssetFormat::kVersion and re-import models" );
            static_assert( MeshAssetFormat::kSkinVertexSize == 4 * sizeof( uint16 ) + 4 * sizeof( float32 ), "kSkinVertexSize must match the skin layout" );

            static void appendUint16( vector<uint8>& outBytes, uint16 value )
            {
                outBytes.push_back( static_cast<uint8>( value & 0xFFu ) );
                outBytes.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
            }

            static void appendUint32( vector<uint8>& outBytes, uint32 value )
            {
                outBytes.push_back( static_cast<uint8>( value & 0xFFu ) );
                outBytes.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
                outBytes.push_back( static_cast<uint8>( ( value >> 16 ) & 0xFFu ) );
                outBytes.push_back( static_cast<uint8>( ( value >> 24 ) & 0xFFu ) );
            }

            static void appendFloat32( vector<uint8>& outBytes, float32 value )
            {
                uint32 bits = 0;
                Memory::copy( &bits, &value, sizeof( bits ) );
                appendUint32( outBytes, bits );
            }

            static uint16 readUint16( const uint8* pData ) { return static_cast<uint16>( pData[0] | ( pData[1] << 8 ) ); }

            static uint32 readUint32( const uint8* pData )
            {
                return static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) | ( static_cast<uint32>( pData[2] ) << 16 ) |
                       ( static_cast<uint32>( pData[3] ) << 24 );
            }

            static float32 readFloat32( const uint8* pData )
            {
                const uint32 bits  = readUint32( pData );
                float32      value = 0.0f;
                Memory::copy( &value, &bits, sizeof( value ) );
                return value;
            }

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
        MeshAssetFormatInternal::appendUint32( outBytes, kVersion );
        MeshAssetFormatInternal::appendUint32( outBytes, static_cast<uint32>( data._listVertex.size() ) );
        MeshAssetFormatInternal::appendUint32( outBytes, kVertexSize );
        MeshAssetFormatInternal::appendFloat32( outBytes, MeshAssetFormatInternal::computeBoundingRadius( data._listVertex ) );
        MeshAssetFormatInternal::appendUint32( outBytes, skinBoneCount );

        for ( const RHIVertex& vertex : data._listVertex )
        {
            for ( const float32 value : vertex._arrPosition )
            {
                MeshAssetFormatInternal::appendFloat32( outBytes, value );
            }
            for ( const float32 value : vertex._arrNormal )
            {
                MeshAssetFormatInternal::appendFloat32( outBytes, value );
            }
            for ( const float32 value : vertex._arrUv )
            {
                MeshAssetFormatInternal::appendFloat32( outBytes, value );
            }
            for ( const float32 value : vertex._arrColor )
            {
                MeshAssetFormatInternal::appendFloat32( outBytes, value );
            }
        }
        if ( bSkin )
        {
            for ( const MeshSkinVertex& skin : data._listSkinVertex )
            {
                for ( const uint16 joint : skin._arrJoint )
                {
                    MeshAssetFormatInternal::appendUint16( outBytes, joint );
                }
                for ( const float32 weight : skin._arrWeight )
                {
                    MeshAssetFormatInternal::appendFloat32( outBytes, weight );
                }
            }
        }
        if ( data._listMorphTarget.empty() )
            return;
        // 선택 덩어리 — 모프 타깃. 길이는 내용을 다 쓴 뒤 채운다.
        for ( const uint8 letter : MeshAssetFormatInternal::kArrMorphChunk )
        {
            outBytes.push_back( letter );
        }
        const size_t lengthOffset = outBytes.size();
        MeshAssetFormatInternal::appendUint32( outBytes, 0u );
        const size_t contentStart = outBytes.size();
        MeshAssetFormatInternal::appendUint32( outBytes, static_cast<uint32>( data._listMorphTarget.size() ) );
        for ( const MeshMorphTarget& target : data._listMorphTarget )
        {
            const string_view name = target._name.c_str();
            MeshAssetFormatInternal::appendUint32( outBytes, static_cast<uint32>( name.size() ) );
            outBytes.insert( outBytes.end(), reinterpret_cast<const uint8*>( name.data() ), reinterpret_cast<const uint8*>( name.data() ) + name.size() );
            MeshAssetFormatInternal::appendUint32( outBytes, static_cast<uint32>( target._listDelta.size() ) );
            for ( const MeshMorphDelta& delta : target._listDelta )
            {
                MeshAssetFormatInternal::appendUint32( outBytes, delta._vertexIndex );
                const float32 arrValue[6] = { delta._position._x, delta._position._y, delta._position._z, delta._normal._x, delta._normal._y, delta._normal._z };
                for ( const float32 value : arrValue )
                {
                    MeshAssetFormatInternal::appendFloat32( outBytes, value );
                }
            }
        }
        const uint32 contentLength = static_cast<uint32>( outBytes.size() - contentStart );
        for ( uint32 byteIndex = 0; byteIndex < 4; ++byteIndex )
        {
            outBytes[lengthOffset + byteIndex] = static_cast<uint8>( ( contentLength >> ( byteIndex * 8 ) ) & 0xFFu );
        }
    }

    void MeshAssetFormat::makeBytes( const vector<RHIVertex>& listVertex, vector<uint8>& outBytes )
    {
        MeshAssetData data{};
        data._listVertex = listVertex;
        makeBytes( data, outBytes );
    }

    bool MeshAssetFormat::readFromBytes( const uint8* pData, size_t size, MeshAssetData& outData, float32* pOutBoundingRadius )
    {
        outData = MeshAssetData{};
        if ( pData == nullptr || size < kHeaderSize )
        {
            SW_LOG_WARNING( "Mesh asset is shorter than its header (%# bytes)", size );
            return false;
        }
        if ( Memory::compare( pData, MeshAssetFormatInternal::kArrMagic, sizeof( MeshAssetFormatInternal::kArrMagic ) ) != 0 )
        {
            SW_LOG_WARNING( "Mesh asset has no SWMS magic" );
            return false;
        }

        const uint32  version        = MeshAssetFormatInternal::readUint32( pData + 4 );
        const uint32  vertexCount    = MeshAssetFormatInternal::readUint32( pData + 8 );
        const uint32  vertexSize     = MeshAssetFormatInternal::readUint32( pData + 12 );
        const float32 boundingRadius = MeshAssetFormatInternal::readFloat32( pData + 16 );
        const uint32  skinBoneCount  = MeshAssetFormatInternal::readUint32( pData + 20 );
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

        outData._listVertex.resize( vertexCount );
        const uint8* pCursor = pData + kHeaderSize;
        for ( RHIVertex& vertex : outData._listVertex )
        {
            float32 arrValue[MeshAssetFormatInternal::kFloatPerVertex]{};
            for ( float32& value : arrValue )
            {
                value = MeshAssetFormatInternal::readFloat32( pCursor );
                pCursor += sizeof( float32 );
            }
            Memory::copy( vertex._arrPosition, arrValue + 0, sizeof( vertex._arrPosition ) );
            Memory::copy( vertex._arrNormal, arrValue + 3, sizeof( vertex._arrNormal ) );
            Memory::copy( vertex._arrUv, arrValue + 6, sizeof( vertex._arrUv ) );
            Memory::copy( vertex._arrColor, arrValue + 8, sizeof( vertex._arrColor ) );
        }
        if ( skinBoneCount > 0 )
        {
            outData._listSkinVertex.resize( vertexCount );
            for ( MeshSkinVertex& skin : outData._listSkinVertex )
            {
                for ( uint16& joint : skin._arrJoint )
                {
                    joint = MeshAssetFormatInternal::readUint16( pCursor );
                    pCursor += sizeof( uint16 );
                    if ( joint >= skinBoneCount )
                    {
                        SW_LOG_WARNING( "Mesh asset skin names bone %# but the skeleton has %# bones", joint, skinBoneCount );
                        outData = MeshAssetData{};
                        return false;
                    }
                }
                for ( float32& weight : skin._arrWeight )
                {
                    weight = MeshAssetFormatInternal::readFloat32( pCursor );
                    pCursor += sizeof( float32 );
                }
            }
            outData._skinBoneCount = skinBoneCount;
        }
        // 선택 덩어리 — 모르는 이름 · 길이가 맞지 않는 덩어리는 깨진 파일로 본다.
        const uint8* pEnd = pData + size;
        while ( pCursor < pEnd )
        {
            if ( static_cast<size_t>( pEnd - pCursor ) < 8 )
            {
                SW_LOG_WARNING( "Mesh asset has a truncated chunk header" );
                outData = MeshAssetData{};
                return false;
            }
            const bool   bMorph      = Memory::compare( pCursor, MeshAssetFormatInternal::kArrMorphChunk, 4 ) == 0;
            const uint32 chunkLength = MeshAssetFormatInternal::readUint32( pCursor + 4 );
            const uint8* pChunk      = pCursor + 8;
            const uint8* pChunkEnd   = pChunk + chunkLength;
            if ( bMorph == false || chunkLength > static_cast<size_t>( pEnd - pChunk ) )
            {
                SW_LOG_WARNING( "Mesh asset has an unknown or truncated chunk" );
                outData = MeshAssetData{};
                return false;
            }
            bool   bValid      = chunkLength >= 4;
            uint32 targetCount = bValid ? MeshAssetFormatInternal::readUint32( pChunk ) : 0u;
            pChunk += 4;
            for ( uint32 targetIndex = 0; bValid && targetIndex < targetCount; ++targetIndex )
            {
                bValid                  = pChunkEnd - pChunk >= 4;
                const uint32 nameLength = bValid ? MeshAssetFormatInternal::readUint32( pChunk ) : 0u;
                bValid                  = bValid && static_cast<size_t>( pChunkEnd - pChunk ) >= 8u + nameLength;
                if ( bValid == false )
                    break;
                MeshMorphTarget target{};
                target._name = hashed_string( string( reinterpret_cast<const utf8*>( pChunk + 4 ), nameLength ) );
                pChunk += 4 + nameLength;
                const uint32 deltaCount = MeshAssetFormatInternal::readUint32( pChunk );
                pChunk += 4;
                bValid = static_cast<uint64>( pChunkEnd - pChunk ) >= static_cast<uint64>( deltaCount ) * MeshAssetFormatInternal::kMorphDeltaSize;
                for ( uint32 deltaIndex = 0; bValid && deltaIndex < deltaCount; ++deltaIndex )
                {
                    MeshMorphDelta delta{};
                    delta._vertexIndex = MeshAssetFormatInternal::readUint32( pChunk );
                    float32 arrValue[6]{};
                    for ( uint32 valueIndex = 0; valueIndex < 6; ++valueIndex )
                    {
                        arrValue[valueIndex] = MeshAssetFormatInternal::readFloat32( pChunk + 4 + valueIndex * 4 );
                    }
                    delta._position = float3{ arrValue[0], arrValue[1], arrValue[2] };
                    delta._normal   = float3{ arrValue[3], arrValue[4], arrValue[5] };
                    pChunk += MeshAssetFormatInternal::kMorphDeltaSize;
                    bValid = delta._vertexIndex < vertexCount;
                    target._listDelta.push_back( delta );
                }
                outData._listMorphTarget.push_back( std::move( target ) );
            }
            if ( bValid == false || pChunk != pChunkEnd )
            {
                SW_LOG_WARNING( "Mesh asset has a malformed morph target chunk" );
                outData = MeshAssetData{};
                return false;
            }
            pCursor = pChunkEnd;
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

    bool MeshAssetFormat::loadFromResource( string_view path, MeshAssetData& outData )
    {
        outData = MeshAssetData{};
        vector<uint8> bytes;
        if ( ResourceUtil::readBinaryResource( path, bytes ) == false )
            return false;
        return readFromBytes( bytes.data(), bytes.size(), outData );
    }

    bool MeshAssetFormat::loadFromResource( string_view path, vector<RHIVertex>& outListVertex )
    {
        MeshAssetData data{};
        const bool    bRead = loadFromResource( path, data );
        outListVertex       = std::move( data._listVertex );
        return bRead;
    }
} // namespace sw
