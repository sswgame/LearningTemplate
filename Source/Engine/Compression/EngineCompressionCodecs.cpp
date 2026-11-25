#include "pch.h"

#include "Engine/Compression/EngineCompressionCodecs.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryBlock.h"

// 코덱 셋을 한 TU 에 둔다(Godot `core/io/compression.cpp` 와 같은 자리) — 외부 라이브러리 헤더는 이 파일만 include 한다.
// ZSTD_customMem · ZSTD_createCCtx_advanced · ZSTD_createDCtx_advanced 는 zstd.h 의 정적 링크 전용 절에 있다(공유 라이브러리도 내보낸다).
#define ZSTD_STATIC_LINKING_ONLY
#include <lz4.h>
#include <lz4hc.h>
#include <zlib.h>
#include <zstd.h>

namespace sw
{
    vector<Subscription> EngineCompressionCodecUtil::registerAll( CompressionCodecRegistry& inoutRegistry )
    {
        vector<Subscription> listSubscription;
        listSubscription.push_back( inoutRegistry.registerCodec( make_unique<LZ4CompressionCodec>() ) );
        listSubscription.push_back( inoutRegistry.registerCodec( make_unique<ZstdCompressionCodec>() ) );
        listSubscription.push_back( inoutRegistry.registerCodec( make_unique<ZlibCompressionCodec>() ) );
        return listSubscription;
    }
} // namespace sw

namespace sw
{
    CompressionCodecKind LZ4CompressionCodec::getCodecType() const
    {
        return CompressionCodecKind::LZ4;
    }

    const utf8* LZ4CompressionCodec::getCodecName() const
    {
        return "LZ4";
    }

    size_t LZ4CompressionCodec::compressBound( size_t uncompressedSize ) const
    {
        // LZ4 는 int32 크기까지만 다룬다. 넘으면 0 을 돌려주므로 그대로 전달한다.
        if ( uncompressedSize > static_cast<size_t>( LZ4_MAX_INPUT_SIZE ) )
            return 0;
        return static_cast<size_t>( LZ4_compressBound( static_cast<int32>( uncompressedSize ) ) );
    }

    size_t LZ4CompressionCodec::decompressBound( size_t compressedSize ) const
    {
        // LZ4 블록에서 가장 많이 불어나는 것은 매치 길이의 덧붙임 바이트로, 한 바이트가 255 바이트를 더한다. 토큰 · 오프셋(3 바이트)은
        // 최대 19 바이트, 리터럴은 1 대 1 이므로 어느 바이트도 255 배를 넘지 못한다.
        constexpr size_t kMaxExpansionRatio = 255;
        return compressedSize * kMaxExpansionRatio;
    }

    bool LZ4CompressionCodec::compress( const void* pSrc,
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
        if ( srcSize > static_cast<size_t>( LZ4_MAX_INPUT_SIZE ) )
        {
            SW_LOG_ERROR_ID( "Lz4Codec.LZ4InputExceedsLimit", "LZ4 input exceeds the limit (%# bytes > %# ).",
                             static_cast<uint64>( srcSize ), static_cast<uint64>( LZ4_MAX_INPUT_SIZE ) );
            return false;
        }

        // 대상 용량은 한계까지만 알려 준다(위 decompress 와 같은 이유다).
        const utf8* pSrcBytes = static_cast<const utf8*>( pSrc );
        utf8*       pDstBytes = static_cast<utf8*>( pDst );
        const int32 srcBytes  = static_cast<int32>( srcSize );
        const int32 dstBytes  = static_cast<int32>( MathUtil::min( dstCapacity, static_cast<size_t>( LZ4_MAX_INPUT_SIZE ) ) );

        // level 0 = 기본 속도 경로, 1 이상 = HC(고압축). 해제 속도는 둘이 같다.
        int32 written{ 0 };
        if ( compressionLevel > 0 )
        {
            // HC 는 상태(~256 KB)를 힙에 둔다. LZ4_compress_HC 는 그것을 CRT malloc 으로 잡으므로 상태를 sw 할당자로 잡아 넘긴다.
            const MemoryBlock state = MemoryBlock::allocate( static_cast<size_t>( LZ4_sizeofStateHC() ) );
            if ( state.isValid() == false )
                return false;
            written = LZ4_compress_HC_extStateHC( state.get(), pSrcBytes, pDstBytes, srcBytes, dstBytes, compressionLevel );
        }
        else
            written = LZ4_compress_default( pSrcBytes, pDstBytes, srcBytes, dstBytes );

        if ( written <= 0 )
            return false;

        outCompressedSize = static_cast<size_t>( written );
        return true;
    }

