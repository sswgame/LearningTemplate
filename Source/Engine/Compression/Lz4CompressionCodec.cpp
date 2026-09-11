#include "pch.h"

#include "Engine/Compression/Lz4CompressionCodec.h"

#include "Core/Log/Logger.h"

#include <lz4.h>
#include <lz4hc.h>

namespace sw
{
    SW_LOG_CALLER( "Lz4Codec" );

    CompressionCodecType Lz4CompressionCodec::getCodecType() const
    {
        return CompressionCodecType::LZ4;
    }

    const utf8* Lz4CompressionCodec::getCodecName() const
    {
        return "LZ4";
    }

    size_t Lz4CompressionCodec::compressBound( size_t uncompressedSize ) const
    {
        // LZ4 는 int32 크기까지만 다룬다. 넘으면 0 을 돌려주므로 그대로 전달한다.
        if ( uncompressedSize > static_cast<size_t>( LZ4_MAX_INPUT_SIZE ) )
            return 0;
        return static_cast<size_t>( LZ4_compressBound( static_cast<int32>( uncompressedSize ) ) );
    }

    bool Lz4CompressionCodec::compress( const void* pSrc,
                                        size_t      srcSize,
                                        void*       pDst,
                                        size_t      dstCapacity,
                                        size_t&     outCompressedSize,
                                        int32       compressionLevel )
    {
        outCompressedSize = 0;
        if ( pSrc == nullptr || pDst == nullptr || srcSize == 0 )
            return false;
        if ( srcSize > static_cast<size_t>( LZ4_MAX_INPUT_SIZE ) )
        {
            SW_LOG_ERROR( "LZ4 입력이 한계를 넘었습니다 (%# bytes > %# ).",
                          static_cast<uint64>( srcSize ), static_cast<uint64>( LZ4_MAX_INPUT_SIZE ) );
            return false;
        }

        const utf8* pSrcBytes = static_cast<const utf8*>( pSrc );
        utf8*       pDstBytes = static_cast<utf8*>( pDst );
        const int32 srcBytes  = static_cast<int32>( srcSize );
        const int32 dstBytes  = static_cast<int32>( dstCapacity );

        // level 0 = 기본 속도 경로, 1 이상 = HC(고압축). 해제 속도는 둘이 같다.
        int32 written{ 0 };
        if ( compressionLevel > 0 )
            written = LZ4_compress_HC( pSrcBytes, pDstBytes, srcBytes, dstBytes, compressionLevel );
        else
            written = LZ4_compress_default( pSrcBytes, pDstBytes, srcBytes, dstBytes );

        if ( written <= 0 )
            return false;

        outCompressedSize = static_cast<size_t>( written );
        return true;
    }

    bool Lz4CompressionCodec::decompress( const void* pSrc,
                                          size_t      srcSize,
                                          void*       pDst,
                                          size_t      dstCapacity,
                                          size_t&     outUncompressedSize )
    {
        outUncompressedSize = 0;
        if ( pSrc == nullptr || pDst == nullptr || srcSize == 0 )
            return false;

        // **`_safe` 를 쓴다.** 입력 크기를 믿고 읽는 변형(`LZ4_decompress_fast`)은 손상된 데이터에
        // 대해 대상 버퍼 밖으로 쓴다 — 팩·세이브는 외부에서 오는 바이트다.
        const int32 written = LZ4_decompress_safe( static_cast<const utf8*>( pSrc ), static_cast<utf8*>( pDst ),
                                                   static_cast<int32>( srcSize ), static_cast<int32>( dstCapacity ) );
        if ( written < 0 )
        {
            SW_LOG_ERROR( "LZ4 해제 실패 (src %# → dst %# bytes).",
                          static_cast<uint64>( srcSize ), static_cast<uint64>( dstCapacity ) );
            return false;
        }

        outUncompressedSize = static_cast<size_t>( written );
        return true;
    }
} // namespace sw
