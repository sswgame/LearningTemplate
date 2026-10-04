/**
 * @file AudioClipDecoder.h
 * @brief 소리 파일 바이트(WAV · OGG Vorbis)를 재생할 PCM 으로 푸는 플랫폼 공통 디코더입니다.
 * @details 바이트에서 바로 풀므로 팩 안의 파일도 됩니다. 재생 백엔드(XAudio2 등)는 결과의 형식 필드를 자기 형식 구조체로 옮기기만 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct AudioPcm
     * @brief 디코드한 소리 하나입니다. 샘플은 채널이 교차된(interleaved) 리틀 엔디언입니다.
     */
    struct AudioPcm
    {
        vector<uint8>          _listData;
        uint32                 _sampleRate{ 0 };
        uint32                 _channelMask{ 0 }; ///< 확장형일 때 스피커 배치(0 = 정하지 않음)
        uint16                 _channelCount{ 0 };
        uint16                 _bitsPerSample{ 0 };
        uint16                 _validBitsPerSample{ 0 }; ///< 확장형일 때 유효 비트
        uint8                  _bFloat      : 1;         ///< 샘플이 IEEE float 다(아니면 정수 PCM)
        uint8                  _bExtensible : 1;         ///< 원본이 확장형(WAVE_FORMAT_EXTENSIBLE)이었다 — 재생 형식도 확장형으로 넘긴다
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
    /**
     * @struct AudioClipDecoder
     * @brief 형식마다 하나씩, 그리고 확장자로 고르는 창구 하나입니다. 실패하면 false 이고 @p outPcm 은 건드리지 않습니다.
     */
    struct SW_API AudioClipDecoder
    {
        /** @brief RIFF WAV 의 PCM · IEEE float(확장형 포함)를 읽습니다. ADPCM 같은 압축 WAV 는 false 입니다. */
        [[nodiscard]] static bool decodeWav( const uint8* pBytes, size_t byteCount, AudioPcm& outPcm );
        /** @brief OGG Vorbis 를 16 비트 정수 PCM 으로 풉니다(stb_vorbis). */
        [[nodiscard]] static bool decodeOgg( const uint8* pBytes, size_t byteCount, AudioPcm& outPcm );
        /** @brief 이 디코더가 확장자로 다루는 형식인지(`.wav` · `.ogg`) 반환합니다. */
        static bool isSupportedExtension( string_view path );
        /** @brief @p path 의 확장자로 디코더를 고릅니다. 다루지 않는 확장자면 false 입니다. */
        [[nodiscard]] static bool decode( string_view path, const uint8* pBytes, size_t byteCount, AudioPcm& outPcm );
    };
} // namespace sw
