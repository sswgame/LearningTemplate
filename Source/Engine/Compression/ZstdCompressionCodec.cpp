#include "pch.h"

#include "Engine/Compression/ZstdCompressionCodec.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

// ZSTD_customMem · ZSTD_createCCtx_advanced · ZSTD_createDCtx_advanced 는 zstd.h 의 정적 링크 전용 절에 있다(공유 라이브러리도 내보낸다).

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

namespace sw
{
    namespace
    {
        struct ZstdCompressionCodecInternal
        {
            static void* allocate( void* /*pOpaque*/, size_t size ) { return Memory::allocate( size ); }
            static void  free( void* /*pOpaque*/, void* pAddress ) { Memory::free( pAddress ); }

            /** @brief 압축 · 해제 문맥의 작업 공간을 sw 할당자로 잡게 하는 zstd 할당 함수 묶음입니다. */
            static constexpr ZSTD_customMem kSwMemory{ &allocate, &free, nullptr };
        };
    } // namespace
} // namespace sw

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

    size_t ZstdCompressionCodec::decompressBound( size_t compressedSize ) const
    {
        // 블록 하나는 많아야 ZSTD_BLOCKSIZE_MAX(128 KB)를 내고, 가장 작은 블록(헤더 3 바이트 + RLE 값 1 바이트)도 4 바이트다.
        // 프레임 헤더 · 건너뛰는 프레임은 아무것도 내지 않으므로 4 바이트마다 블록 하나로 잡으면 넉넉한 상한이다.
        constexpr size_t kMinBlockBytes = 4;
        return ( compressedSize / kMinBlockBytes ) * static_cast<size_t>( ZSTD_BLOCKSIZE_MAX );
    }

    bool ZstdCompressionCodec::compress( const void* pSrc,
                                         size_t      srcSize,
                                         void*       pDst,
                                         size_t      dstCapacity,
                                         size_t&     outCompressedSize,
                                         int32       compressionLevel )
    {
        outCompressedSize                = 0;
        const bool bInvalidCompressInput = pSrc == nullptr || pDst == nullptr || srcSize == 0;
        if ( bInvalidCompressInput )
            return false;

        const int32 level    = ( compressionLevel != 0 ) ? compressionLevel : ZSTD_defaultCLevel();
        ZSTD_CCtx*  pContext = ZSTD_createCCtx_advanced( ZstdCompressionCodecInternal::kSwMemory );
        if ( pContext == nullptr )
        {
            SW_LOG_ERROR( "zstd 압축 문맥을 만들지 못했습니다 (src %# bytes)", static_cast<uint64>( srcSize ) );
            return false;
        }
        const size_t result = ZSTD_compressCCtx( pContext, pDst, dstCapacity, pSrc, srcSize, level );
        ZSTD_freeCCtx( pContext );
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
        outUncompressedSize                = 0;
        const bool bInvalidDecompressInput = pSrc == nullptr || pDst == nullptr || srcSize == 0;
        if ( bInvalidDecompressInput )
            return false;

        // ZSTD_decompressDCtx 는 프레임 헤더의 크기를 보고 대상 용량을 스스로 검사한다. 손상된 입력이
        // 버퍼 밖으로 쓰지 않는다. 실패는 에러 코드로 돌아온다.
        ZSTD_DCtx* pContext = ZSTD_createDCtx_advanced( ZstdCompressionCodecInternal::kSwMemory );
        if ( pContext == nullptr )
        {
            SW_LOG_ERROR( "zstd 해제 문맥을 만들지 못했습니다 (src %# bytes)", static_cast<uint64>( srcSize ) );
            return false;
        }
        const size_t result = ZSTD_decompressDCtx( pContext, pDst, dstCapacity, pSrc, srcSize );
        ZSTD_freeDCtx( pContext );
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
