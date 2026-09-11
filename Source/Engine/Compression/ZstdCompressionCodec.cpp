#include "pch.h"

#include "Engine/Compression/ZstdCompressionCodec.h"

#include "Core/Log/Logger.h"

#include <zstd.h>

namespace sw
{
    SW_LOG_CALLER( "ZstdCodec" );

    CompressionCodecType ZstdCompressionCodec::getCodecType() const
    {
        return CompressionCodecType::Zstd;
    }

    const utf8* ZstdCompressionCodec::getCodecName() const
    {
        return "Zstd";
    }

    size_t ZstdCompressionCodec::compressBound( size_t uncompressedSize ) const
    {
        return ZSTD_compressBound( uncompressedSize );
    }

    bool ZstdCompressionCodec::compress( const void* pSrc,
                                         size_t      srcSize,
                                         void*       pDst,
                                         size_t      dstCapacity,
                                         size_t&     outCompressedSize,
                                         int32       compressionLevel )
    {
        outCompressedSize = 0;
        if ( pSrc == nullptr || pDst == nullptr || srcSize == 0 )
            return false;

        const int32  level  = ( compressionLevel != 0 ) ? compressionLevel : ZSTD_defaultCLevel();
        const size_t result = ZSTD_compress( pDst, dstCapacity, pSrc, srcSize, level );
        if ( ZSTD_isError( result ) != 0 )
        {
            SW_LOG_ERROR( "zstd 압축 실패: %# (src %# bytes, level %#)", ZSTD_getErrorName( result ),
                          static_cast<uint64>( srcSize ), level );
            return false;
        }

        outCompressedSize = result;
        return true;
    }

    bool ZstdCompressionCodec::decompress( const void* pSrc,
                                           size_t      srcSize,
                                           void*       pDst,
                                           size_t      dstCapacity,
                                           size_t&     outUncompressedSize )
    {
        outUncompressedSize = 0;
        if ( pSrc == nullptr || pDst == nullptr || srcSize == 0 )
            return false;

        // ZSTD_decompress 는 프레임 헤더의 크기를 보고 대상 용량을 스스로 검사한다 — 손상된 입력이
        // 버퍼 밖으로 쓰지 않는다. 실패는 에러 코드로 돌아온다.
        const size_t result = ZSTD_decompress( pDst, dstCapacity, pSrc, srcSize );
        if ( ZSTD_isError( result ) != 0 )
        {
            SW_LOG_ERROR( "zstd 해제 실패: %# (src %# → dst %# bytes)", ZSTD_getErrorName( result ),
                          static_cast<uint64>( srcSize ), static_cast<uint64>( dstCapacity ) );
            return false;
        }

        outUncompressedSize = result;
        return true;
    }
} // namespace sw
