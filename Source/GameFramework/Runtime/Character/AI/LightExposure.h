/**
 * @file LightExposure.h
 * @brief 한 위치가 씬의 라이트에 얼마나 드러나는지 계산하는 빛 노출 질의입니다(잠입 · AI 시각).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    /**
     * @struct LightExposure
     * @brief 잠입의 빛 노출입니다. 한 위치가 씬의 라이트(점, 스폿, 방향)에 얼마나 드러나는지 계산하며, 0 이면 어둠입니다. 점 라이트와 스폿 라이트는 반경 안에서 (1 - d/r)² × 세기이고(스폿은 원뿔 밖이면 0),
     *        방향광은 세기 그대로입니다. @p bOcclusion 이 켜져 있으면 라이트까지 광선이 막힐 때 그 라이트를 뺍니다(방향광은 라이트 반대쪽 100 m 까지 검사). 꺼져 있으면 가림을 검사하지 않습니다.
     */
    struct SW_GF_API LightExposure
    {
        static float32 computeExposure( const GameObjectManager& manager, const float3& position, bool bOcclusion, uint64 ignoreObjectID = 0 );
    };
} // namespace sw
