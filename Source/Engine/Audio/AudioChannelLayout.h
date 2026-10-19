/**
 * @file AudioChannelLayout.h
 * @brief 믹서 출력의 스피커 구성(채널 집합)과 채널 수입니다.
 * @details 채널 수는 이 형식에서만 나온다. 믹서 · 엔진 · 장치 경계는 상수 대신 `AudioChannelLayout` 을 받아 `AudioChannelLayoutUtil::getChannelCount` 로 센다.
 *          채널 순서는 FL FR FC LFE BL BR SL SR 의 앞부분이다(스테레오 FL FR, 5.1 FL FR FC LFE BL BR, 7.1 은 뒤에 SL SR).
 *          레이아웃은 장치를 닫은 뒤에만 바꾼다. 최대 채널 수(`audio::kMaxChannelCount`)보다 큰 구성(7.1.4, 앰비소닉)은 다루지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    namespace audio
    {
        /** @brief 지원하는 가장 많은 출력 채널 수입니다(7.1). */
        inline constexpr uint32 kMaxChannelCount = 8;
        /** @brief 스테레오의 채널 수입니다. 스테레오만 처리하는 이펙트와 보이스가 씁니다. */
        inline constexpr uint32 kStereoChannelCount = 2;
    } // namespace audio

    /** @brief 출력 스피커 구성입니다. */
    enum class AudioChannelLayout : uint8
    {
        Stereo,
        Surround51,
        Surround71,
    };

    /**
     * @struct AudioChannelLayoutUtil
     * @brief 레이아웃에서 채널 수를 구합니다.
     */
    struct AudioChannelLayoutUtil
    {
        /** @brief 레이아웃의 채널 수입니다(LFE 포함). */
        static constexpr uint32 getChannelCount( AudioChannelLayout layout )
        {
            switch ( layout )
            {
                case AudioChannelLayout::Stereo:
                    return audio::kStereoChannelCount;
                case AudioChannelLayout::Surround51:
                    return 6;
                case AudioChannelLayout::Surround71:
                    return audio::kMaxChannelCount;
            }
            return 0;
        }
    };
} // namespace sw
