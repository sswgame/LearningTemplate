/**
 * @file EditorInputActionNames.h
 * @brief 명령이 아닌 에디터 입력 액션(코드가 `EditorInputGUI::wasActionTriggered` · `isActionDown` 으로 읽는 것)의 이름 상수입니다.
 * @details 단축키 데이터(`Content/editor/input/editor.input.xml`)의 액션은 명령의 액션(`*.commands.xml` 의 `_inputAction`)이거나 여기 이름이어야 한다 —
 *          에디터가 명령 표를 맞출 때 둘 다 아닌 액션(오타 · 지운 명령의 남은 줄)과 입력 맵에 없는 여기 이름을 오류로 멈춘다(`EditorDefaultCommands`).
 *          언리얼은 단축키가 C++ 명령 멤버(`TCommands<>`)를 가리켜 오타가 컴파일 오류이고, 여기는 단축키가 데이터라 읽을 때 대조한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor::editorinput
{
    inline constexpr const utf8* kConsoleCopySelection          = "console.copySelection";
    inline constexpr const utf8* kContentBrowserNavigateBack    = "contentBrowser.navigateBack";
    inline constexpr const utf8* kContentBrowserNavigateForward = "contentBrowser.navigateForward";
    inline constexpr const utf8* kHierarchySelectAll            = "hierarchy.selectAll";
    inline constexpr const utf8* kHierarchySelectNext           = "hierarchy.selectNext";
    inline constexpr const utf8* kHierarchySelectPrevious       = "hierarchy.selectPrevious";
    inline constexpr const utf8* kPropertyCopyValue             = "property.copyValue";
    inline constexpr const utf8* kPropertyPasteValue            = "property.pasteValue";
    inline constexpr const utf8* kTileMapBrush                  = "tileMap.brush";
    inline constexpr const utf8* kTileMapFill                   = "tileMap.fill";
    inline constexpr const utf8* kTileMapLine                   = "tileMap.line";
    inline constexpr const utf8* kTileMapPicker                 = "tileMap.picker";
    inline constexpr const utf8* kTileMapRect                   = "tileMap.rect";
    inline constexpr const utf8* kViewFrameHovered              = "view.frameHovered";
    inline constexpr const utf8* kViewportFlyBack               = "viewport.flyBack";
    inline constexpr const utf8* kViewportFlyDown               = "viewport.flyDown";
    inline constexpr const utf8* kViewportFlyForward            = "viewport.flyForward";
    inline constexpr const utf8* kViewportFlyLeft               = "viewport.flyLeft";
    inline constexpr const utf8* kViewportFlyRight              = "viewport.flyRight";
    inline constexpr const utf8* kViewportFlyUp                 = "viewport.flyUp";
    inline constexpr const utf8* kViewportFrameSelected         = "viewport.frameSelected";
    inline constexpr const utf8* kViewportGizmoCycle            = "viewport.gizmoCycle";
    inline constexpr const utf8* kViewportGizmoRotate           = "viewport.gizmoRotate";
    inline constexpr const utf8* kViewportGizmoScale            = "viewport.gizmoScale";
    inline constexpr const utf8* kViewportGizmoTranslate        = "viewport.gizmoTranslate";
    inline constexpr const utf8* kViewportPlacementRotate       = "viewport.placementRotate";
    inline constexpr const utf8* kViewportPlacementRotateBack   = "viewport.placementRotateBack";
    inline constexpr const utf8* kViewportRulerHold             = "viewport.rulerHold";
    inline constexpr const utf8* kViewportToggleMaximize        = "viewport.toggleMaximize";
    inline constexpr const utf8* kViewportToggleOrthographic    = "viewport.toggleOrthographic";
    inline constexpr const utf8* kViewportViewFront             = "viewport.viewFront";
    inline constexpr const utf8* kViewportViewRight             = "viewport.viewRight";
    inline constexpr const utf8* kViewportViewTop               = "viewport.viewTop";

    /** @brief 씬 뷰 북마크로 가기(1 ~ 9)입니다. */
    inline constexpr const utf8* kArrViewportBookmark[] = { "viewport.bookmark1", "viewport.bookmark2", "viewport.bookmark3", "viewport.bookmark4", "viewport.bookmark5", "viewport.bookmark6", "viewport.bookmark7", "viewport.bookmark8", "viewport.bookmark9" };
    /** @brief 씬 뷰 북마크 저장(Ctrl+1 ~ 9)입니다. */
    inline constexpr const utf8* kArrViewportSetBookmark[] = { "viewport.setBookmark1", "viewport.setBookmark2", "viewport.setBookmark3", "viewport.setBookmark4", "viewport.setBookmark5", "viewport.setBookmark6", "viewport.setBookmark7", "viewport.setBookmark8", "viewport.setBookmark9" };

    /** @brief 북마크 밖의 코드 액션 전부입니다(입력 맵 대조용). */
    inline constexpr const utf8* kArrCodeAction[] = {
        kConsoleCopySelection,
        kContentBrowserNavigateBack,
        kContentBrowserNavigateForward,
        kHierarchySelectAll,
        kHierarchySelectNext,
        kHierarchySelectPrevious,
        kPropertyCopyValue,
        kPropertyPasteValue,
        kTileMapBrush,
        kTileMapFill,
        kTileMapLine,
        kTileMapPicker,
        kTileMapRect,
        kViewFrameHovered,
        kViewportFlyBack,
        kViewportFlyDown,
        kViewportFlyForward,
        kViewportFlyLeft,
        kViewportFlyRight,
        kViewportFlyUp,
        kViewportFrameSelected,
        kViewportGizmoCycle,
        kViewportGizmoRotate,
        kViewportGizmoScale,
        kViewportGizmoTranslate,
        kViewportPlacementRotate,
        kViewportPlacementRotateBack,
        kViewportRulerHold,
        kViewportToggleMaximize,
        kViewportToggleOrthographic,
        kViewportViewFront,
        kViewportViewRight,
        kViewportViewTop,
    };
} // namespace sw::editor::editorinput