    bool LZ4CompressionCodec::decompress( const void* pSrc,
                                          size_t      srcSize,
                                          void*       pDst,
                                          size_t      dstCapacity,
                                          size_t&     outUncompressedSize )
    {
        outUncompressedSize                = 0;
        const bool bInvalidDecompressInput = pSrc == nullptr || pDst == nullptr || srcSize == 0;
        if ( bInvalidDecompressInput )
            return false;

        // 입력 크기를 **여기서도** 본다. 외부에서 온 바이트를 먹는 쪽은 이쪽이다. 2GB 를 넘는 값을 int32 로 캐스팅하면 음수가 되어 LZ4 에 그대로
        // 들어가고, 그때 동작은 정의되어 있지 않다. (대상 용량은 아래에서 좁은 쪽으로 자른다.)
        if ( srcSize > static_cast<size_t>( LZ4_MAX_INPUT_SIZE ) )
        {
            SW_LOG_ERROR_ID( "Lz4Codec.LZ4DecompressionInputExceeds", "LZ4 decompression input exceeds the limit (%# bytes > %#).",
                             static_cast<uint64>( srcSize ), static_cast<uint64>( LZ4_MAX_INPUT_SIZE ) );
            return false;
        }

        // **`_safe` 를 쓴다.** 입력 크기를 믿고 읽는 변형(`LZ4_decompress_fast`)은 손상된 데이터에
        // 대해 대상 버퍼 밖으로 쓴다. 팩 · 세이브는 외부에서 오는 바이트다.
        // 대상 용량은 한계까지만 알려 준다. 실제 버퍼보다 좁게 보는 것은 안전한 방향이다
        // (넘치면 LZ4 가 실패로 끝낸다). 그냥 캐스팅하면 음수가 될 수 있다.
        const int32 dstBytes = static_cast<int32>( MathUtil::min( dstCapacity, static_cast<size_t>( LZ4_MAX_INPUT_SIZE ) ) );
        const int32 written  = LZ4_decompress_safe( static_cast<const utf8*>( pSrc ), static_cast<utf8*>( pDst ),
                                                    static_cast<int32>( srcSize ), dstBytes );
        if ( written < 0 )
        {
            SW_LOG_ERROR_ID( "Lz4Codec.LZ4DecompressionFailedSrc", "LZ4 decompression failed (src %# -> dst %# bytes).",
                             static_cast<uint64>( srcSize ), static_cast<uint64>( dstCapacity ) );
            return false;
        }

        outUncompressedSize = static_cast<size_t>( written );
        return true;
    }
} // namespace sw

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
    CompressionCodecKind ZstdCompressionCodec::getCodecType() const
    {
        return CompressionCodecKind::Zstd;
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
            SW_LOG_ERROR_ID( "ZstdCodec.FailedCreateZstdCompression", "Failed to create the zstd compression context (src %# bytes)", static_cast<uint64>( srcSize ) );
            return false;
        }
        const size_t result = ZSTD_compressCCtx( pContext, pDst, dstCapacity, pSrc, srcSize, level );
        ZSTD_freeCCtx( pContext );
        if ( ZSTD_isError( result ) != 0 )
        {
            SW_LOG_ERROR_ID( "ZstdCodec.ZstdCompressionFailedSrc", "zstd compression failed: %# (src %# bytes, level %#)", ZSTD_getErrorName( result ),
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
            SW_LOG_ERROR_ID( "ZstdCodec.FailedCreateZstdDecompression", "Failed to create the zstd decompression context (src %# bytes)", static_cast<uint64>( srcSize ) );
            return false;
        }
        const size_t result = ZSTD_decompressDCtx( pContext, pDst, dstCapacity, pSrc, srcSize );
        ZSTD_freeDCtx( pContext );
        if ( ZSTD_isError( result ) != 0 )
        {
            SW_LOG_ERROR_ID( "ZstdCodec.ZstdDecompressionFailedSrc", "zstd decompression failed: %# (src %# -> dst %# bytes)", ZSTD_getErrorName( result ),
                             static_cast<uint64>( srcSize ), static_cast<uint64>( dstCapacity ) );
            return false;
        }

        outUncompressedSize = result;
        return true;
    }
} // namespace sw

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
            static constexpr size_t kMaxChunkSize = static_cast<size_t>( static_cast<uInt>( -1 ) ); ///< `z_stream` 의 `avail_in` · `avail_out` 칸(uInt)에 들어가는 가장 큰 크기
            static voidpf           allocate( voidpf /*pOpaque*/, uInt itemCount, uInt itemSize )
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
                const size_t chunk = MathUtil::min( inoutLeft, kMaxChunkSize );
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
    CompressionCodecKind ZlibCompressionCodec::getCodecType() const
    {
        return CompressionCodecKind::Zlib;
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

    size_t ZlibCompressionCodec::decompressBound( size_t compressedSize ) const
    {
        // Deflate 의 이론상 최대 압축률은 1032 대 1 이다(zlib 기술 문서). zlib 머리 · 꼬리(6 바이트)는 아무것도 내지 않으므로 넉넉하다.
        constexpr size_t kMaxDeflateRatio = 1032;
        return compressedSize * kMaxDeflateRatio;
    }

    bool ZlibCompressionCodec::compress( const void* pSrc,
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
        if ( isZlibRepresentableSize( srcSize ) == false || isZlibRepresentableSize( dstCapacity ) == false )
        {
            SW_LOG_ERROR_ID( "ZlibCodec.ZlibInputExceedsULong", "zlib input exceeds the uLong limit (src %# / dst %# bytes > %#).",
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
            SW_LOG_ERROR_ID( "Compression.ZlibDeflateFailed", "zlib deflate failed (code %#, src %# bytes)", result, static_cast<uint64>( srcSize ) );
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
        outUncompressedSize                = 0;
        const bool bInvalidDecompressInput = pSrc == nullptr || pDst == nullptr || srcSize == 0;
        if ( bInvalidDecompressInput )
            return false;
        if ( isZlibRepresentableSize( srcSize ) == false || isZlibRepresentableSize( dstCapacity ) == false )
        {
            SW_LOG_ERROR_ID( "ZlibCodec.ZlibDecompressionInputExceeds", "zlib decompression input exceeds the uLong limit (src %# / dst %# bytes > %#).",
                             static_cast<uint64>( srcSize ), static_cast<uint64>( dstCapacity ),
                             static_cast<uint64>( kZlibMaxSize ) );
            return false;
        }

        size_t      written{ 0 };
        const int32 result = ZlibCompressionCodecInternal::inflateBuffer( static_cast<Bytef*>( pDst ), dstCapacity, written,
                                                                          static_cast<const Bytef*>( pSrc ), srcSize );
        if ( result != Z_OK )
        {
            SW_LOG_ERROR_ID( "Compression.ZlibInflateFailed", "zlib inflate failed (code %#, src %# -> dst %# bytes)",
                             result, static_cast<uint64>( srcSize ), static_cast<uint64>( dstCapacity ) );
            return false;
        }

        outUncompressedSize = written;
        return true;
    }
} // namespace sw
