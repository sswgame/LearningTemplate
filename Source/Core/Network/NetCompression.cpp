#include "pch.h"

#include "Core/Network/NetCompression.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Container/VarIntUtil.h"

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
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool NetCompressionUtil::compressEnvelope( const NetCompressionConfig& settings, const uint8* pData, int32 size, vector<uint8>& outBytes,
                                               CompressionCodecRegistry* pRegistry )
    {
        outBytes.clear();
        const bool bCompressionSkipped = settings._codec == CompressionCodecType::None || size < settings._minInputBytes || size <= 0;
        if ( bCompressionSkipped )
            return false;
        CompressionCodecRegistry* pResolved = NetCompressionInternal::resolveRegistry( pRegistry );
        ICompressionCodec*        pCodec    = pResolved != nullptr ? pResolved->getCodec( settings._codec ) : nullptr;
        if ( pCodec == nullptr )
            return false;
        outBytes.clear();
        outBytes.push_back( static_cast<uint8>( settings._codec ) );
        const int32 headerSize = 1 + static_cast<int32>( VarIntUtil::encodeVarUint64( static_cast<uint64>( size ), outBytes ) );
        outBytes.resize( static_cast<size_t>( kMaxEnvelopeHeaderSize ) + pCodec->compressBound( static_cast<size_t>( size ) ) );
        size_t compressedSize = 0;
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
        size_t      sizeEnd                  = 1;
        uint64      rawSize64                = 0;
        uint32      rawSize                  = 0;
        const bool  bReadSize                = VarIntUtil::decodeVarUint64( pData, static_cast<size_t>( size ), sizeEnd, rawSize64 ) && VarIntUtil::narrowToUint32( rawSize64, rawSize );
        const int32 sizeBytes                = static_cast<int32>( sizeEnd ) - 1;
        const bool  bInvalidCompressedHeader = bReadSize == false || rawSize == 0 || rawSize > static_cast<uint32>( maxRawSize );
        if ( bInvalidCompressedHeader )
            return false; // 폭탄 · 깨진 크기 — 코덱을 부르기 전에 끊는다
        if ( pData[0] == static_cast<uint8>( CompressionCodecType::None ) )
            return false;
        CompressionCodecRegistry* pResolved = NetCompressionInternal::resolveRegistry( pRegistry );
        ICompressionCodec*        pCodec    = pResolved != nullptr ? pResolved->getCodec( static_cast<CompressionCodecType>( pData[0] ) ) : nullptr;
        if ( pCodec == nullptr )
            return false;
        if ( rawSize > pCodec->decompressBound( static_cast<size_t>( size - 1 - sizeBytes ) ) )
            return false; // 압축 바이트로는 나올 수 없는 원본 크기 — resize 전에 끊는다(B-5)
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
