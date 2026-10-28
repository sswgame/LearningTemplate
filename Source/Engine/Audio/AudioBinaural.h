/**
 * @file AudioBinaural.h
 * @brief 헤드폰용 바이노럴 렌더러입니다. 모노 소스 하나를 두 귀 신호로 바꿉니다(구 머리 모델 HRTF).
 * @details 측정 데이터(SOFA) 없이 Brown · Duda(1998)의 구 머리 모델을 쓴다. 귀마다 두 가지를 건다.
 *          - 도달 시간 차(ITD): 귀 축과 소리 방향 사이 각 θ 에서 지연 = (a/c)(1 − cos θ)(θ < 90°), (a/c)(1 + θ − π/2)(그 밖). 머리 반지름 a = 8.75 cm.
 *          - 머리 그림자(ILD): H(s) = (1 + α(θ)·s/(2ω₀)) / (1 + s/(2ω₀)), ω₀ = c/a, α(θ) = 1.05 + 0.95·cos(θ/150° · π). 바로 앞의 귀는 고음이 +6 dB, 반대쪽 귀는 -20 dB 다.
 *          지연은 블록 안에서 목표까지 선형으로 옮겨 가고(분수 지연, 선형 보간), 필터 계수는 블록마다 바꾼다. 방향 길이가 1 보다 짧으면(머리 근처)
 *          HRTF 신호와 두 귀 같은 신호(-3 dB)를 길이로 섞는다. 고도와 귓바퀴(pinna) 단서는 아직 없다 — 방향에 고도가 들어올 때 함께 넣는다.
 *          오디오 스레드의 보이스 하나가 하나씩 가지며 할당하지 않는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Audio/AudioPanner.h"

namespace sw
{
    /**
     * @struct AudioBinauralFilter
     * @brief 보이스 하나의 바이노럴 상태(입력 지연선, 귀마다 지연과 머리 그림자 필터)입니다.
     */
    struct SW_API AudioBinauralFilter
    {
        /** @brief 입력 지연선 길이(프레임, 2 의 거듭제곱)입니다. 가장 큰 ITD(약 32 프레임) + 보간 여유보다 깁니다. */
        static constexpr uint32 kDelayLineLength = 64;

        /** @brief 귀 하나의 상태입니다. */
        struct Ear
        {
            float32 _delayFrames{ 0.0f };       ///< 지금 지연(프레임)이다.
            float32 _targetDelayFrames{ 0.0f }; ///< 이번 블록 끝의 지연이다.
            float32 _b0{ 1.0f };                ///< 머리 그림자 필터 계수(y = b0·x + b1·x₁ − a1·y₁)다.
            float32 _b1{ 0.0f };
            float32 _a1{ 0.0f };
            float32 _x1{ 0.0f };
            float32 _y1{ 0.0f };
        };

        float32 _arrHistory[kDelayLineLength]{}; ///< 입력 지연선이다.
        Ear     _left{};                         ///< 왼쪽 귀다.
        Ear     _right{};                        ///< 오른쪽 귀다.
        float32 _focus{ 1.0f };                  ///< 방향 길이(0 = 머리 안, 두 귀 같음)다.
        uint32  _writeIndex{ 0 };                ///< 다음에 쓸 지연선 자리다.
        bool    _bPrimed{ false };               ///< 방향을 한 번이라도 받았는지다(첫 블록은 지연을 옮기지 않는다).

        /** @brief 상태를 비웁니다(새 소리를 시작하거나 바이노럴로 처음 바뀔 때). */
        void reset();
        /** @brief 다음 `process` 의 목표 방향을 정합니다(`AudioPanMode::Surround` 의 오른쪽 · 앞 성분). */
        void setDirection( const AudioPanDirection& direction );
        /** @brief 모노 @p pSource 의 @p frameCount 프레임을 두 귀 신호로 씁니다. */
        void process( const float32* pSource, uint32 frameCount, float32* pOutLeft, float32* pOutRight );

        /** @brief 귀 축과 소리 방향 사이 각(라디안, 0 ~ π)의 지연(프레임)입니다. */
        static float32 computeDelayFrames( float32 incidenceRadians );
        /** @brief 같은 각의 머리 그림자 필터 계수를 @p outEar 에 적습니다(상태는 그대로). */
        static void computeShadow( float32 incidenceRadians, Ear& outEar );
    };
} // namespace sw
