#include "pch.h"

#include "Engine/Compression/ZlibCompressionCodec.h"

#include "Core/Log/Logger.h"

#include <zlib.h>

namespace sw
{
    namespace
    {
        // zlib 의 길이 타입은 `uLong`(= unsigned long) 이라 **Windows 에서 32비트다.** 이보다 큰
        // 크기를 그대로 캐스팅하면 조용히 잘린 값이 들어가고, compress2 는 그 잘린 만큼만 압축한
        // 뒤 Z_OK 를 돌려준다 — 성공을 보고하면서 데이터를 버리는 셈이다. 입구에서 막는다.
        constexpr size_t kZlibMaxSize = static_cast<size_t>( static_cast<uLong>( -1 ) );

        /** @brief zlib 의 길이 타입에 담기는 크기인지 봅니다. */
        bool isZlibRepresentableSize( size_t byteSize )
        {
            return byteSize <= kZlibMaxSize;
        }
    } // namespace
} // namespace sw

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
        // 담기지 않는 크기에는 0 을 준다 — 잘린 크기의 한계를 돌려주면 호출자가 그것을 믿는다.
        if ( isZlibRepresentableSize( uncompressedSize ) == false )
            return 0;
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
        if ( isZlibRepresentableSize( srcSize ) == false || isZlibRepresentableSize( dstCapacity ) == false )
        {
            SW_LOG_ERROR( "zlib 입력이 uLong 한계를 넘었습니다 (src %# / dst %# bytes > %#).",
                          static_cast<uint64>( srcSize ), static_cast<uint64>( dstCapacity ),
                          static_cast<uint64>( kZlibMaxSize ) );
            return false;
        }

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
        if ( isZlibRepresentableSize( srcSize ) == false || isZlibRepresentableSize( dstCapacity ) == false )
        {
            SW_LOG_ERROR( "zlib 해제 입력이 uLong 한계를 넘었습니다 (src %# / dst %# bytes > %#).",
                          static_cast<uint64>( srcSize ), static_cast<uint64>( dstCapacity ),
                          static_cast<uint64>( kZlibMaxSize ) );
            return false;
        }

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
