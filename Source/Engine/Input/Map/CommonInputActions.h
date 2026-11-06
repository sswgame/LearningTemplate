/**
 * @file CommonInputActions.h
 * @brief 여러 게임이 같은 뜻으로 쓰는 공용 입력 액션 이름의 표입니다. 정의(키)는 `Content/common/input/*.input.xml` 이 갖습니다.
 * @details 코드가 공용 이름을 적어야 할 때(프로퍼티의 초기값, 씬 데이터로 받지 않는 디렉터) 글자를 흩지 않고 여기서 읽습니다. 씬 · 프리팹 · 시나리오는 데이터에 이름을 그대로 적습니다.
 *          표와 파일이 맞는지는 테스트(`InputMapBundleTest`)가 봅니다. 이름 규칙(공용은 접두어 없음, 한 게임 것은 접두어)은 `docs/InputMapBundle.md`.
 *          상용 엔진: 언리얼의 Native Gameplay Tag(`InputTag_Move`) 표, Godot 의 기본 액션 이름(`ui_accept`)과 같은 자리입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 공용 입력 액션 이름입니다. */
    struct CommonInputActions
    {
        static constexpr const utf8* kMove            = "Move";
        static constexpr const utf8* kJump            = "Jump";
        static constexpr const utf8* kSprint          = "Sprint";
        static constexpr const utf8* kLook            = "Look";
        static constexpr const utf8* kToggleMouseLock = "ToggleMouseLock";
        static constexpr const utf8* kCameraPan       = "Camera.Pan";
        static constexpr const utf8* kCameraRotate    = "Camera.Rotate";
        static constexpr const utf8* kCameraLook      = "Camera.Look";
        static constexpr const utf8* kCameraLookHold  = "Camera.LookHold";
        static constexpr const utf8* kCameraZoom      = "Camera.Zoom";
        static constexpr const utf8* kInteract        = "Interact";
        static constexpr const utf8* kStatus          = "Status";
        static constexpr const utf8* kPause           = "Pause";
        static constexpr const utf8* kSlower          = "Slower";
        static constexpr const utf8* kFaster          = "Faster";
        static constexpr const utf8* kFastForward     = "FastForward";
    };
} // namespace sw
