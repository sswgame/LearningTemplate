/**
 * @file Engine/Input/IInputCaptureQuery.h
 * @brief 게임 입력을 먼저 가져간 쪽(런타임 UI)에게 묻는 창구입니다 — 조종자 · 카메라 · 게임 모드가 UI 를 몰라도 메뉴가 먹은 입력을 거릅니다.
 * @details 언리얼의 입력 소비 · 유니티 `EventSystem.IsPointerOverGameObject` 자리입니다. 구현은 `UISystem` 이고 서지 않았으면 모두 false 입니다. 없으면(UI 를 넣지 않은 하네스) 칸이 비고 거르지 않습니다.
 *          서비스 칸의 정의는 이 폴더에 있어 UI 폴더를 짓지 않는 구성에서도 칸을 읽는 쪽이 링크됩니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Name/hashed_string.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    class InputMap;

    /**
     * @class IInputCaptureQuery
     * @brief 입력 포획 질의입니다. 게임 스레드에서 부릅니다.
     */
    class SW_API IInputCaptureQuery
    {
    public:
        virtual ~IInputCaptureQuery() = default;

        /** @brief 모달 · 로딩 화면처럼 게임 입력을 통째로 막는 것이 떠 있으면 true 입니다. */
        [[nodiscard]] virtual bool isGameInputCaptured() const = 0;
        /** @brief @p action 의 물리 입력을 이미 먹었으면(메뉴에서 누른 패드 A, 위젯이 받은 클릭) 뗄 때까지 true 입니다. */
        [[nodiscard]] virtual bool isActionCaptured( const InputMap& inputMap, const hashed_string& action ) const = 0;
        /** @brief 커서를 바라면(메뉴가 열렸다) true 입니다 — 마우스 잠금 · 시선을 쉽니다. */
        [[nodiscard]] virtual bool shouldShowCursor() const = 0;

        SW_SERVICE_SLOT( IInputCaptureQuery, Optional );
    };
} // namespace sw
