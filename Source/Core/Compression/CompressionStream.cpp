#include "pch.h"

#include "Core/Compression/CompressionStream.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/NullCompressionCodec.h"
#include "Core/Compression/RleCompressionCodec.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        /** @brief 레지스트리가 없을 때 쓰는 내장 코덱 — Core 만 링크하는 도구 경로용. */
        NullCompressionCodec s_nullCodec;
        RleCompressionCodec  s_rleCodec;

        struct CompressionStreamInternal
        {
            /**
             * @brief 코덱을 고릅니다 — 넘겨받은 레지스트리, 없으면 **바인딩된 활성 레지스트리**.
             * @details 예전에는 `pRegistry` 가 널이면 곧장 하드코딩 코덱으로 갔다. 그런데 넘기는 호출부가
             *          하나도 없어서(레지스트리를 엔진이 들고 있었고 Core 는 거기 닿지 못한다) **항상**
             *          하드코딩으로 갔고, 등록한 코덱은 쓰이지 않았다. 이제 활성 레지스트리를 본다.
             *
             *          **못 찾으면 nullptr 이다.** 예전에는 마지막에 무조건 Null 코덱을 돌려줬는데, 그
             *          한 줄이 호출부의 오류 처리를 전부 죽은 코드로 만들었다. 결과가 둘이었다:
             *          (1) `compressBuffer( …, Zstd )` 를 Zstd 없이 부르면 헤더에는 `Zstd` 라고 적고
             *              페이로드는 **무압축**으로 썼다 — Zstd 가 등록된 다른 기계가 그 스트림을 읽으면
             *              쓰레기가 나온다.
             *          (2) 모르는 `_codecType` 이 든 스트림을 "해제" 해 버렸다. 체크섬 플래그가 꺼진
             *              스트림이면 그 쓰레기가 **성공으로** 돌아갔다.
             *          내장 코덱은 자기가 실제로 구현하는 둘(None · RLE)에만 물러난다.
             */
            static ICompressionCodec* findCodec( CompressionCodecType type, const CompressionCodecRegistry* pRegistry )
            {
                if ( pRegistry == nullptr )
                    pRegistry = CompressionCodecRegistry::getActive();

                if ( pRegistry != nullptr )
                {
                    if ( ICompressionCodec* pCodec = pRegistry->getCodec( type ) )
                        return pCodec;
                }

                if ( type == CompressionCodecType::None )
                    return &s_nullCodec;
                if ( type == CompressionCodecType::RLE )
                    return &s_rleCodec;

                return nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "CompressionStream" );

    uint32 CompressionStream::calculateChecksum( const void* pData, size_t dataSize )
    {
        if ( pData == nullptr || dataSize == 0 )
            return 0;

        const auto* const pBytes = static_cast<const uint8*>( pData );
        uint32            hash   = 2166136261u;
        for ( size_t byteIndex = 0; byteIndex < dataSize; ++byteIndex )
        {
            hash ^= pBytes[byteIndex];
            hash *= 16777619u;
        }
        return hash;
    }

    bool CompressionStream::verifyHeader( const void* pData, size_t dataSize, CompressionHeader& outHeader )
    {
        if ( pData == nullptr || dataSize < sizeof( CompressionHeader ) )
            return false;

        Memory::copy( &outHeader, pData, sizeof( CompressionHeader ) );
        if ( outHeader._magic != CompressionHeader::kMagic )
            return false;

        if ( outHeader._version != CompressionHeader::kVersion )
            return false;

        if ( outHeader._compressedSize + sizeof( CompressionHeader ) > dataSize )
            return false;

        return true;
    }

    bool CompressionStream::compressBuffer( const void*                     pSrc,
                                            size_t                          srcSize,
                                            vector<uint8>&                  outBytes,
                                            CompressionCodecType            codecType,
                                            int32                           compressionLevel,
                                            const CompressionCodecRegistry* pRegistry )
    {
        outBytes.clear();
        if ( pSrc == nullptr || srcSize == 0 )
            return true;

        ICompressionCodec* pCodec = CompressionStreamInternal::findCodec( codecType, pRegistry );
        if ( pCodec == nullptr )
        {
            SW_LOG_WARNING( "Requested codec %# not found, falling back to Null codec", static_cast<uint8>( codecType ) );
            pCodec = CompressionStreamInternal::findCodec( CompressionCodecType::None, pRegistry );
            if ( pCodec == nullptr )
                return false;
            codecType = CompressionCodecType::None;
        }

        const size_t bound = pCodec->compressBound( srcSize );
        outBytes.resize( sizeof( CompressionHeader ) + bound );

        auto* const pHeader        = reinterpret_cast<CompressionHeader*>( outBytes.data() );
        pHeader->_magic            = CompressionHeader::kMagic;
        pHeader->_version          = CompressionHeader::kVersion;
        pHeader->_codecType        = codecType;
        pHeader->_flags            = CompressionHeader::kFlagChecksum;
        pHeader->_uncompressedSize = static_cast<uint64>( srcSize );
        pHeader->_checksum         = calculateChecksum( pSrc, srcSize );

        uint8* const pDstPayload    = outBytes.data() + sizeof( CompressionHeader );
        size_t       compressedSize = 0;

        const bool bSuccess = pCodec->compress( pSrc, srcSize, pDstPayload, bound, compressedSize, compressionLevel );
        if ( bSuccess == false )
        {
            outBytes.clear();
            return false;
        }

        pHeader->_compressedSize = static_cast<uint64>( compressedSize );
        outBytes.resize( sizeof( CompressionHeader ) + compressedSize );
        return true;
    }

    bool CompressionStream::decompressBuffer( const void*                     pSrc,
                                              size_t                          srcSize,
                                              vector<uint8>&                  outBytes,
                                              const CompressionCodecRegistry* pRegistry )
    {
        outBytes.clear();
        if ( pSrc == nullptr || srcSize == 0 )
            return true;

        CompressionHeader header{};
        if ( verifyHeader( pSrc, srcSize, header ) == false )
        {
            SW_LOG_ERROR( "Invalid compression header (size=%#)", srcSize );
            return false;
        }

        if ( header._uncompressedSize == 0 )
            return true;

        outBytes.resize( static_cast<size_t>( header._uncompressedSize ) );

        size_t     outUncompressedSize = 0;
        const bool bSuccess            = decompressBuffer( pSrc, srcSize, outBytes.data(), outBytes.size(), outUncompressedSize, pRegistry );
        if ( bSuccess == false || outUncompressedSize != static_cast<size_t>( header._uncompressedSize ) )
        {
            outBytes.clear();
            return false;
        }

        return true;
    }

    bool CompressionStream::decompressBuffer( const void*                     pSrc,
                                              size_t                          srcSize,
                                              void*                           pDst,
                                              size_t                          dstCapacity,
                                              size_t&                         outUncompressedSize,
                                              const CompressionCodecRegistry* pRegistry )
    {
        outUncompressedSize = 0;
        if ( pSrc == nullptr || srcSize == 0 )
            return true;

        if ( pDst == nullptr )
            return false;

        CompressionHeader header{};
        if ( verifyHeader( pSrc, srcSize, header ) == false )
            return false;

        if ( dstCapacity < static_cast<size_t>( header._uncompressedSize ) )
            return false;

        ICompressionCodec* pCodec = CompressionStreamInternal::findCodec( header._codecType, pRegistry );
        if ( pCodec == nullptr )
        {
            SW_LOG_ERROR( "Unsupported codec type in stream: %#", static_cast<uint32>( header._codecType ) );
            return false;
        }

        const uint8* const pSrcPayload    = static_cast<const uint8*>( pSrc ) + sizeof( CompressionHeader );
        const size_t       srcPayloadSize = static_cast<size_t>( header._compressedSize );

        const bool bSuccess = pCodec->decompress( pSrcPayload, srcPayloadSize, pDst, dstCapacity, outUncompressedSize );
        if ( bSuccess == false )
        {
            SW_LOG_ERROR( "Decompression failed using codec: %#", pCodec->getCodecName() );
            return false;
        }

        if ( ( header._flags & CompressionHeader::kFlagChecksum ) != 0 )
        {
            const uint32 calculatedChecksum = calculateChecksum( pDst, outUncompressedSize );
            if ( calculatedChecksum != header._checksum )
            {
                SW_LOG_ERROR( "Checksum mismatch: expected %x, got %x", header._checksum, calculatedChecksum );
                return false;
            }
        }

        return true;
    }
} // namespace sw
