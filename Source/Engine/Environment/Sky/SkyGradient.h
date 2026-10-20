/**
 * @file SkyGradient.h
 * @brief 하늘 그러데이션 한 벌(천정 · 지평선 · 땅 색, 세기, 지평선 지수)과 방향마다의 하늘빛 식입니다.
 * @details 값은 씬의 `SkyComponent` 가 정하고, 렌더 패킷(`RenderFramePacket::_sky`)이 렌더 스레드로 나른다. 렌더러가 컴포넌트 헤더를 보지 않도록 값 타입만 따로 둔다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    /**
     * @brief 하늘 그러데이션 한 벌입니다. 셰이더(`sky.hlsl` 의 `evaluateSky`)와 같은 식을 CPU 에서 셉니다.
     * @details 위쪽은 천정 색에서 지평선 색으로, 아래쪽은 땅 색에서 지평선 색으로 간다. 지평선 쪽 비율은 `(1 - |y|)^지수` 이고 마지막에 세기를 곱한다.
     *          지수가 클수록 지평선 띠가 좁다. 셰이더 상수로는 `g_SkyZenith`(rgb, w = 지수) · `g_SkyHorizon`(rgb) · `g_SkyGround`(rgb, w = 세기)로 싣는다.
     *          같은 식을 간접 조명 베이크가 프로브마다 SH 로 투영한다(계획 `IndirectLighting` P2).
     */
    struct SW_API SkyGradient
    {
        float3  _zenithColor{};           ///< 바로 위(+y)의 색(선형 RGB)
        float3  _horizonColor{};          ///< 지평선의 색(선형 RGB)
        float3  _groundColor{};           ///< 바로 아래(-y)의 색(선형 RGB)
        float32 _intensity{ 0.0f };       ///< 세 색에 곱하는 세기
        float32 _horizonExponent{ 1.0f }; ///< 지평선 쪽 비율 `(1 - |y|)` 의 지수(0 보다 크다)

        /** @brief 방향 @p direction(길이는 상관없다, 0 벡터는 지평선)에서 오는 하늘빛입니다. */
        float3 evaluateRadiance( const float3& direction ) const;
    };
} // namespace sw
