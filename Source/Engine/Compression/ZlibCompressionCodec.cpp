#include "pch.h"

#include "Engine/Compression/ZlibCompressionCodec.h"

#include "Core/Log/Logger.h"

#include <zlib.h>

namespace sw
{
    SW_LOG_CALLER( "ZlibCodec" );

    CompressionCodecType ZlibCompressionCodec::getCodecType() const
    {
        return CompressionCodecType::Zlib;
    }

    const utf8* ZlibCompressionCodec::getCodecName() const
    {
        return "Zlib";
    }

    size_t ZlibCompressionCodec::compressBound( size_t uncompressedSize ) const
    {
        return static_cast<size_t>( ::compressBound( static_cast<uLong>( uncompressedSize ) ) );
    }

    bool ZlibCompressionCodec::compress( const void* pSrc,
                                         size_t      srcSize,
                                         void*       pDst,
                                         size_t      dstCapacity,
                                         size_t&     outCompressedSize,
                                         int32       compressionLevel )
    {
        outCompressedSize = 0;
        if ( pSrc == nullptr || pDst == nullptr || srcSize == 0 )
            return false;

        // 레벨 0 은 zlib 에서 "무압축" 이라 의미가 다르다 — 우리 계약의 0 은 "기본" 이므로 9 로 읽는다
        // (굽는 쪽 파이썬도 level=9 로 쓴다).
        const int32 level = ( compressionLevel != 0 ) ? compressionLevel : Z_BEST_COMPRESSION;

        uLongf      destLen = static_cast<uLongf>( dstCapacity );
        const int32 result  = compress2( static_cast<Bytef*>( pDst ), &destLen,
                                         reinterpret_cast<const Bytef*>( pSrc ), static_cast<uLong>( srcSize ), level );
        if ( result != Z_OK )
        {
            SW_LOG_ERROR( "zlib compress2 실패 (code %#, src %# bytes)", result, static_cast<uint64>( srcSize ) );
            return false;
        }

        outCompressedSize = static_cast<size_t>( destLen );
        return true;
    }

    bool ZlibCompressionCodec::decompress( const void* pSrc,
                                           size_t      srcSize,
                                           void*       pDst,
                                           size_t      dstCapacity,
                                           size_t&     outUncompressedSize )
    {
        outUncompressedSize = 0;
        if ( pSrc == nullptr || pDst == nullptr || srcSize == 0 )
            return false;

        uLongf      destLen = static_cast<uLongf>( dstCapacity );
        const int32 result  = uncompress( static_cast<Bytef*>( pDst ), &destLen,
                                          reinterpret_cast<const Bytef*>( pSrc ), static_cast<uLong>( srcSize ) );
        if ( result != Z_OK )
        {
            SW_LOG_ERROR( "zlib uncompress 실패 (code %#, src %# → dst %# bytes)",
                          result, static_cast<uint32>( srcSize ), static_cast<uint32>( dstCapacity ) );
            return false;
        }

        outUncompressedSize = static_cast<size_t>( destLen );
        return true;
    }
} // namespace sw
