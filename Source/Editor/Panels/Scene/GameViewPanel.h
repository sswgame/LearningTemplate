/**
 * @file GameViewPanel.h
 * @brief 활성 씬의 게임 카메라 출력만 보이는 Game 창입니다 — 격자 · 기즈모 · 선택 외곽선 없이 화면 UI 까지 포함한 게임 화면.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorPanelID.h"
#include "Editor/Common/Workspace/EditorViewTargetUtil.h"

#include "Engine/Input/KeyCodeUtil.h"

namespace sw::editor
{
    /**
     * @class GameViewPanel
     * @brief 게임 뷰 RT(`EditorViewKind::Game`)를 화면 비율에 맞춰 그립니다(유니티 Game 뷰). Play 중 게임 입력은 이 패널이 포커스일 때만 게임으로 갑니다.
     * @details 게임 카메라가 없거나 꺼져 있으면 "No camera rendering" 을 띄웁니다. 패널이 보이지 않으면 게임 뷰를 그리지 않습니다(RT 요청 0).
     */
    class GameViewPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = editorpanel::kGameView.c_str();

        /** @brief Game 창을 만듭니다. */
        GameViewPanel();

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8*    getPanelTitle() const override { return "Game"; }
        EditorDockSlot getDefaultDockSlot() const override { return EditorDockSlot::Center; }
        /** @brief 화면 비율 · HUD 툴바와 게임 화면을 그립니다. */
        void drawContent() override;
        /** @brief 창이 숨겨지면 게임 뷰 포커스를 해제합니다(게임 입력이 끊긴다). */
        void onPanelCollapsed() override;

        /** @brief 마지막 프레임에 "No camera rendering" 안내를 띄웠으면 true 입니다. */
        bool wasNoCameraHintShown() const { return _bNoCameraHintShown; }

    private:
        /** @brief 화면 비율 고르기와 HUD 토글을 그립니다. */
        void drawToolbar();
        /**
         * @brief 패널이 다른 OS 창(ImGui 보조 뷰포트)에 있으면 그 창의 마우스(이동 · 단추 · 휠)를 주 창 클라이언트 좌표로 옮겨 게임 입력에 넣습니다.
         * @details 보조 창의 마우스 메시지는 ImGui 백엔드의 창 프로시저만 받아 게임 입력(`InputManager`)에 닿지 않는다. 입력 장치의 좌표 약속은 "주 창 클라이언트 픽셀" 이므로
         *          ImGui 화면 좌표에서 주 뷰포트 자리를 빼서 넣는다 — 이미지 자리(`markViewDrawn`)와 같은 기준이라 런타임 UI 포인터가 맞는다. 주 창에 있으면 주 창 메시지가 이미 간다.
         */
        void forwardDetachedPointer();
        /**
         * @brief 패널이 다른 OS 창에서 포커스이면 그 창의 키 · 글자 입력을 게임 입력에 넣습니다(`ImGuiKeyForward`).
         * @details 보조 창의 키 메시지도 ImGui 백엔드만 받는다. 넣은 누름의 짝 뗌은 포커스를 잃어도 넣는다 — 안 넣으면 게임에서 키가 눌린 채 남는다.
         *          한글 조합(IME) 중인 글은 넘기지 않는다 — 조합이 끝나 확정된 글만 간다.
         */
        void forwardDetachedKeyboard();

    private:
        EditorGameViewAspect _aspect;              ///< 이미지 화면 비율(자유 · 16:9)
        bool                 _bNoCameraHintShown;  ///< 마지막 프레임에 카메라 없음 안내를 띄웠다
        uint8                _forwardedButtonMask; ///< 보조 창에서 게임으로 누름을 넣은 단추(비트 = `kForwardedButtons` 순서). 패널 밖에서 떼도 뗌을 넣는다
        vector<Key>          _listForwardedKey;    ///< 보조 창에서 게임으로 누름을 넣고 아직 뗌을 넣지 않은 키
    };
} // namespace sw::editor
