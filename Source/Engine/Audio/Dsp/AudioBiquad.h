/**
 * @file AudioBiquad.h
 * @brief 2 차 IIR 필터(바이쿼드) — RBJ "Audio EQ Cookbook" 계수와 스테레오 상태(전치 직접형 II)입니다.
 * @details 버스 이펙트(LowPass · HighPass · BandPass · Peaking · 셸빙)와 보이스의 가림 · 거리 로우패스가 같이 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 바이쿼드 모양입니다. */
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
         * @brief cookbook 계수입니다. @p frequencyHz 는 (0, 나이퀴스트) 로 묶습니다.
         * @param gainDb Peaking · 셸빙에서만 씁니다.
         */
        static AudioBiquadCoefficients make( AudioBiquadType type, float32 frequencyHz, float32 q, float32 gainDb, float32 sampleRate );
        /** @brief @p frequencyHz 에서 크기 응답(선형)입니다 — |H(e^jω)|. 시험이 처리 결과와 견줍니다. */
        float32 computeMagnitude( float32 frequencyHz, float32 sampleRate ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioBiquadStereo
     * @brief 계수 하나 · 채널 둘의 상태입니다. 스테레오 교차 버퍼를 제자리에서 거릅니다.
     */
    struct SW_API AudioBiquadStereo
    {
        AudioBiquadCoefficients _coefficients{};
        float32                 _arrState1[2]{ 0.0f, 0.0f }; ///< 채널마다 z⁻¹ 상태
        float32                 _arrState2[2]{ 0.0f, 0.0f }; ///< 채널마다 z⁻² 상태

        /** @brief 상태를 비웁니다. */
        void reset();
        /** @brief @p frameCount 프레임(스테레오 교차)을 제자리에서 거릅니다. */
        void process( float32* pInterleaved, uint32 frameCount );
    };
} // namespace sw
