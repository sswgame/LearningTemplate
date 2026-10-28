/**
 * @file AudioClipDecoder.h
 * @brief 소리 파일 바이트(WAV, OGG Vorbis)를 재생할 PCM 으로 디코드하는, 플랫폼 공통 디코더입니다.
 * @details 바이트에서 바로 디코드하므로 팩 안의 파일도 읽을 수 있다. 재생 백엔드(XAudio2 등)는 결과의 형식 필드를 자기 형식 구조체로 옮기기만 하면 된다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @struct AudioPcm
     * @brief 디코드한 소리 하나입니다. 샘플은 채널이 교차된(interleaved) 리틀 엔디언이고, 채널 순서는 WAV 순서(채널 마스크 비트 오름차순)입니다.
     * @details 마스크가 0 인 3 채널 이상은 마스크 비트 순서로 아래부터 채운 배치로 읽습니다(`AudioDownmix::makeForWaveMask`).
     */
    struct AudioPcm
    {
        vector<uint8>          _listData;
        uint32                 _sampleRate{ 0 };
        uint32                 _channelMask{ 0 }; ///< 확장형일 때 스피커 배치(0 = 정하지 않음)
        uint16                 _channelCount{ 0 };
        uint16                 _bitsPerSample{ 0 };
        uint16                 _validBitsPerSample{ 0 }; ///< 확장형일 때 유효 비트 수다.
        uint8                  _bFloat      : 1;         ///< 샘플이 IEEE float 다(아니면 정수 PCM)
        uint8                  _bExtensible : 1;         ///< 원본이 확장형(WAVE_FORMAT_EXTENSIBLE)이었다. 재생 형식도 확장형으로 넘긴다.
        [[maybe_unused]] uint8 _reserved    : 6;

        AudioPcm()
            : _bFloat{ SW_FALSE }
            , _bExtensible{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        uint16 getBlockAlign() const { return static_cast<uint16>( _channelCount * ( _bitsPerSample / 8u ) ); }
    };
} // namespace sw

namespace sw
{
    struct AudioStreamInfo;

    class IAudioStreamDecoder;

    /**
     * @struct AudioClipDecoder
     * @brief 형식마다 하나씩 두고 확장자로 고르는 디코더입니다. 실패하면 false 를 반환하고 @p outPcm 은 건드리지 않습니다.
     */
    struct SW_API AudioClipDecoder
    {
        /** @brief RIFF WAV 의 PCM 과 IEEE float(확장형 포함)를 읽습니다. ADPCM 같은 압축 WAV 는 false 를 반환합니다. */
        [[nodiscard]] static bool decodeWAV( const uint8* pBytes, size_t byteCount, AudioPcm& outPcm );
        /**
         * @brief WAV 의 형식과 `data` 청크 위치만 읽습니다(샘플을 복사하지 않습니다). @p outFormat 의 `_listData` 는 비어 있습니다.
         * @details 스트리밍이 바이트 안의 샘플 자리를 바로 읽으려고 씁니다. `decodeWAV` 가 같은 코드를 씁니다.
         */
        [[nodiscard]] static bool parseWAVLayout( const uint8* pBytes, size_t byteCount, AudioPcm& outFormat, size_t& outDataOffset, uint32& outDataSize );
        /**
         * @brief OGG Vorbis 를 처음부터 순차로 읽는 디코더를 엽니다(스트리밍). 헤더가 틀렸거나 채널이 9 개 이상이면 nullptr 입니다. 채널은 Vorbis 순서 그대로입니다.
         * @details 디코더는 @p pBytes 를 복사하지 않고 읽으므로 호출한 쪽이 디코더보다 오래 바이트를 지켜야 합니다. 같은 헤더 검사(`decodeOGG`)를 거칩니다.
         */
        static unique_ptr<IAudioStreamDecoder> openOGGStream( const uint8* pBytes, size_t byteCount, AudioStreamInfo& outInfo );
        /** @brief OGG Vorbis 를 16 비트 정수 PCM 으로 풉니다(stb_vorbis). 3 채널 이상은 WAV 채널 순서로 옮기고 `_channelMask` 를 적습니다(PCM 의 약속). */
        [[nodiscard]] static bool decodeOGG( const uint8* pBytes, size_t byteCount, AudioPcm& outPcm );
        /** @brief 이 디코더가 확장자로 다루는 형식(`.wav`, `.ogg`)인지 반환합니다. */
        static bool isSupportedExtension( string_view path );
        /** @brief @p path 의 확장자로 디코더를 고릅니다. 다루지 않는 확장자면 false 입니다. */
        [[nodiscard]] static bool decode( string_view path, const uint8* pBytes, size_t byteCount, AudioPcm& outPcm );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioWAVEncoder
     * @brief float32 교차 샘플을 WAV(`WAVE_FORMAT_EXTENSIBLE`, IEEE float, 채널 마스크 포함) 바이트로 씁니다. `AudioClipDecoder::decodeWAV` 가 그대로 읽습니다.
     * @details 오프라인 렌더 덤프(`gv_audioDumpPath`)와 테스트가 다채널 출력을 파일로 남겨 분석할 때 씁니다. 형식 상수는 디코더와 같은 정의를 씁니다.
     */
    struct SW_API AudioWAVEncoder
    {
        /** @brief @p frameCount 프레임 × @p channelCount 채널을 @p outBytes 에 씁니다(이전 내용은 지웁니다). */
        static void encodeFloat32( const float32* pInterleaved, uint32 frameCount, uint32 channelCount, uint32 channelMask, uint32 sampleRate, vector<uint8>& outBytes );
    };
} // namespace sw
