#include "pch.h"

#include "Engine/Compression/ZlibCompressionCodec.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include <zlib.h>

namespace sw
{
    namespace
    {
        /**
         * @brief zlib 스트림을 sw 할당자로 돌리는 도우미입니다. `compress2` · `uncompress` 는 스트림의 할당 함수를 고를 수 없어(CRT malloc 고정)
         *        같은 일을 `z_stream` 으로 직접 합니다. 반환 코드와 출력 크기는 두 함수와 같습니다.
         */
        struct ZlibCompressionCodecInternal
        {
            static voidpf allocate( voidpf /*pOpaque*/, uInt itemCount, uInt itemSize )
            {
                return Memory::allocate( static_cast<size_t>( itemCount ) * static_cast<size_t>( itemSize ) );
            }

            static void free( voidpf /*pOpaque*/, voidpf pAddress ) { Memory::free( pAddress ); }

            /** @brief 할당 함수만 채운 빈 스트림입니다. */
            static z_stream makeStream()
            {
                z_stream stream{};
                stream.zalloc = &allocate;
                stream.zfree  = &free;
                stream.opaque = Z_NULL;
                return stream;
            }

            /** @brief 남은 바이트를 `uInt` 한 칸(avail_in · avail_out)에 담을 만큼 떼어 줍니다. 4 GB 를 넘는 버퍼는 여러 번에 나눠 넘깁니다. */
            static uInt takeChunk( size_t& inoutLeft )
            {
                const size_t chunk = MathUtil::min( inoutLeft, static_cast<size_t>( static_cast<uInt>( -1 ) ) );
                inoutLeft -= chunk;
                return static_cast<uInt>( chunk );
            }

            /** @brief `compress2` 와 같은 일을 합니다(반환 코드 · 출력 크기 포함). */
            static int32 deflateBuffer( Bytef* pDst, size_t dstCapacity, size_t& outWritten, const Bytef* pSrc, size_t srcSize, int32 level )
            {
                outWritten      = 0;
                z_stream stream = makeStream();
                int32    result = deflateInit( &stream, level );
                if ( result != Z_OK )
                    return result;

                size_t dstLeft  = dstCapacity;
                size_t srcLeft  = srcSize;
                stream.next_out = pDst;
                stream.next_in  = const_cast<Bytef*>( pSrc );
                do
                {
                    if ( stream.avail_out == 0 )
                        stream.avail_out = takeChunk( dstLeft );
                    if ( stream.avail_in == 0 )
                        stream.avail_in = takeChunk( srcLeft );
                    result = deflate( &stream, srcLeft > 0 ? Z_NO_FLUSH : Z_FINISH );
                } while ( result == Z_OK );

                outWritten = static_cast<size_t>( stream.next_out - pDst );
                deflateEnd( &stream );
                return result == Z_STREAM_END ? Z_OK : result;
            }

            /** @brief `uncompress` 와 같은 일을 합니다(반환 코드 · 출력 크기 포함). */
            static int32 inflateBuffer( Bytef* pDst, size_t dstCapacity, size_t& outWritten, const Bytef* pSrc, size_t srcSize )
            {
                outWritten      = 0;
                z_stream stream = makeStream();
                int32    result = inflateInit( &stream );
                if ( result != Z_OK )
                    return result;

                size_t dstLeft  = dstCapacity;
                size_t srcLeft  = srcSize;
                stream.next_out = pDst;
                stream.next_in  = const_cast<Bytef*>( pSrc );
                do
                {
                    if ( stream.avail_out == 0 )
                        stream.avail_out = takeChunk( dstLeft );
                    if ( stream.avail_in == 0 )
                        stream.avail_in = takeChunk( srcLeft );
                    result = inflate( &stream, Z_NO_FLUSH );
                } while ( result == Z_OK );

                outWritten = static_cast<size_t>( stream.next_out - pDst );
                inflateEnd( &stream );
                if ( result == Z_STREAM_END )
                    return Z_OK;
                if ( result == Z_NEED_DICT )
                    return Z_DATA_ERROR;
                // 출력 칸이 남았는데 더 나아가지 못했으면 입력이 잘린 것이다(대상이 작은 것과 구별한다).
                const bool bOutputLeft = dstLeft + stream.avail_out > 0;
                if ( result == Z_BUF_ERROR && bOutputLeft )
                    return Z_DATA_ERROR;
                return result;
            }
        };

        // zlib 의 길이 타입은 `uLong`(= unsigned long) 이라 **Windows 에서 32비트다.** 이보다 큰
        // 크기를 그대로 캐스팅하면 조용히 잘린 값이 들어가고, compress2 는 그 잘린 만큼만 압축한
        // 뒤 Z_OK 를 반환한다. 성공을 보고하면서 데이터를 버리는 셈이다. 입구에서 막는다.
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
        // 담기지 않는 크기에는 0 을 준다. 잘린 크기의 한계를 반환하면 부르는 쪽이 그것을 믿는다.
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

        // 레벨 0 은 zlib 에서 "무압축" 이라 의미가 다르다. 우리 계약의 0 은 "기본" 이므로 9 로 읽는다
        // (쿠킹하는 쪽 파이썬도 level=9 로 쓴다).
        const int32 level = ( compressionLevel != 0 ) ? compressionLevel : Z_BEST_COMPRESSION;

        size_t      written{ 0 };
        const int32 result = ZlibCompressionCodecInternal::deflateBuffer( static_cast<Bytef*>( pDst ), dstCapacity, written,
                                                                          static_cast<const Bytef*>( pSrc ), srcSize, level );
        if ( result != Z_OK )
        {
            SW_LOG_ERROR( "zlib deflate 실패 (code %#, src %# bytes)", result, static_cast<uint64>( srcSize ) );
            return false;
        }

        outCompressedSize = written;
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

        size_t      written{ 0 };
        const int32 result = ZlibCompressionCodecInternal::inflateBuffer( static_cast<Bytef*>( pDst ), dstCapacity, written,
                                                                          static_cast<const Bytef*>( pSrc ), srcSize );
        if ( result != Z_OK )
        {
            SW_LOG_ERROR( "zlib inflate 실패 (code %#, src %# → dst %# bytes)",
                          result, static_cast<uint64>( srcSize ), static_cast<uint64>( dstCapacity ) );
            return false;
        }

        outUncompressedSize = written;
        return true;
    }
} // namespace sw
