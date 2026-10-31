#include "pch.h"

#include "Core/Compression/CompressionStream.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/NullCompressionCodec.h"
#include "Core/Compression/RleCompressionCodec.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        /** @brief 레지스트리가 없을 때 쓰는 내장 코덱입니다(Core 만 링크하는 도구 경로용). */
        NullCompressionCodec s_nullCodec;
        RleCompressionCodec  s_rleCodec;

        struct CompressionStreamInternal
        {
            /**
             * @brief 코덱을 고릅니다. 넘겨받은 레지스트리를 먼저 보고, 없으면 연결된 활성 레지스트리를 봅니다.
             * @details 넘겨받는 호출부는 드물고(레지스트리는 엔진이 들고 있고 Core 는 거기 닿지 못합니다), 대개 활성 레지스트리가 답합니다.
             *
             *          **찾지 못하면 nullptr 을 반환합니다.** Null 코덱으로 대신하면 호출부의 오류 처리가 모두 죽은 코드가 되어
             *          (1) Zstd 가 없는 상태에서 `compressBuffer( …, Zstd )` 가 헤더에는 `Zstd` 라고 적고 페이로드는 **압축하지 않은 채**
             *              쓰고(Zstd 가 등록된 다른 기계에서 읽으면 쓰레기가 나옵니다),
             *          (2) 알 수 없는 `_codecType` 이 든 스트림도 "해제" 해, 체크섬 플래그가 꺼진 스트림이면 그 쓰레기가 **성공으로** 반환됩니다.
             *          내장 코덱으로 대신하는 것은 실제로 구현된 두 가지(None · RLE)뿐입니다.
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

            /**
             * @brief 검증한 헤더가 말하는 코덱을 찾고, 그 코덱으로 그 원본 크기가 **나올 수 있는지** 봅니다. 아니면 nullptr 입니다.
             * @details 헤더의 `_uncompressedSize` 는 그대로 할당 크기가 됩니다. `kMaxUncompressedSize` 만으로는 81 바이트짜리 입력이
             *          620 MB 를 요구해도 통과했고, 그만큼 할당 · 0 채우기 · 비우기를 한 뒤에야 페이로드가 비었다는 것을 알았습니다
             *          (야간 퍼저 `Compression` 대상 10 초 타임아웃). 압축 크기에서 나올 수 있는 최대치는 코덱이 알므로
             *          (`ICompressionCodec::decompressBound`) 할당 **전에** 그것과 비교합니다. 두 해제 경로가 모두 여기를 지납니다.
             */
            static ICompressionCodec* findCodecForHeader( const CompressionHeader& header, const CompressionCodecRegistry* pRegistry )
            {
                ICompressionCodec* const pCodec = findCodec( header._codecType, pRegistry );
                if ( pCodec == nullptr )
                {
                    SW_LOG_ERROR_ID( "Compression.UnsupportedCodec", "Unsupported codec type in stream: %#", static_cast<uint32>( header._codecType ) );
                    return nullptr;
                }

                const uint64 maxUncompressedSize = static_cast<uint64>( pCodec->decompressBound( static_cast<size_t>( header._compressedSize ) ) );
                if ( header._uncompressedSize > maxUncompressedSize )
                {
                    SW_LOG_ERROR_ID( "Compression.SizeExceedsBound", "Stream claims %# bytes from %# compressed bytes, codec %# allows at most %#",
                                     header._uncompressedSize, header._compressedSize, pCodec->getCodecName(), maxUncompressedSize );
                    return nullptr;
                }
                return pCodec;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "CompressionStream" );

    uint32 CompressionStream::computeChecksum( const void* pData, size_t dataSize )
    {
        if ( pData == nullptr || dataSize == 0 )
            return 0;

        const auto* const pBytes = static_cast<const uint8*>( pData );
        return HashUtil::appendBytes32( HashUtil::kFNVOffset32, pBytes, dataSize );
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

        // 덧셈이 아니라 뺄셈으로 비교한다. 덧셈으로 쓰면 스트림에 적힌 크기가 클 때 오버플로가 난다.
        // `_compressedSize` 가 UINT64_MAX 면 `+28` 이 27 로 돌아 검사를 통과하고, 그 뒤 `decompress` 에 srcSize 로 SIZE_MAX 가
        // 그대로 들어가 코덱이 버퍼 밖을 읽는다. 위에서 `dataSize >= sizeof( CompressionHeader )` 를 이미 확인했으므로 이 뺄셈은 안전하다.
        if ( outHeader._compressedSize > dataSize - sizeof( CompressionHeader ) )
            return false;

        // 해제 후 크기도 여기서 확인한다. 이 값은 그대로 할당 크기가 되므로(vector 오버로드의 resize), 망가진 헤더가 2^60 을
        // 적어 두면 코덱이 한 바이트도 읽기 전에 메모리가 바닥난다.
        if ( outHeader._uncompressedSize > kMaxUncompressedSize )
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

        // 요청한 코덱이 없으면 다른 코덱으로 바꾸지 않고 실패한다 — 부른 쪽은 그 코덱으로 압축됐다고 믿는다.
        ICompressionCodec* pCodec = CompressionStreamInternal::findCodec( codecType, pRegistry );
        if ( pCodec == nullptr )
        {
            SW_LOG_ERROR_ID( "Compression.CodecNotRegistered", "Requested codec %# is not registered", static_cast<uint8>( codecType ) );
            return false;
        }

        const size_t bound = pCodec->compressBound( srcSize );
        outBytes.resize( sizeof( CompressionHeader ) + bound );

        auto* const pHeader        = reinterpret_cast<CompressionHeader*>( outBytes.data() );
        pHeader->_magic            = CompressionHeader::kMagic;
        pHeader->_version          = CompressionHeader::kVersion;
        pHeader->_codecType        = codecType;
        pHeader->_flags            = CompressionHeader::kFlagChecksum;
        pHeader->_uncompressedSize = static_cast<uint64>( srcSize );
        pHeader->_checksum         = computeChecksum( pSrc, srcSize );

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
            SW_LOG_ERROR_ID( "Compression.InvalidHeader", "Invalid compression header (size=%#)", srcSize );
            return false;
        }

        if ( header._uncompressedSize == 0 )
            return true;

        // 할당 전에 코덱의 상한과 비교한다(아래 resize 가 헤더 값을 그대로 쓴다).
        if ( CompressionStreamInternal::findCodecForHeader( header, pRegistry ) == nullptr )
            return false;

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

        ICompressionCodec* const pCodec = CompressionStreamInternal::findCodecForHeader( header, pRegistry );
        if ( pCodec == nullptr )
            return false;

        const uint8* const pSrcPayload    = static_cast<const uint8*>( pSrc ) + sizeof( CompressionHeader );
        const size_t       srcPayloadSize = static_cast<size_t>( header._compressedSize );

        const bool bSuccess = pCodec->decompress( pSrcPayload, srcPayloadSize, pDst, dstCapacity, outUncompressedSize );
        if ( bSuccess == false )
        {
            SW_LOG_ERROR_ID( "Compression.DecompressFailed", "Decompression failed using codec: %#", pCodec->getCodecName() );
            return false;
        }

        if ( ( header._flags & CompressionHeader::kFlagChecksum ) != 0 )
        {
            const uint32 calculatedChecksum = computeChecksum( pDst, outUncompressedSize );
            if ( calculatedChecksum != header._checksum )
            {
                SW_LOG_ERROR_ID( "Compression.ChecksumMismatch", "Checksum mismatch: expected %x, got %x", header._checksum, calculatedChecksum );
                return false;
            }
        }

        return true;
    }
} // namespace sw
