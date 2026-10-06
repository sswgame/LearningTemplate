#include "pch.h"

#include "Core/Network/NetCompression.h"

#include "Core/Compression/CompressionCodecRegistry.h"

namespace sw
{
    namespace
    {
        struct NetCompressionInternal
        {
            static CompressionCodecRegistry* resolveRegistry( CompressionCodecRegistry* pRegistry )
            {
                return pRegistry != nullptr ? pRegistry : CompressionCodecRegistry::getActive();
            }

            static int32 writeVarUint( uint8* pOut, uint32 value )
            {
                int32 count = 0;
                while ( value >= 0x80u )
                {
                    pOut[count++] = static_cast<uint8>( value | 0x80u );
                    value >>= 7;
                }
                pOut[count++] = static_cast<uint8>( value );
                return count;
            }

            /** @brief 읽은 바이트 수입니다. 5 바이트 안에 끝나지 않거나 모자라면 0 입니다. */
            static int32 readVarUint( const uint8* pData, int32 size, uint32& outValue )
            {
                outValue = 0;
                for ( int32 index = 0; index < size && index < 5; ++index )
                {
                    outValue |= static_cast<uint32>( pData[index] & 0x7Fu ) << ( 7 * index );
                    if ( ( pData[index] & 0x80u ) == 0 )
                        return index + 1;
                }
                return 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool NetCompressionUtil::compressEnvelope( const NetCompressionSettings& settings, const uint8* pData, int32 size, vector<uint8>& outBytes,
                                               CompressionCodecRegistry* pRegistry )
    {
        outBytes.clear();
        if ( settings._codec == CompressionCodecType::None || size < settings._minInputBytes || size <= 0 )
            return false;
        CompressionCodecRegistry* pResolved = NetCompressionInternal::resolveRegistry( pRegistry );
        ICompressionCodec*        pCodec    = pResolved != nullptr ? pResolved->getCodec( settings._codec ) : nullptr;
        if ( pCodec == nullptr )
            return false;
        outBytes.resize( static_cast<size_t>( kMaxEnvelopeHeaderSize ) + pCodec->compressBound( static_cast<size_t>( size ) ) );
        outBytes[0]                = static_cast<uint8>( settings._codec );
        const int32 headerSize     = 1 + NetCompressionInternal::writeVarUint( outBytes.data() + 1, static_cast<uint32>( size ) );
        size_t      compressedSize = 0;
        if ( pCodec->compress( pData, static_cast<size_t>( size ), outBytes.data() + headerSize, outBytes.size() - static_cast<size_t>( headerSize ), compressedSize,
                               settings._level ) == false ||
             static_cast<int64>( compressedSize ) + headerSize >= static_cast<int64>( size ) )
        {
            outBytes.clear();
            return false; // 줄지 않았다 — 원문을 보낸다
        }
        outBytes.resize( static_cast<size_t>( headerSize ) + compressedSize );
        return true;
    }

    bool NetCompressionUtil::decompressEnvelope( const uint8* pData, int32 size, int32 maxRawSize, vector<uint8>& outBytes, CompressionCodecRegistry* pRegistry )
    {
        outBytes.clear();
        if ( pData == nullptr || size < 2 )
            return false;
        uint32      rawSize   = 0;
        const int32 sizeBytes = NetCompressionInternal::readVarUint( pData + 1, size - 1, rawSize );
        if ( sizeBytes == 0 || rawSize == 0 || rawSize > static_cast<uint32>( maxRawSize ) )
            return false; // 폭탄 · 깨진 크기 — 코덱을 부르기 전에 끊는다
        if ( pData[0] == static_cast<uint8>( CompressionCodecType::None ) )
            return false;
        CompressionCodecRegistry* pResolved = NetCompressionInternal::resolveRegistry( pRegistry );
        ICompressionCodec*        pCodec    = pResolved != nullptr ? pResolved->getCodec( static_cast<CompressionCodecType>( pData[0] ) ) : nullptr;
        if ( pCodec == nullptr )
            return false;
        outBytes.resize( rawSize );
        size_t      written    = 0;
        const int32 headerSize = 1 + sizeBytes;
        if ( pCodec->decompress( pData + headerSize, static_cast<size_t>( size - headerSize ), outBytes.data(), outBytes.size(), written ) == false || written != rawSize )
        {
            outBytes.clear();
            return false;
        }
        return true;
    }
} // namespace sw
