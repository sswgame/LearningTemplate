/**
 * @file GameplayAutomationParams.h
 * @brief 게임플레이 시나리오 단계(`AdvanceGameTime` · `Mount` · `Dismount`)의 인자 스키마입니다(`AutomationStep::readProperties`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief `AdvanceGameTime` 칸의 "적지 않음" 입니다 — `Min` 은 적힌 값만 보므로 음수 초기값이 곧 적지 않음입니다. */
    inline constexpr float32 kAdvanceGameTimeUnset = -1.0f;

    /** @brief `AdvanceGameTime` 의 인자입니다. 셋 중 정확히 하나를 적습니다. */
    REFLECT()
    struct SW_GF_API AdvanceGameTimeParams
    {
        REFLECT_BODY();

        PROPERTY( Min = 0 )
        float32 _minutes{ kAdvanceGameTimeUnset };
        PROPERTY( Min = 0 )
        float32 _hours{ kAdvanceGameTimeUnset };
        PROPERTY( Min = 0, Max = 24 )
        float32 _toHour{ kAdvanceGameTimeUnset };
    };

    /** @brief `Mount` 의 인자입니다. */
    REFLECT()
    struct SW_GF_API MountStepParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _rider{};
        PROPERTY( Required )
        string _vehicle{};
    };

    /** @brief `Dismount` 의 인자입니다. */
    REFLECT()
    struct SW_GF_API DismountStepParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _rider{};
    };
} // namespace sw
