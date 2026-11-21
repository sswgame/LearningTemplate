/**
 * @file EditorViewportOverlaysViewModel.h
 * @brief 씬 뷰 오버레이 바(View `EditorViewportOverlays` · 감시 값)가 배치를 읽고 바꾸는 ViewModel 입니다.
 * @details View 는 서비스 창구(`EditorService.h`)를 보지 못한다(`[panelViewBan] patterns`) — 배치의 주인(`SceneViewOverlayLayoutStore`)은 여기서 찾는다.
 *          읽기는 값 사본(`gather` · `findBar`)이라 그리는 중의 명령(리셋)이 주인의 목록을 다시 채워도 View 가 지난 원소를 가리키지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Viewport/Model/EditorViewportOverlayLayout.h"

namespace sw::editor
{
    /** @brief 씬 뷰 오버레이 바의 ViewModel 입니다(상태 없음 — 배치는 `SceneViewOverlayLayoutStore` 가 든다). */
    struct SW_EDITOR_API EditorViewportOverlaysViewModel
    {
        /** @brief 씬 뷰 바(등록된 id)의 배치를 저장 순서대로 @p outListBar 에 채웁니다(비우고 채운다). */
        static void gather( vector<EditorOverlayBarState>& outListBar );
        /** @brief 바 @p barID 의 배치를 @p outState 에 담습니다. 에디터 범위가 없거나(감시 값) 그 바가 없으면 false 입니다. */
        [[nodiscard]] static bool findBar( string_view barID, EditorOverlayBarState& outState );

        /** @brief 모든 바를 기본 위치 · 보임으로 되돌리고 저장합니다. */
        static void resetToDefault();
        /** @brief 바를 보이거나 숨기고 저장합니다. */
        static void setBarVisible( string_view barID, bool bVisible );
        /** @brief 바를 접거나 펴고 저장합니다. */
        static void setBarCollapsed( string_view barID, bool bCollapsed );
        /** @brief 끌어 놓은 위치로 붙은 쪽 · 비율을 정하고 저장합니다(`SceneViewOverlayLayoutStore::dropBar`). */
        static void dropBar( string_view barID, const float2& position, const float2& barSize, const float2& viewSize, float32 snapDistance );
    };
} // namespace sw::editor
