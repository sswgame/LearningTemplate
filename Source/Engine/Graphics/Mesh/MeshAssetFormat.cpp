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
         * @brief 由ы? ?붾뵒??諛붿씠???쎄린 쨌 ?곌린?낅땲?? ?몄뒪??諛붿씠???쒖꽌??湲곕?吏 ?딅룄濡?媛믪쓣 諛붿씠?몃줈 ????곷땲??
         */
        struct MeshAssetFormatInternal
        {
            /** @brief ?뚯씪 癒몃━??留ㅼ쭅(`SWMS`)?낅땲?? */
            static constexpr uint8 kArrMagic[4] = { 'S', 'W', 'M', 'S' };
            /** @brief ?뺤젏 ?섎굹??float32 媛쒖닔?낅땲???꾩튂 3 쨌 ?몃? 3 쨌 UV 2 쨌 ??4). */
            static constexpr uint32 kFloatPerVertex = 12;

            static_assert( MeshAssetFormat::kVertexSize == kFloatPerVertex * sizeof( float32 ), "kVertexSize must match the float layout" );
            static_assert( sizeof( RHIVertex ) == MeshAssetFormat::kVertexSize, "RHIVertex changed - bump MeshAssetFormat::kVersion and re-import models" );

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

            /** @brief ?먯젏?먯꽌 媛??癒??뺤젏源뚯???嫄곕━?낅땲??`Mesh::getBoundingRadius` ? 媛숈? ?뺤쓽). */
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

    void MeshAssetFormat::makeBytes( const vector<RHIVertex>& listVertex, vector<uint8>& outBytes )
    {
        outBytes.clear();
        outBytes.reserve( kHeaderSize + listVertex.size() * kVertexSize );
        for ( const uint8 magic : MeshAssetFormatInternal::kArrMagic )
            outBytes.push_back( magic );
        MeshAssetFormatInternal::appendUint32( outBytes, kVersion );
        MeshAssetFormatInternal::appendUint32( outBytes, static_cast<uint32>( listVertex.size() ) );
        MeshAssetFormatInternal::appendUint32( outBytes, kVertexSize );
        MeshAssetFormatInternal::appendFloat32( outBytes, MeshAssetFormatInternal::computeBoundingRadius( listVertex ) );

        for ( const RHIVertex& vertex : listVertex )
        {
            for ( const float32 value : vertex._arrPosition )
                MeshAssetFormatInternal::appendFloat32( outBytes, value );
            for ( const float32 value : vertex._arrNormal )
                MeshAssetFormatInternal::appendFloat32( outBytes, value );
            for ( const float32 value : vertex._arrUv )
                MeshAssetFormatInternal::appendFloat32( outBytes, value );
            for ( const float32 value : vertex._arrColor )
                MeshAssetFormatInternal::appendFloat32( outBytes, value );
        }
    }

    bool MeshAssetFormat::readFromBytes( const uint8* pData, size_t size, vector<RHIVertex>& outListVertex, float32* pOutBoundingRadius )
    {
        outListVertex.clear();
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
        if ( version != kVersion || vertexSize != kVertexSize )
        {
            SW_LOG_WARNING( "Mesh asset version %# / vertex size %# is not the current %# / %# - re-import the model", version, vertexSize, kVersion, kVertexSize );
            return false;
        }

        const bool bWholeTriangles = vertexCount > 0 && vertexCount % 3 == 0;
        // ?뺤젏 ??횞 ?ш린瑜?64 鍮꾪듃濡??덊븳????源⑥쭊 癒몃━?????뺤젏 ?섍? 32 鍮꾪듃濡??섏퀜 ?묒? ?뚯씪怨?"留욎븘" 蹂댁씠吏 ?딄쾶 ?쒕떎.
        const uint64 expectedSize = static_cast<uint64>( kHeaderSize ) + static_cast<uint64>( vertexCount ) * kVertexSize;
        if ( bWholeTriangles == false || expectedSize != static_cast<uint64>( size ) )
        {
            SW_LOG_WARNING( "Mesh asset declares %# vertices but holds %# bytes (expected %#)", vertexCount, size, expectedSize );
            return false;
        }
        if ( MathUtil::isFinite( boundingRadius ) == false || boundingRadius < 0.0f )
        {
            SW_LOG_WARNING( "Mesh asset has an invalid bounding radius" );
            return false;
        }

        outListVertex.resize( vertexCount );
        const uint8* pCursor = pData + kHeaderSize;
        for ( RHIVertex& vertex : outListVertex )
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
        if ( pOutBoundingRadius != nullptr )
            *pOutBoundingRadius = boundingRadius;
        return true;
    }

    bool MeshAssetFormat::saveToFile( string_view path, const vector<RHIVertex>& listVertex )
    {
        vector<uint8> bytes;
        makeBytes( listVertex, bytes );
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeFile( path, bytes.data(), bytes.size() );
    }

    bool MeshAssetFormat::loadFromResource( string_view path, vector<RHIVertex>& outListVertex )
    {
        outListVertex.clear();
        vector<uint8> bytes;
        if ( ResourceUtil::readBinaryResource( path, bytes ) == false )
            return false;
        return readFromBytes( bytes.data(), bytes.size(), outListVertex );
    }
} // namespace sw
