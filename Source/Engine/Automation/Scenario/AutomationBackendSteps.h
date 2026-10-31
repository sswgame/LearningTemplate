/**
 * @file AutomationBackendSteps.h
 * @brief 시나리오 단계 `SwapBackend` 의 속성 읽기와 검사 함수를 정의합니다. 단계 자체(교체 요청 · 준비)는 App 이 등록하고(`RHIBackendSwitcher.cpp`), 속성만 읽는 이 부분은 App 을 모릅니다.
 * @details 검사 함수가 App 안에 있으면 App 이 없는 프로세스(nogpu 테스트)가 이 단계의 속성을 검사하지 못한다. 그래서 순수 부분을 Engine 으로 두고
 *          `SW_AUTOMATION_STEP_VALIDATOR` 로 따로 올린다(`AutomationStepValidation`). Shipping 에는 단계도 검사도 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Config/RHIBackendType.h"

namespace sw
{
    struct AutomationStep;

    /** @struct AutomationBackendSteps @brief `SwapBackend` 단계의 이름 · 속성 읽기 · 검사 함수 모음입니다. */
    struct SW_API AutomationBackendSteps
    {
        /** @brief 단계의 엘리먼트 이름입니다. */
        static constexpr const utf8* kSwapBackendKind = "SwapBackend";
        /** @brief `backend="other"` — 지금과 다른 백엔드(DirectX 12 가 아니면 DirectX 12, DirectX 12 면 DirectX 11)입니다. Windows 에서 늘 있는 둘이라 어느 백엔드로 시작해도 건넌다. */
        static constexpr const utf8* kOtherBackend = "other";
        /** @brief `backend="start"` — 시나리오가 시작할 때의 백엔드입니다. `other` 로 건넌 뒤 돌아와 체인의 다음 시나리오가 같은 백엔드에서 돌게 한다. */
        static constexpr const utf8* kStartBackend = "start";
        /**
         * @brief 교체 한 번이 시나리오 벽시계 상한에 더하는 초입니다. 교체는 디바이스 · 렌더 단계 · 자원과 함께 게임 · 에디터 모듈과 씬을 다시 세운다.
         * @details Debug 에서 혼자 2~6 초, 다른 빌드가 CPU 를 쓰면 그 몇 배다(flowterrain 의 교체 넷이 혼자서 19 초, 부하에서 150 초 상한을 넘었다).
         */
        static constexpr float32 kSwapBackendWallSeconds = 15.0f;

        /** @brief 명령줄 철자(`dx11`, `dx12`, `vulkan`, `opengl`)와 열거자 이름을 대소문자 없이 받습니다. */
        [[nodiscard]] static bool tryParseBackend( string_view text, RHIBackend& outBackend );
        /** @brief `backend` 하나만 받고, 백엔드 이름 또는 `other` · `start` 여야 합니다. */
        static bool validateSwapBackend( const AutomationStep& step, string& outError );
    };
} // namespace sw
