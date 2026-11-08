/**
 * @file ShadowFilter.h
 * @brief 그림자 필터 품질(3x3 PCF · PCSS)과 PCSS 커널 크기 식입니다. 셰이더(`lighting.hlsli` 의 `Shadow::filter*`)가 같은 식을 씁니다.
 * @details 품질은 사용자 설정 `graphics.shadowFilter`(`gv_shadowFilter`) 하나가 고르고 패스 상수 `g_ShadowFilter` 로 셰이더에 갑니다.
 *          PCSS 는 빛 원천 크기(`LightShadowSettings::_sourceSize`)로 반그림자 폭을 바꿉니다 — 언리얼 `r.Shadow.FilterMethod 1`(PCSS) · 유니티 HDRP PCSS 와 같은 자리이고,
 *          값싼 단계는 지금까지의 3x3 PCF 그대로입니다(그림이 바뀌지 않는다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 그림자 필터 품질입니다. 값은 `gv_shadowFilter` · 셰이더 `g_ShadowFilter.x` 와 같습니다. */
    enum class ShadowFilterMode : uint32
    {
        Pcf3x3 = 0, ///< 고정 3x3 PCF(쌍선형 비교 탭 아홉) — 값싼 단계, 기본
        Pcss,       ///< 가리개 찾기 + 원천 크기에 따른 반그림자 폭(탭 열여섯 둘)
        Count,
    };

    /**
     * @class ShadowFilter
     * @brief 그림자 필터 품질을 고르고 PCSS 커널 반지름을 셉니다. 셰이더의 같은 식과 짝이라 식을 고치면 `lighting.hlsli` 도 고칩니다.
     */
    class SW_API ShadowFilter
    {
    public:
        /// @brief PCSS 커널 반지름의 상한(텍셀) — 탭 열여섯이 넓게 퍼지면 띠가 보인다. 가리개 찾기 반지름도 이 값을 넘지 않는다.
        static constexpr float32 kMaxKernelRadiusTexels = 8.0f;
        /// @brief PCSS 커널 반지름의 하한(텍셀) — 원천 크기 0 이어도 텍셀 계단이 보이지 않게(3x3 PCF 와 비슷한 폭).
        static constexpr float32 kMinKernelRadiusTexels = 1.0f;

        /**
         * @brief 이번 프레임의 필터 품질입니다(`gv_shadowFilter`).
         * @details 범위 밖 값은 오류 로그를 남기고 3x3 PCF 로 그립니다 — 설정 스키마의 Enum 은 범위 밖을 넣지 못하므로 명령줄(`-gv_shadowFilter=`)에서만 난다.
         */
        [[nodiscard]] static ShadowFilterMode getActiveMode();

        /**
         * @brief 방향광 반그림자 배율입니다 — 받는 점과 가리개의 NDC 깊이 차 1 이 기준 볼륨 텍셀 몇 개의 반그림자 **폭**이 되는가.
         * @param depthRange     기준 볼륨 깊이 구간(m, `DirectionalShadowProjection::_depthRange`)
         * @param sourceAngle    원천 시직경(라디안)
         * @param texelWorldSize 기준 볼륨 텍셀 하나의 월드 길이(m)
         * @details 평행광의 반그림자 폭은 가리개에서 받는 점까지 거리 × 2 tan(시직경 / 2) 이다(원천이 원반일 때). 캐스케이드 c 는 텍셀이 배율만큼 작으므로 셰이더가 배율을 곱한다.
         */
        [[nodiscard]] static float32 computeDirectionalPenumbraScale( float32 depthRange, float32 sourceAngle, float32 texelWorldSize );

        /**
         * @brief 방향광 PCSS 커널 반지름(텍셀)입니다. 셰이더 `Shadow::sampleCascade` 의 식과 같습니다.
         * @param receiverDepth  받는 점의 NDC 깊이(0 ~ 1)
         * @param blockerDepth   가리개 평균 NDC 깊이
         * @param penumbraScale  `computeDirectionalPenumbraScale`
         * @param cascadeScale   캐스케이드 배율(기준 볼륨 폭 / 캐스케이드 폭)
         */
        [[nodiscard]] static float32 computeDirectionalKernelRadius( float32 receiverDepth, float32 blockerDepth, float32 penumbraScale, float32 cascadeScale );

        /**
         * @brief 점광 · 스폿 PCSS 커널 반지름(텍셀)입니다. 셰이더 `LightData::sampleLocalShadow` 의 식과 같습니다.
         * @param receiverDistance 받는 점의 빛 시선 깊이(m)
         * @param blockerDistance  가리개 평균 시선 깊이(m)
         * @param sourceRadius     원천 반지름(m)
         * @param texelAngle       거리 1 m 에서 텍셀 하나의 월드 길이
         * @details 닮은 삼각형으로 반그림자 폭 = 2 · 원천 반지름 · (받는 깊이 − 가리개 깊이) / 가리개 깊이(받는 면 위 m) 이고, 받는 면에서 텍셀 하나는 texelAngle · 받는 깊이다.
         */
        [[nodiscard]] static float32 computeLocalKernelRadius( float32 receiverDistance, float32 blockerDistance, float32 sourceRadius, float32 texelAngle );

        /** @brief 원근 그림자의 NDC 깊이(0 ~ 1, D3D 규약)를 시선 깊이(m)로 되돌립니다. 셰이더의 `Shadow::linearizePerspectiveDepth` 와 같습니다. */
        [[nodiscard]] static float32 linearizePerspectiveDepth( float32 ndcDepth, float32 nearPlane, float32 farPlane );

        /** @brief 셰이더의 `g_ShadowFilter` 입니다 — x 품질, y 방향광 반그림자 배율, z 커널 반지름 상한(텍셀), w 하한(텍셀). */
        [[nodiscard]] static float4 makeShaderParams( ShadowFilterMode mode, float32 directionalPenumbraScale );
    };
} // namespace sw
