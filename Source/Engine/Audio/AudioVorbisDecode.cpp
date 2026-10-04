/**
 * @file AudioVorbisDecode.cpp
 * @brief `AudioClipDecoder::decodeOgg` — stb_vorbis 구현을 이 TU 하나에만 넣습니다.
 * @note stb_vorbis.c 는 `L` · `C` · `R` · `CHECK` · `TRUE` 같은 짧은 매크로를 남깁니다. 유니티 빌드에서 뒤에 붙는 TU 를 오염시키지 않게
 *       include 바로 뒤에서 모두 지웁니다. stb_vorbis 를 다른 파일에서 include 하지 말 것.
 */
#include "pch.h"

#include "Engine/Audio/AudioClipDecoder.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.c>
#undef C
#undef CHECK
#undef CODEBOOK_ELEMENT
#undef CODEBOOK_ELEMENT_BASE
#undef CODEBOOK_ELEMENT_FAST
#undef CRC32_POLY
#undef DECODE
#undef DECODE_RAW
#undef DIVTAB_DENOM
#undef DIVTAB_NUMER
#undef EOP
#undef FALSE
#undef FAST_HUFFMAN_TABLE_MASK
#undef FAST_HUFFMAN_TABLE_SIZE
#undef INVALID_BITS
#undef L
#undef LIBVORBIS_MDCT
#undef LINE_OP
#undef MAX_BLOCKSIZE
#undef MAX_BLOCKSIZE_LOG
#undef NO_CODE
#undef PAGEFLAG_continued_packet
#undef PAGEFLAG_first_page
#undef PAGEFLAG_last_page
#undef PLAYBACK_LEFT
#undef PLAYBACK_MONO
#undef PLAYBACK_RIGHT
#undef R
#undef SAMPLE_unknown
#undef STBV_CDECL
#undef STBV_NOTUSED
#undef TRUE
#undef array_size_required
#undef temp_alloc
#undef temp_alloc_restore
#undef temp_alloc_save
#undef temp_block_array
#undef temp_free

