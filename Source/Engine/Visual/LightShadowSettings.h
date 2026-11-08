/**
 * @file LightShadowSettings.h
 * @brief 3D 빛 하나의 그림자 모양 값입니다 — 빛 원천 크기(PCSS 반그림자)와 접촉 그림자 길이를 담습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct GPULight;

    /**
     * @struct LightShadowSettings
     * @brief 방향광 · 점광 · 스폿이 함께 쓰는 그림자 모양 값입니다. 빛마다 데이터로 정합니다.
     * @details 언리얼의 `SourceRadius`(점광 · 스폿) · `LightSourceAngle`(방향광)와 같은 자리입니다. 원천 크기는 PCSS(그림자 필터 `gv_shadowFilter` 1)만 읽고,
     *          값싼 3x3 PCF 는 읽지 않습니다 — 그래서 기본값이 있어도 PCF 의 그림은 바뀌지 않습니다.
     *          접촉 그림자는 화면 공간에서 빛 쪽으로 짧게 걸어 그림자 맵이 놓친 가까운 가림(발치 · 틈)을 잡습니다(언리얼 `ContactShadowLength` 자리, 0 = 끔).
     *          켜진 빛은 한 프레임에 넷까지 접촉 그림자 패스(`RenderPassType::ContactShadow`)의 채널을 받습니다(`FrameShadowData::populateContactShadows`).
     *          GPU 원소의 `_extra.x` 원천 크기 · `_extra.y` 접촉 길이에 실립니다(`writeGPULightFields`, 3D 빛의 덧칸 규약은 `GPULight::_extra`).
     */
    REFLECT()
    struct SW_API LightShadowSettings
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Light source size for soft shadows (PCSS): emitter radius in m for point and spot lights, angular diameter in degrees for directional lights" )
        float32 _sourceSize{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Screen-space contact shadow ray length toward the light (0 = off); catches short-range occlusion the shadow map misses", Units = m )
        float32 _contactShadowLength{ 0.0f };

        /** @brief 이 값을 3D 빛 원소의 덧칸(`_extra`)에 씁니다. 방향광의 원천 크기는 원소가 아니라 그림자 투영이 쓰지만(`DirectionalShadowProjection::_sourceAngle`) 같은 칸에 함께 싣습니다. */
        void writeGPULightFields( GPULight& outLight ) const;
    };
} // namespace sw
