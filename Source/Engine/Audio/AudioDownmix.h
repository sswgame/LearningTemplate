/**
 * @file AudioDownmix.h
 * @brief 3 채널 이상의 소리(5.1 · 7.1 클립)를 재생할 스테레오로 줄이는 명시 다운믹스 행렬입니다.
 * @details 클립은 1 · 2 채널로 재생한다(`AudioClipData`). 그보다 많은 채널은 읽을 때(통째로 푼 클립) 또는 프레임마다(스트리밍) 이 행렬로 줄인다.
 *          계수는 ITU-R BS.775 의 스테레오 다운믹스다. 왼쪽 = FL + 0.707·FC + 0.707·(BL, SL), 오른쪽 = FR + 0.707·FC + 0.707·(BR, SR),
 *          BC 는 양쪽 0.5, LFE 는 버린다. FLC · FRC 는 같은 쪽 1 이다. 높이 채널(TOP_*)은 다루지 않고 오류로 멈춘다.
 *          채널 배치는 WAV 채널 마스크가 기준이다. 마스크가 없는 WAV(`WAVEFORMATEX`)는 마스크 비트 순서로 아래부터 채운다(Microsoft 의 규칙).
 *          Vorbis 는 명세(Vorbis I 4.3.9)가 채널 수마다 정한 순서를 쓴다(`getVorbisSpeakers`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct AudioDownmix
     * @brief 소스 채널마다의 (왼쪽, 오른쪽) 계수입니다. 프레임 하나를 스테레오 한 쌍으로 바꿉니다.
     */
    struct SW_API AudioDownmix
    {
        /** @brief 다룰 수 있는 가장 많은 소스 채널 수입니다. */
        static constexpr uint32 kMaxSourceChannelCount = 8;

        float32 _arrLeft[kMaxSourceChannelCount]{};  ///< 소스 채널마다 왼쪽 계수다.
        float32 _arrRight[kMaxSourceChannelCount]{}; ///< 소스 채널마다 오른쪽 계수다.
        uint32  _channelCount{ 0 };                  ///< 소스 채널 수다(0 = 다운믹스 없음).

        /**
         * @brief 스피커 배치(소스 채널 순서대로 WAV 마스크 비트 하나씩)로 행렬을 만듭니다.
         * @return 채널이 3 ~ 8 이 아니거나, 모르는 스피커(높이 채널)이거나, 같은 스피커가 두 번 나오면 false 입니다(오류를 남깁니다).
         */
        [[nodiscard]] static bool makeForSpeakers( const uint32* pSpeakerBit, uint32 channelCount, AudioDownmix& outDownmix );
        /**
         * @brief WAV 채널 마스크로 행렬을 만듭니다. 마스크의 비트를 아래부터 소스 채널에 하나씩 줍니다. 마스크가 0 이면 아래 `channelCount` 비트입니다.
         * @return 마스크의 비트가 채널 수보다 적으면(배치를 모르는 채널) false 입니다.
         */
        [[nodiscard]] static bool makeForWaveMask( uint32 channelCount, uint32 channelMask, AudioDownmix& outDownmix );
        /** @brief Vorbis 채널 순서(명세 4.3.9)의 스피커 비트를 @p outArrSpeakerBit 에 적습니다. 채널이 1 ~ 8 이 아니면 false 입니다. */
        [[nodiscard]] static bool getVorbisSpeakers( uint32 channelCount, uint32 ( &outArrSpeakerBit )[kMaxSourceChannelCount] );

        /** @brief 프레임 하나(소스 채널 교차, `_channelCount` 개)를 스테레오로 줄입니다. */
        void apply( const float32* pFrame, float32& outLeft, float32& outRight ) const;
    };
} // namespace sw
