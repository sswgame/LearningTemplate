/**
 * @file ControlAutomationParams.h
 * @brief 조종 시나리오 단계(`Intent` · `Possess`)의 인자 스키마입니다(`AutomationStep::readProperties`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief `Intent` 의 인자입니다. `yaw` · `pitch` 는 적지 않으면 폰의 지금 값을 그대로 둡니다(적었는지가 뜻이라 손으로 읽습니다). */
    REFLECT()
    struct SW_GF_API ControlIntentStepParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _pawn{};
        PROPERTY()
        float2 _move{};
        PROPERTY()
        float32 _up{ 0.0f };
        PROPERTY()
        string _buttons{}; ///< 버튼 이름을 쉼표로 잇는다
        PROPERTY( Min = 1 )
        uint32 _frames{ 1 };
    };

    /** @brief `Possess` 의 인자입니다. `pawn` 을 적지 않으면 놓습니다. */
    REFLECT()
    struct SW_GF_API ControlPossessStepParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _controller{};
        PROPERTY()
        string _pawn{};
    };
} // namespace sw
