/**
 * @file AutomationStepParams.h
 * @brief 엔진 시나리오 단계의 인자 스키마입니다 — 받는 속성 · 필수 · 범위 · 기본값이 `PROPERTY` 한 곳에 있습니다(`AutomationStep::readProperties`).
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
    struct SW_API AutomationWindowMessageParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        AutomationWindowMessage _message{ AutomationWindowMessage::ActivateInactive };
    };

    /** @brief `SimulateAppSuspend` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationAppSuspendParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Main loop ticks the app stays suspended; the scenario does not run during them" )
        uint32 _ticks{ 0 };
    };

    /** @brief `ExpectCursorClip` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationCursorClipParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        AutomationCursorClipState _state{ AutomationCursorClipState::Free };
    };

    /** @brief 파일 하나를 쓰는 단계(`CaptureWindow` · `EndFrameStats` · `DumpUILayout`)의 인자입니다. */
    REFLECT()
    struct SW_API AutomationFileStepParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _file{};
    };

    /** @brief `<Scratch from as>` 의 인자입니다 — 원본 폴더(추적 중인 픽스처)와 사본 폴더(`scratch/…`). */
    REFLECT()
    struct SW_API AutomationScratchParams
    {
        REFLECT_BODY();

        PROPERTY( AssetPath, Required )
        string _from{};
        PROPERTY( Required )
        string _as{};
    };

    /** @brief `ExpectUI` 의 인자입니다. 적은 칸만 본다(셋 중 하나 이상) — 판정은 적었는지를 단계 속성으로 가른다(`screen="None"` 은 화면 없음). */
    REFLECT()
    struct SW_API AutomationExpectUIParams
    {
        REFLECT_BODY();

        PROPERTY()
        string _focus{};
        PROPERTY( AssetPath )
        string _screen{};
        PROPERTY( Min = 0 )
        int32 _screens{ 0 };
    };

    /** @brief `LoadScene` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationLoadSceneParams
    {
        REFLECT_BODY();

        PROPERTY( AssetPath, Required )
        string _path{};
        PROPERTY( Min = 1 )
        uint32 _timeoutFrames{ 600 };
    };

    /** @brief `RecordStateHash` · `ExpectStateHash` 의 인자입니다 — 같은 `key` 로 앞에서 잰 활성 씬 상태 해시를 뒤에서 견준다. */
    REFLECT()
    struct SW_API AutomationStateHashStepParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _key{}; ///< 기억할 이름(한 시나리오 안에서)
    };

    /** @brief `PlaySequence` · `WaitSequence` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationSequenceStepParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _object{};
        PROPERTY( Min = 1 )
        uint32 _timeoutFrames{ 3600 };
    };

    /** @brief `DevCommand` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationDevCommandParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _line{};
    };

    /** @brief `ResizeWindow` 의 인자입니다(픽셀). */
    REFLECT()
    struct SW_API AutomationResizeWindowParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 1, Max = 16384 )
        uint32 _width{ 1 };
        PROPERTY( Required, Min = 1, Max = 16384 )
        uint32 _height{ 1 };
    };

    /** @brief `SwapBackend` 의 인자입니다(`AutomationBackendSteps::tryParseBackend` 의 이름 · `other` · `start`). */
    REFLECT()
    struct SW_API AutomationSwapBackendParams
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
    struct SW_API AutomationActionButtonParams
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
    struct SW_API AutomationActionParams
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
    struct SW_API AutomationActionAxisParams
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
    struct SW_API AutomationNoParams
    {
        REFLECT_BODY();
    };
} // namespace sw
