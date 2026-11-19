/**
 * @file TAAUtil.h
 * @brief TAA(taa.hlsl)의 수식 중 CPU 에서 검사하는 몫입니다 — 히스토리 Catmull-Rom 가중과 적응 블렌드 계수.
 * @details 셰이더가 같은 식을 쓴다(taa.hlsl 의 `TemporalAA::computeCatmullRomWeights` · `computeHistoryWeight`). 식을 바꾸면 둘을 함께 바꾼다 — 이 파일은 단위 테스트가 식의 성질
 *          (가중 합 1 · 정수 자리의 보간 · 신뢰도 · 속도에 따른 단조성)을 붙잡는 자리다. 모양은 Adria(MIT) · Playdead INSIDE TAA 의 feedback min · max 와 Jimenez 의 5 탭 Catmull-Rom 이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @struct TAAUtil @brief TAA 수식의 순수 함수입니다(파일 머리말 참고). */
    struct SW_API TAAUtil
    {
        /**
         * @brief 히스토리 신뢰도의 분모 하한입니다 — 어두운 픽셀에서 작은 휘도 차가 신뢰도를 0 으로 끌어내리지 않게 합니다(Playdead TAA 의 0.2).
         * @details taa.hlsl 의 `kTrustLuminanceFloor` 와 같은 값이다.
         */
        static constexpr float32 kTrustLuminanceFloor = 0.2f;

        /**
         * @brief 1 차원 Catmull-Rom 스플라인의 네 탭 가중(x = 텍셀 -1, 0, +1, +2)입니다. @p fraction 은 텍셀 0 의 중심에서 잰 표본 자리(0 이상 1 미만)입니다.
         * @details 합은 늘 1 이고, fraction 0 이면 (0, 1, 0, 0) 이라 텍셀 중심을 그대로 읽는다. 바깥 두 탭은 음수일 수 있다(날카로운 보간 — 링잉은 클리핑이 자른다).
         *          셰이더는 가운데 두 탭을 바이리니어 한 번으로 합쳐 2 차원 9 탭을 5 탭(네 모서리를 버림)으로 줄인다.
         */
        [[nodiscard]] static float4 computeCatmullRomWeights( float32 fraction );

        /**
         * @brief 이번 프레임의 기록 비율(히스토리 쪽 가중)입니다 — 신뢰도가 높고 느린 픽셀은 @p maxWeight, 기록이 이번 그림과 많이 다르거나 빠른 픽셀은 @p minWeight 쪽입니다.
         * @details 신뢰도 = 1 − |현재 휘도 − 자른 기록 휘도| / max(둘, `kTrustLuminanceFloor`)(클리핑이 기록을 많이 옮긴 픽셀은 낮다). 속도 몫 = 화면 속도(px) / @p velocityRejectPixels 를 0..1 로 자른 값.
         *          비율 = lerp( min, max, 신뢰도² × (1 − 속도 몫) ). @p minWeight 가 @p maxWeight 보다 크면 둘을 바꿔 쓰지 않고 @p maxWeight 를 하한으로도 쓴다(범위가 점).
         */
        [[nodiscard]] static float32 computeHistoryWeight( float32 currentLuminance, float32 historyLuminance, float32 velocityPixels, float32 minWeight, float32 maxWeight,
                                                           float32 velocityRejectPixels );
    };
} // namespace sw
