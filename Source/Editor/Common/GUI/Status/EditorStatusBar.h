/**
 * @file EditorStatusBar.h
 * @brief 메뉴 막대 오른쪽 상태 영역에 항목 하나를 더하는 등록 줄(`SW_EDITOR_STATUS_BAR_ITEM`)입니다.
 */
#pragma once
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw::editor
{
    /**
     * @struct EditorStatusBarItemRegistration
     * @brief 상태 영역 항목 하나입니다. 메뉴 막대는 항목을 모르고 순서대로 그리기만 합니다(언리얼 레벨 에디터 상태 막대의 확장 지점).
     * @details 그리기 함수는 ImGui 항목 몇 개를 그리고 끝에 `ImGui::SameLine` 을 부르지 않습니다 — 메뉴 막대가 항목 사이에 넣습니다.
     *          맵 검사 수(`MapCheckPanel.cpp`)가 첫 사용처이고, 확장 모듈도 같은 줄로 더합니다.
     */
    struct EditorStatusBarItemRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "statusBarItem";

        void ( *_pfnDraw )();
    };
} // namespace sw::editor

/** @code SW_EDITOR_STATUS_BAR_ITEM( MapCheck, "map_check", 100, &drawMapCheckStatus ); @endcode */
#define SW_EDITOR_STATUS_BAR_ITEM( name, pID, order, pfnDraw ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorStatusBarItemRegistration, StatusBarItem_##name, { pID, order }, pfnDraw )
