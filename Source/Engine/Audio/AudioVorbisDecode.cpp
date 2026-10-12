/**
 * @file AudioVorbisDecode.cpp
 * @brief `AudioClipDecoder::decodeOgg` 입니다. stb_vorbis 구현을 이 TU 하나에만 넣습니다.
 * @note stb_vorbis.c 는 `L`, `C`, `R`, `CHECK`, `TRUE` 같은 짧은 매크로를 남깁니다. 유니티 빌드에서 뒤에 붙는 TU 가 오염되지 않게
 *       include 바로 뒤에서 모두 지웁니다. stb_vorbis 를 다른 파일에서 include 하지 마세요.
 *       주의: `TRUE` 와 `FALSE` 는 stb_vorbis 가 없을 때만 정의합니다. Windows 에서는 `windows.h` 의 정의라서 지우면, 유니티 묶음의 뒤쪽 파일
 *       (d3d11.h 를 처음 include 하는 XAudio2AudioBackend.cpp)이 PCH 없는 빌드에서 깨집니다. 이 TU 가 정의한 경우에만 지웁니다.
 */
#include "pch.h"

#include "Engine/Audio/AudioClipDecoder.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#if !defined( TRUE )
    #define SW_STB_VORBIS_DEFINES_TRUE_FALSE
#endif
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
#if defined( SW_STB_VORBIS_DEFINES_TRUE_FALSE )
    #undef FALSE
    #undef TRUE
    #undef SW_STB_VORBIS_DEFINES_TRUE_FALSE
#endif
#undef array_size_required
#undef temp_alloc
#undef temp_alloc_restore
#undef temp_alloc_save
#undef temp_block_array
#undef temp_free

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/AudioStream.h"

namespace sw
{
    namespace
    {
        /**
         * @brief stb_vorbis 에 넘기기 전에, Vorbis 주석 헤더(두 번째 패킷)의 길이 필드들이 패킷 안에 들어오는지 확인합니다.
         * @details stb_vorbis 1.22 는 주석 헤더의 공급자 길이, 주석 수, 주석 길이를 패킷 크기와 대조하지 않는다. 값이 크면 할당이 실패하고,
         *          실패 뒤 정리(`vorbis_deinit`)가 아직 채우지 않은 주석 필드까지 `free` 해서 프로세스가 죽는다(CVE-2023-45675, 45676, 45677 계열이며 `LoaderFuzzTest` 가 찾았다).
         *          서드파티를 고치지 않고 입구에서 막는다. 길이가 패킷 밖을 가리키는 파일은 어차피 망가진 파일이다.
         */
        struct OggCommentHeaderGuard
        {
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

            /** @brief 주석 헤더의 길이 필드가 모두 패킷 안에 있으면 true 를 반환합니다. */
            static bool isCommentHeaderSane( const uint8* pBytes, size_t byteCount )
            {
                vector<uint8> packetBytes;
                if ( collectPacket( pBytes, byteCount, 1, packetBytes ) == false )
                    return false;
                const size_t size = packetBytes.size();
                if ( size < 7 + 4 + 4 || packetBytes[0] != 3 || Memory::compare( packetBytes.data() + 1, "vorbis", 6 ) != 0 )
                    return false;
                size_t       offset       = 7;
                const uint32 vendorLength = LittleEndianUtil::readUint32( packetBytes.data() + offset );
                offset += 4;
                if ( vendorLength > size - offset )
                    return false;
                offset += vendorLength;
                if ( offset + 4 > size )
                    return false;
                const uint32 commentCount = LittleEndianUtil::readUint32( packetBytes.data() + offset );
                offset += 4;
                if ( commentCount > ( size - offset ) / 4 )
                    return false;
                for ( uint32 commentIndex = 0; commentIndex < commentCount; ++commentIndex )
                {
                    if ( offset + 4 > size )
                        return false;
                    const uint32 commentLength = LittleEndianUtil::readUint32( packetBytes.data() + offset );
                    offset += 4;
                    if ( commentLength > size - offset )
                        return false;
                    offset += commentLength;
                }
                return true;
            }
        };

        /**
         * @brief 디코더 메모리를 우리 버퍼 한 덩어리에서 나눠 주며 stb_vorbis 를 엽니다. 모자라면(VORBIS_outofmem) 두 배로 다시 열고, 상한을 넘으면 실패합니다.
         * @details stb 는 깨진 헤더의 오류 경로에서 `setup_temp_malloc` 으로 잡은 임시 버퍼를 해제하지 않는데(변이한 Ogg 로 재현된다), 우리 버퍼 안이면 함께 버려진다.
         *          조작한 헤더가 수백 MB 를 요구하는 경우도 여기서 막힌다. 열린 디코더는 @p inoutListArena 를 닫을 때까지 써서 호출한 쪽이 디코더와 같이 쥐어야 한다.
         */
        struct OggOpenUtil
        {
            static constexpr size_t kInitialArenaBytes = 256 * 1024;
            static constexpr size_t kMaxArenaBytes     = 64 * 1024 * 1024;

