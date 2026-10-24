/**
 * @file AudioChannelLayout.h
 * @brief 믹서 출력의 스피커 구성(채널 집합)과 채널 수, 그리고 사용자가 고르는 스피커 모드입니다.
 * @details 채널 수는 이 형식에서만 나온다. 믹서 · 엔진 · 장치 경계는 상수 대신 `AudioChannelLayout` 을 받아 `AudioChannelLayoutUtil::getChannelCount` 로 센다.
 *          채널 순서는 FL FR FC LFE BL BR SL SR 의 앞부분이다(스테레오 FL FR, 5.1 FL FR FC LFE BL BR, 7.1 은 뒤에 SL SR). WAV 의 채널 마스크 순서와 같아서
 *          장치(`WAVEFORMATEXTENSIBLE`)와 덤프 파일에 그대로 넘긴다.
 *          레이아웃은 장치를 닫은 뒤에만 바꾼다. 최대 채널 수(`audio::kMaxChannelCount`)보다 큰 구성(7.1.4, 앰비소닉)은 다루지 않는다.
 *          레이아웃의 기준은 장치 협상과 사용자 설정(`audio.speakerMode` → `AudioSpeakerMode`)이다. 믹서 그래프 데이터는 채널 중립이다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    namespace audio
    {
        /** @brief 지원하는 가장 많은 출력 채널 수입니다(7.1). */
        inline constexpr uint32 kMaxChannelCount = 8;
        /** @brief 스테레오의 채널 수입니다. 스테레오 클립과 앞 두 채널(FL FR) 패닝이 씁니다. */
        inline constexpr uint32 kStereoChannelCount = 2;
        /** @brief 5.1 · 7.1 에서 LFE 채널의 번호입니다(FL FR FC 다음). 패너와 이펙트는 이 채널을 직접 패닝하거나 리버브에 넣지 않습니다. */
        inline constexpr uint32 kLfeChannelIndex = 3;
    } // namespace audio

    /** @brief 출력 스피커 구성입니다. */
    enum class AudioChannelLayout : uint8
    {
        Stereo,
        Surround51,
        Surround71,
        Binaural, ///< 헤드폰 두 채널(FL FR)이다. 채널 집합은 스테레오와 같고, 3D 모노 소리를 HRTF(`AudioBinauralFilter`)로 낸다.
    };

    /**
     * @brief 사용자 설정이 고르는 스피커 모드입니다(`audio.speakerMode`).
     * @details `Auto` 는 장치의 채널 마스크를 따르는 선언된 모드다(장치가 없으면 스테레오). 명시값은 장치가 그만큼의 채널을 갖지 않으면 오류로 멈추며, 몰래 낮추지 않는다.
     *          헤드폰은 장치가 알려 주지 않으므로 `Auto` 가 고르지 않는다. 사용자가 `Headphones` 를 골라야 바이노럴(`AudioChannelLayout::Binaural`)이 된다.
     */
    enum class AudioSpeakerMode : uint8
    {
        Auto,
        Stereo,
        Surround51,
        Surround71,
        Headphones,
    };

    /**
     * @struct AudioChannelLayoutUtil
     * @brief 레이아웃에서 채널 수 · 채널 마스크 · LFE 위치를 구합니다.
     */
    struct AudioChannelLayoutUtil
    {
        /** @brief WAV 채널 마스크의 비트입니다(`SPEAKER_*` 와 같은 값). */
        static constexpr uint32 kSpeakerFrontLeft          = 0x1;
        static constexpr uint32 kSpeakerFrontRight         = 0x2;
        static constexpr uint32 kSpeakerFrontCenter        = 0x4;
        static constexpr uint32 kSpeakerLowFrequency       = 0x8;
        static constexpr uint32 kSpeakerBackLeft           = 0x10;
        static constexpr uint32 kSpeakerBackRight          = 0x20;
        static constexpr uint32 kSpeakerFrontLeftOfCenter  = 0x40;
        static constexpr uint32 kSpeakerFrontRightOfCenter = 0x80;
        static constexpr uint32 kSpeakerBackCenter         = 0x100;
        static constexpr uint32 kSpeakerSideLeft           = 0x200;
        static constexpr uint32 kSpeakerSideRight          = 0x400;

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
                case AudioChannelLayout::Binaural:
                    return audio::kStereoChannelCount;
            }
            return 0;
        }

        /** @brief 레이아웃의 WAV 채널 마스크입니다(장치 형식과 덤프 파일). */
        static constexpr uint32 getChannelMask( AudioChannelLayout layout )
        {
            constexpr uint32 kStereoMask = kSpeakerFrontLeft | kSpeakerFrontRight;
            constexpr uint32 k51Mask     = kStereoMask | kSpeakerFrontCenter | kSpeakerLowFrequency | kSpeakerBackLeft | kSpeakerBackRight;
            switch ( layout )
            {
                case AudioChannelLayout::Stereo:
                    return kStereoMask;
                case AudioChannelLayout::Surround51:
                    return k51Mask;
                case AudioChannelLayout::Surround71:
                    return k51Mask | kSpeakerSideLeft | kSpeakerSideRight;
                case AudioChannelLayout::Binaural:
                    return kStereoMask;
            }
            return 0;
        }

        /** @brief 레이아웃에 LFE 채널이 있는지입니다. 있으면 번호는 `audio::kLfeChannelIndex` 입니다. */
        static constexpr bool hasLfe( AudioChannelLayout layout ) { return layout == AudioChannelLayout::Surround51 || layout == AudioChannelLayout::Surround71; }

        /** @brief 레이아웃에서 LFE 를 뺀 채널 수입니다(패너의 퍼짐과 리버브 입력 정규화). */
        static constexpr uint32 getFullRangeChannelCount( AudioChannelLayout layout ) { return getChannelCount( layout ) - ( hasLfe( layout ) ? 1u : 0u ); }
    };
} // namespace sw
