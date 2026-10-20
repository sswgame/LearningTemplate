/**
 * @file AudioBiquad.h
 * @brief 2 차 IIR 필터(바이쿼드)입니다. RBJ "Audio EQ Cookbook" 계수와 채널별 상태(전치 직접형 II)를 담습니다.
 * @details 버스 이펙트(LowPass, HighPass, BandPass, Peaking, 셸빙)와 보이스의 가림, 거리 로우패스가 함께 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Audio/AudioBlock.h"

namespace sw
{
    /** @brief 바이쿼드 형태입니다. */
    enum class AudioBiquadType : uint8
    {
        LowPass = 0,
        HighPass,
        BandPass, ///< 중심 0 dB 대역 통과(cookbook "constant 0 dB peak gain")
        Peaking,
        LowShelf,
        HighShelf,
    };

    /** @brief a0 로 나눈 계수입니다. y = b0·x + b1·x₁ + b2·x₂ − a1·y₁ − a2·y₂. */
    struct SW_API AudioBiquadCoefficients
    {
        float32 _b0{ 1.0f };
        float32 _b1{ 0.0f };
        float32 _b2{ 0.0f };
        float32 _a1{ 0.0f };
        float32 _a2{ 0.0f };

        /**
         * @brief cookbook 계수를 구합니다. @p frequencyHz 는 (0, 나이퀴스트) 범위로 제한합니다.
         * @param gainDb Peaking 과 셸빙에서만 씁니다.
         */
        static AudioBiquadCoefficients make( AudioBiquadType type, float32 frequencyHz, float32 q, float32 gainDb, float32 sampleRate );
        /** @brief @p frequencyHz 에서의 크기 응답(선형) |H(e^jω)| 을 반환합니다. 테스트가 처리 결과와 비교합니다. */
        float32 computeMagnitude( float32 frequencyHz, float32 sampleRate ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioBiquadFilter
     * @brief 계수 하나와 채널마다의 상태입니다(최대 `audio::kMaxChannelCount` 채널). 평면 블록의 모든 채널을 제자리에서 거릅니다.
     * @details 버스 이펙트는 레이아웃의 채널 수만큼, 보이스는 소스 채널(1 · 2)만큼 같은 형식을 쓴다. 채널마다 상태가 따로라 채널끼리 섞이지 않는다.
     */
    struct SW_API AudioBiquadFilter
    {
        AudioBiquadCoefficients _coefficients{};
        float32                 _arrState1[audio::kMaxChannelCount]{}; ///< 채널마다 z⁻¹ 상태를 가진다.
        float32                 _arrState2[audio::kMaxChannelCount]{}; ///< 채널마다 z⁻² 상태를 가진다.

        /** @brief 상태를 비웁니다. */
        void reset();
        /** @brief @p block(평면, `_channelCount` 채널 · `_frameCount` 프레임)을 제자리에서 거릅니다. */
        void process( const AudioBlockView& block );
    };
} // namespace sw
