/**
 * @file GameViewPanel.h
 * @brief 씬 프레임버퍼 미리보기와 ImGuizmo 조작을 제공하는 Game View 창입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Gui/IEditorPanel.h"
#include "Editor/Viewport/EditorViewportClient.h"

#include "Engine/Utility/Debug/DebugOverlayState.h"

namespace sw::editor
{
    /** @brief 게임 프레임버퍼를 표시하고 선택된 오브젝트 트랜스폼을 편집합니다 */
    class GameViewPanel : public IEditorPanel
    {
    public:
        /** @brief Game View 창을 만듭니다. */
        GameViewPanel();

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Game View"; }
        /** @brief 게임 캔버스와 선택된 오브젝트의 기즈모를 그립니다. */
        void drawContent() override;
        /** @brief 창이 숨겨지면 Game View 포커스를 해제합니다. */
        void onPanelCollapsed() override;

        /** @brief 마지막 프레임에 그린 디버그 오버레이(`DebugOverlayState`) 줄 수입니다(에디터 자체 시험이 읽습니다). */
        uint32 getLastOverlayRowCount() const { return _lastOverlayRowCount; }

    private:
        /** @brief 플레이를 시작하는 버튼의 종류입니다. 미저장 확인 모달이 어느 쪽을 이어 갈지 기억합니다. */
        enum class PendingSession : uint8
        {
            Play = 0,
            Simulate
        };

        /** @brief Play/Sim/Pause/Step/Stop 버튼을 그립니다. */
        void drawTransportControls();
        /** @brief 시간 배율 · 카메라에서 시작 · 디버그 카테고리 · 오버레이 토글을 그립니다. */
        void drawSessionOptions();
        /** @brief 디버그 드로우 카테고리를 켜고 끄는 팝업을 그립니다. */
        void drawDebugCategoryPopup();
        /** @brief 세션을 시작합니다. 카메라에서 시작이 켜져 있으면 에디터 카메라 위치를 시작 위치로 넘깁니다. */
        void startSession( PendingSession session );
        /** @brief 캔버스 왼쪽 위에 `DebugOverlayState` 의 값을 그립니다. */
        void drawDebugOverlay( const float2& canvasPos );

    private:
        EditorViewportClient    _viewportClient;
        vector<DebugOverlayRow> _listOverlayRow;      ///< 오버레이 줄 재사용 버퍼
        vector<hashed_string>   _listDebugCategory;   ///< 카테고리 팝업 재사용 버퍼
        uint32                  _lastOverlayRowCount; ///< 마지막으로 그린 오버레이 줄 수
        int32                   _stepFrameCount;      ///< "Step N" 이 진행할 프레임 수
        PendingSession          _pendingSession;
        bool                    _bConfirmUnsavedPlay;
        bool                    _bStartAtCamera; ///< Play 를 에디터 카메라 위치에서 시작한다
        bool                    _bShowOverlay;   ///< 디버그 오버레이(게임이 쓰는 값)를 그린다
    };
} // namespace sw::editor
