/**
 * @file RainOcclusionComponent.h
 * @brief 씬에 비 가림 맵을 켭니다 — 지붕 아래의 표면은 젖지 않고 GPU 강수가 들지 않습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Environment/Rain/RainOcclusionSettings.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class RainOcclusionComponent
     * @brief 비 가림 맵 하나입니다. 씬에 하나다(`WindComponent` 와 같은 규칙). 없으면 맵도 비용도 없고 표면은 어디서나 젖습니다.
     * @details 날씨 값(젖음 · 웅덩이 · 눈 · 비)이 모두 0 이고 이 맵을 읽는 GPU 강수도 없는 프레임은 그리지 않는다(`FrameRainOcclusion`).
     */
    REFLECT( Category = "Environment", DisplayName = "Rain Occlusion", Tooltip = "Top-down depth map following the main view that keeps wetness, snow and GPU rain out of covered places", Meta = "Icon=Wind, IconColor=Environment" )
    class SW_API RainOcclusionComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( RainOcclusionComponent );

        RainOcclusionComponent();
        virtual ~RainOcclusionComponent() override = default;

        /** @brief 지금 값입니다(해상도는 0 — 품질이 채운다). */
        RainOcclusionSettings makeSettings() const;

        /** @brief @p manager 의 켜진 비 가림(씬에 하나)입니다. 없거나 둘 이상이면(오류) 기본값을 쓰고 false 입니다. 읽기만 합니다. */
        [[nodiscard]] static bool findRainOcclusion( const GameObjectManager& manager, RainOcclusionSettings& outSettings );

    private:
        PROPERTY( Category = "Rain Occlusion", DisplayName = "Extent", Min = 1.0, Units = m, Tooltip = "Side of the square around the main view the map covers; larger is coarser" )
        float32 _extent;
        PROPERTY( Category = "Rain Occlusion", DisplayName = "Height Above", Min = 1.0, Units = m, Tooltip = "Roofs higher than this above the view do not cover" )
        float32 _heightAbove;
        PROPERTY( Category = "Rain Occlusion", DisplayName = "Depth Below", Min = 1.0, Units = m, Tooltip = "How far below the view the map reaches" )
        float32 _depthBelow;
        PROPERTY( Category = "Rain Occlusion", DisplayName = "Depth Bias", Min = 0.0, Units = m, Tooltip = "Height difference still counted as the same surface" )
        float32 _depthBias;
        PROPERTY( Category = "Rain Occlusion", DisplayName = "Normal Offset", Min = 0.0, Units = m, Tooltip = "Surfaces read the map this far along their normal so walls are not covered by their own top" )
        float32 _normalOffset;
    };
} // namespace sw