            static stb_vorbis* openWithArena( const uint8* pBytes, size_t byteCount, vector<uint8>& inoutListArena )
            {
                if ( pBytes == nullptr || byteCount < 4 || byteCount > static_cast<size_t>( INT32_MAX ) || Memory::compare( pBytes, "OggS", 4 ) != 0 )
                    return nullptr;
                if ( OggCommentHeaderGuard::isCommentHeaderSane( pBytes, byteCount ) == false )
                    return nullptr;
                stb_vorbis* pVorbis = nullptr;
                for ( size_t arenaBytes = kInitialArenaBytes; arenaBytes <= kMaxArenaBytes; arenaBytes *= 2 )
                {
                    inoutListArena.resize( arenaBytes );
                    stb_vorbis_alloc alloc{ reinterpret_cast<utf8*>( inoutListArena.data() ), static_cast<int32>( arenaBytes ) };
                    int32            error = VORBIS__no_error;
                    pVorbis                = stb_vorbis_open_memory( pBytes, static_cast<int32>( byteCount ), &error, &alloc );
                    if ( pVorbis != nullptr || error != VORBIS_outofmem )
                        break;
                }
                return pVorbis;
            }
        };

        /** @brief stb_vorbis 로 OGG 를 앞으로 가며 float 로 푸는 스트리밍 디코더입니다. 채널은 파일 그대로(1 ~ 2)입니다. */
        class OggStreamDecoder final : public IAudioStreamDecoder
        {
        public:
            OggStreamDecoder( stb_vorbis* pVorbis, vector<uint8>&& listArena, int32 channelCount, uint32 frameCount )
                : _listArena{ std::move( listArena ) }
                , _pVorbis{ pVorbis }
                , _frameCount{ frameCount }
                , _channelCount{ channelCount }
            {
            }

            ~OggStreamDecoder() override
            {
                if ( _pVorbis != nullptr )
                    stb_vorbis_close( _pVorbis );
            }

            uint32 readFrames( float32* pOut, uint32 frameCount ) override
            {
                const int32 decoded = stb_vorbis_get_samples_float_interleaved( _pVorbis, _channelCount, pOut, static_cast<int32>( frameCount ) * _channelCount );
                return decoded > 0 ? static_cast<uint32>( decoded ) : 0u;
            }

            bool seekToFrame( uint32 frame ) override
            {
                if ( _frameCount < frame )
                    return false;
                if ( frame == 0 )
                    return stb_vorbis_seek_start( _pVorbis ) != 0; // 루프의 되감기는 처음으로 가는 것이 대부분이라 빠른 길을 쓴다
                return stb_vorbis_seek( _pVorbis, frame ) != 0;
            }

        private:
            vector<uint8> _listArena; ///< stb_vorbis 의 메모리입니다. 디코더가 닫힐 때까지 살아 있어야 한다.
            stb_vorbis*   _pVorbis;
            uint32        _frameCount;
            int32         _channelCount;
        };
    } // namespace

    unique_ptr<IAudioStreamDecoder> AudioClipDecoder::openOggStream( const uint8* pBytes, size_t byteCount, AudioStreamInfo& outInfo )
    {
        vector<uint8> listArena;
        stb_vorbis*   pVorbis = OggOpenUtil::openWithArena( pBytes, byteCount, listArena );
        if ( pVorbis == nullptr )
            return nullptr;
        const stb_vorbis_info info       = stb_vorbis_get_info( pVorbis );
        const uint32          frameCount = stb_vorbis_stream_length_in_samples( pVorbis );
        if ( info.channels < 1 || 2 < info.channels || info.sample_rate == 0 || frameCount == 0 )
        {
            stb_vorbis_close( pVorbis );
            return nullptr;
        }
        outInfo._sampleRate   = info.sample_rate;
        outInfo._frameCount   = frameCount;
        outInfo._channelCount = static_cast<uint16>( info.channels );
        return make_unique<OggStreamDecoder>( pVorbis, std::move( listArena ), info.channels, frameCount );
    }

    bool AudioClipDecoder::decodeOgg( const uint8* pBytes, size_t byteCount, AudioPcm& outPcm )
    {
        // 디코더 메모리는 우리 버퍼 한 덩어리에서 나눠 준다(`OggOpenUtil::openWithArena`).
        vector<uint8> listArena;
        stb_vorbis*   pVorbis = OggOpenUtil::openWithArena( pBytes, byteCount, listArena );
        if ( pVorbis == nullptr )
            return false;

        const stb_vorbis_info info = stb_vorbis_get_info( pVorbis );
        if ( info.channels <= 0 || info.sample_rate == 0 )
        {
            stb_vorbis_close( pVorbis );
            return false;
        }

        // stb_vorbis_decode_memory 와 같은 경로다. 채널을 섞어 16 비트 정수로 끝까지 읽는다.
        constexpr int32 kChunkFrames = 4096;
        vector<int16>   listSample;
        vector<int16>   listChunk( static_cast<size_t>( kChunkFrames ) * static_cast<size_t>( info.channels ) );
        for ( ;; )
        {
            const int32 frameCount = stb_vorbis_get_samples_short_interleaved( pVorbis, info.channels, listChunk.data(), static_cast<int32>( listChunk.size() ) );
            if ( frameCount <= 0 )
                break;
            listSample.insert( listSample.end(), listChunk.begin(), listChunk.begin() + static_cast<ptrdiff_t>( frameCount ) * info.channels );
        }
        stb_vorbis_close( pVorbis );
        if ( listSample.empty() )
            return false;

        AudioPcm pcm;
        pcm._channelCount   = static_cast<uint16>( info.channels );
        pcm._sampleRate     = info.sample_rate;
        pcm._bitsPerSample  = 16;
        const uint8* pBegin = reinterpret_cast<const uint8*>( listSample.data() );
        pcm._listData.assign( pBegin, pBegin + listSample.size() * sizeof( int16 ) );
        outPcm = std::move( pcm );
        return true;
    }
} // namespace sw
