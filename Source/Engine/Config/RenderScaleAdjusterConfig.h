/**
 * @file RenderScaleAdjusterConfig.h
 * @brief 자동 렌더 배율 조정기(`RenderScaleAdjuster`)의 설정입니다. 값은 `Config/Engine/RenderScale.json` 에 있고 이 헤더는 모양만 둡니다.
 * @details 설정 층(1)에 두어 설정 파일 검사(`ConfigFileSchemaTest`)가 렌더러 층 없이 읽는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct RenderScaleAdjusterConfig
     * @brief 조정기의 설정입니다(`Config/Engine/RenderScale.json` — 모든 칸이 필수). 맞지 않으면 `RenderScaleAdjuster::configure` 가 false 를 돌려줍니다(기본값으로 대신하지 않는다).
     */
    REFLECT()
    struct SW_API RenderScaleAdjusterConfig : IConfig
    {
        REFLECT_BODY();

        /** @brief 설정 파일의 위치입니다(프로젝트 폴더 기준). */
        static constexpr const utf8* kConfigFile = "Config/Engine/RenderScale.json";

        PROPERTY( Required, Tooltip = "GPU frame time budget in ms (> 0)" )
        float32 _budgetMs{ 0.0f };
        PROPERTY( Required, Tooltip = "Lowest auto render scale, in (0, 1]" )
        float32 _minScale{ 0.0f };
        PROPERTY( Required, Tooltip = "Scale removed per step when the budget is broken (> 0)" )
        float32 _stepDown{ 0.0f };
        PROPERTY( Required, Tooltip = "Scale added per step while there is headroom (> 0, smaller than stepDown so the scale does not oscillate)" )
        float32 _stepUp{ 0.0f };
        PROPERTY( Required, Tooltip = "The budget is broken when GPU time exceeds budget times this (>= 1)" )
        float32 _overRatio{ 0.0f };
        PROPERTY( Required, Tooltip = "There is headroom when GPU time is under budget times this, in (0, 1)" )
        float32 _headroomRatio{ 0.0f };
        PROPERTY( Required, Tooltip = "Consecutive broken samples before stepping down (>= 1)" )
        uint32 _downFrames{ 0 };
        PROPERTY( Required, Tooltip = "Consecutive headroom samples before stepping up (>= 1)" )
        uint32 _upFrames{ 0 };
        PROPERTY( Required, Tooltip = "Samples ignored after a step - they were drawn at the old scale (readback ring + 1)" )
        uint32 _discardSamples{ 0 };
    };
} // namespace sw
