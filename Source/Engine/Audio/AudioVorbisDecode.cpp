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

#include "Core/Memory/Memory.h"

namespace sw
{
    bool AudioClipDecoder::decodeOgg( const uint8* pBytes, size_t byteCount, AudioPcm& outPcm )
    {
        if ( pBytes == nullptr || byteCount < 4 || byteCount > static_cast<size_t>( INT32_MAX ) || Memory::compare( pBytes, "OggS", 4 ) != 0 )
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
