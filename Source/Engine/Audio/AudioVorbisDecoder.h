/**
 * @file AudioVorbisDecoder.h
 * @brief OGG Vorbis 디코딩(통째로 · 스트리밍)입니다. stb_vorbis 구현은 `AudioVorbisDecoder.cpp` 한 TU 에만 들어간다(짧은 매크로가 다른 TU 를 오염시키지 않게).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/AudioPCM.h"

namespace sw
{
    struct AudioStreamInfo;

    class IAudioStreamDecoder;

    /** @brief OGG Vorbis 를 PCM 으로 풀거나 순차로 읽는 디코더를 엽니다. 확장자로 고르는 일은 `AudioPCM::decode` 가 한다. */
    struct SW_API AudioVorbisDecoder
    {
        /**
         * @brief OGG Vorbis 를 처음부터 순차로 읽는 디코더를 엽니다(스트리밍). 헤더가 틀렸거나 채널이 9 개 이상이면 nullptr 입니다. 채널은 Vorbis 순서 그대로입니다.
         * @details 디코더는 @p pBytes 를 복사하지 않고 읽으므로 호출한 쪽이 디코더보다 오래 바이트를 지켜야 합니다. 같은 헤더 검사(`decode`)를 거칩니다.
         */
        static unique_ptr<IAudioStreamDecoder> openStream( const uint8* pBytes, size_t byteCount, AudioStreamInfo& outInfo );
        /** @brief OGG Vorbis 를 16 비트 정수 PCM 으로 풉니다(stb_vorbis). 3 채널 이상은 WAV 채널 순서로 옮기고 `_channelMask` 를 적습니다(PCM 의 약속). */
        [[nodiscard]] static bool decode( const uint8* pBytes, size_t byteCount, AudioPCM& outPCM );
    };
} // namespace sw
