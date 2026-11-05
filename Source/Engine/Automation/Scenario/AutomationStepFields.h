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

    /** @brief 인자가 없는 단계의 스키마입니다 — 적힌 속성은 모두 오타입니다. */
    REFLECT()
    struct SW_API AutomationNoFields
    {
        REFLECT_BODY();
    };
} // namespace sw