#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        /**
         * @brief stb_vorbis 에 넘기기 전에 Vorbis 주석 헤더(두 번째 패킷)의 길이 칸들이 패킷 안에 들어오는지 봅니다.
         * @details stb_vorbis 1.22 는 주석 헤더의 공급자 길이 · 주석 수 · 주석 길이를 패킷 크기와 대조하지 않는다. 큰 값이면 할당이 실패하고,
         *          실패한 뒤 정리(`vorbis_deinit`)가 아직 채우지 않은 주석 칸까지 `free` 해 프로세스가 죽는다(CVE-2023-45675 · 45676 · 45677 계열,
         *          `LoaderFuzzTest` 가 찾았다). 서드파티를 고치지 않고 입구에서 막는다 — 길이가 패킷 밖을 가리키는 파일은 어차피 망가진 파일이다.
         */
        struct OggCommentHeaderGuard
        {
            static uint32 readUint32( const uint8* pData )
            {
                return static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) | ( static_cast<uint32>( pData[2] ) << 16 ) |
                       ( static_cast<uint32>( pData[3] ) << 24 );
            }

            /** @brief 첫 논리 스트림의 @p packetIndex 번째 패킷을 페이지를 건너 모읍니다. 페이지가 잘렸거나 패킷이 끝나지 않으면 false 입니다. */
            static bool collectPacket( const uint8* pBytes, size_t byteCount, uint32 packetIndex, vector<uint8>& outPacketBytes )
            {
                outPacketBytes.clear();
                constexpr size_t kPageHeaderSize = 27;
                size_t           pageOffset      = 0;
                uint32           currentPacket   = 0;
                while ( pageOffset + kPageHeaderSize <= byteCount )
                {
                    if ( Memory::compare( pBytes + pageOffset, "OggS", 4 ) != 0 )
                        return false;
                    const size_t segmentCount = pBytes[pageOffset + 26];
                    if ( pageOffset + kPageHeaderSize + segmentCount > byteCount )
                        return false;
                    const uint8* pSegmentTable = pBytes + pageOffset + kPageHeaderSize;
                    size_t       dataOffset    = pageOffset + kPageHeaderSize + segmentCount;
                    for ( size_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex )
                    {
                        const size_t lacing = pSegmentTable[segmentIndex];
                        if ( dataOffset + lacing > byteCount )
                            return false;
                        if ( currentPacket == packetIndex )
                            outPacketBytes.insert( outPacketBytes.end(), pBytes + dataOffset, pBytes + dataOffset + lacing );
                        dataOffset += lacing;
                        if ( lacing < 255 )
                        {
                            if ( currentPacket == packetIndex )
                                return true;
                            ++currentPacket;
                        }
                    }
                    pageOffset = dataOffset;
                }
                return false;
            }

            /** @brief 주석 헤더의 길이 칸이 모두 패킷 안이면 true 입니다. */
            static bool isCommentHeaderSane( const uint8* pBytes, size_t byteCount )
            {
                vector<uint8> packetBytes;
                if ( collectPacket( pBytes, byteCount, 1, packetBytes ) == false )
                    return false;
                const size_t size = packetBytes.size();
                if ( size < 7 + 4 + 4 || packetBytes[0] != 3 || Memory::compare( packetBytes.data() + 1, "vorbis", 6 ) != 0 )
                    return false;
                size_t       offset       = 7;
                const uint32 vendorLength = readUint32( packetBytes.data() + offset );
                offset += 4;
                if ( vendorLength > size - offset )
                    return false;
                offset += vendorLength;
                if ( offset + 4 > size )
                    return false;
                const uint32 commentCount = readUint32( packetBytes.data() + offset );
                offset += 4;
                if ( commentCount > ( size - offset ) / 4 )
                    return false;
                for ( uint32 commentIndex = 0; commentIndex < commentCount; ++commentIndex )
                {
                    if ( offset + 4 > size )
                        return false;
                    const uint32 commentLength = readUint32( packetBytes.data() + offset );
                    offset += 4;
                    if ( commentLength > size - offset )
                        return false;
                    offset += commentLength;
                }
                return true;
            }
        };
    } // namespace

    bool AudioClipDecoder::decodeOgg( const uint8* pBytes, size_t byteCount, AudioPcm& outPcm )
    {
        if ( pBytes == nullptr || byteCount < 4 || byteCount > static_cast<size_t>( INT32_MAX ) || Memory::compare( pBytes, "OggS", 4 ) != 0 )
            return false;
        if ( OggCommentHeaderGuard::isCommentHeaderSane( pBytes, byteCount ) == false )
            return false;

        int32  channelCount = 0;
        int32  sampleRate   = 0;
        int16* pSample      = nullptr;
        // 반환은 채널당 샘플 수다. 음수는 실패이고, 그때 stb 는 버퍼를 잡지 않는다.
        const int32 frameCount = stb_vorbis_decode_memory( pBytes, static_cast<int32>( byteCount ), &channelCount, &sampleRate, &pSample );
        if ( frameCount <= 0 || pSample == nullptr || channelCount <= 0 || sampleRate <= 0 )
        {
            free( pSample );
            return false;
        }

        AudioPcm pcm;
        pcm._channelCount     = static_cast<uint16>( channelCount );
        pcm._sampleRate       = static_cast<uint32>( sampleRate );
        pcm._bitsPerSample    = 16;
        const size_t byteSize = static_cast<size_t>( frameCount ) * static_cast<size_t>( channelCount ) * sizeof( int16 );
        const uint8* pBegin   = reinterpret_cast<const uint8*>( pSample );
        pcm._listData.assign( pBegin, pBegin + byteSize );
        free( pSample );
        outPcm = std::move( pcm );
        return true;
    }
} // namespace sw
