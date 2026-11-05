/**
 * @file AutomationStepFields.h
 * @brief 엔진 시나리오 단계의 인자 스키마입니다 — 받는 속성 · 필수 · 범위 · 기본값이 `PROPERTY` 한 곳에 있습니다(`AutomationStep::readFields`).
 * @details 언리얼 자동화 스펙의 `USTRUCT` 인자와 같은 자리입니다. 칸 사이 의미 검사(`.png` 끝 · 등록된 명령 이름)만 단계의 검사 함수가 손으로 합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief `PostWindowMessage` 가 보내는 창 메시지입니다. */
    ENUM()
    enum class AutomationWindowMessage : uint8
    {
        ActivateInactive,
        ActivateActive,
        KillFocus,
        SetFocus,
        Close,
        LeftButtonDownClient,
        LeftButtonUpClient,
    };

    /** @brief `ExpectCursorClip` 이 기대하는 커서 상태입니다. */
    ENUM()
    enum class AutomationCursorClipState : uint8
    {
        Locked,
        Free,
    };

    /** @brief `PostWindowMessage` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationWindowMessageFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        AutomationWindowMessage _message{ AutomationWindowMessage::ActivateInactive };
    };

    /** @brief `ExpectCursorClip` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationCursorClipFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        AutomationCursorClipState _state{ AutomationCursorClipState::Free };
    };

    /** @brief 파일 하나를 쓰는 단계(`CaptureWindow` · `EndFrameStats` · `DumpUILayout`)의 인자입니다. */
    REFLECT()
    struct SW_API AutomationFileStepFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _file{};
    };

    /** @brief `LoadScene` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationLoadSceneFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _path{};
        PROPERTY( Min = 1 )
        uint32 _timeoutFrames{ 600 };
    };

    /** @brief `PlaySequence` · `WaitSequence` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationSequenceStepFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _object{};
        PROPERTY( Min = 1 )
        uint32 _timeoutFrames{ 3600 };
    };

    /** @brief `DevCommand` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationDevCommandFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _line{};
    };

    /** @brief `ResizeWindow` 의 인자입니다(픽셀). */
    REFLECT()
    struct SW_API AutomationResizeWindowFields
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 1, Max = 16384 )
        uint32 _width{ 1 };
        PROPERTY( Required, Min = 1, Max = 16384 )
        uint32 _height{ 1 };
    };

    /** @brief `SwapBackend` 의 인자입니다(`AutomationBackendSteps::tryParseBackend` 의 이름 · `other` · `start`). */
    REFLECT()
    struct SW_API AutomationSwapBackendFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _backend{};
    };

    /** @brief 액션 단계가 고를 입력 장치입니다. `Any` 면 입력 맵의 첫 바인딩입니다. */
    ENUM()
    enum class AutomationActionDevice : uint8
    {
        Any,
        Keyboard,
        Mouse,
        Gamepad,
    };

    /** @brief `ActionPress` · `ActionRelease` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationActionButtonFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY()
        AutomationActionDevice _device{ AutomationActionDevice::Any };
    };

    /** @brief 초 칸의 "적지 않음" 입니다 — `PROPERTY( Min = 0 )` 는 적힌 값만 보므로 음수 초기값이 곧 적지 않음입니다. */
    inline constexpr float32 kAutomationNoSeconds = -1.0f;
    /** @brief 프레임 칸의 "적지 않음" 입니다. */
    inline constexpr uint32 kAutomationNoFrames = 0xFFFFFFFFu;

    /** @brief `Action` 의 인자입니다. 누르는 길이와 간격은 프레임 또는 초 둘 중 하나로 적습니다. */
    REFLECT()
    struct SW_API AutomationActionFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY()
        uint32 _hold{ kAutomationNoFrames };
        PROPERTY( Min = 0 )
        float32 _seconds{ kAutomationNoSeconds };
        PROPERTY()
        uint32 _count{ 1 };
        PROPERTY()
        uint32 _interval{ kAutomationNoFrames };
        PROPERTY( Min = 0 )
        float32 _intervalSeconds{ kAutomationNoSeconds };
        PROPERTY()
        AutomationActionDevice _device{ AutomationActionDevice::Any };
    };

    /** @brief `ActionAxis` 의 인자입니다. 길이는 프레임 또는 초 둘 중 하나로 적습니다. */
    REFLECT()
    struct SW_API AutomationActionAxisFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY()
        float32 _x{ 0.0f };
        PROPERTY()
        float32 _y{ 0.0f };
        PROPERTY()
        uint32 _frames{ kAutomationNoFrames };
        PROPERTY( Min = 0 )
        float32 _seconds{ kAutomationNoSeconds };
        PROPERTY()
        AutomationActionDevice _device{ AutomationActionDevice::Any };
    };

    /** @brief 인자가 없는 단계의 스키마입니다 — 적힌 속성은 모두 오타입니다. */
    REFLECT()
    struct SW_API AutomationNoFields
    {
        REFLECT_BODY();
    };
} // namespace sw
