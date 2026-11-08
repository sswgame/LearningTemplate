/**
 * @file GameViewPanel.h
 * @brief 활성 씬의 게임 카메라 출력만 보이는 Game 창입니다 — 격자 · 기즈모 · 선택 외곽선 없이 화면 UI 까지 포함한 게임 화면.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorPanelID.h"
#include "Editor/Panels/Scene/Model/GameViewModel.h"

namespace sw::editor
{
    /**
     * @class GameViewPanel
     * @brief 게임 뷰 RT(`EditorViewKind::Game`)를 화면 비율에 맞춰 그립니다(유니티 Game 뷰). Play 중 게임 입력은 이 패널이 포커스일 때만 게임으로 갑니다.
     * @details 게임 카메라가 없거나 꺼져 있으면 "No camera rendering" 을 띄웁니다. 패널이 보이지 않으면 게임 뷰를 그리지 않습니다(RT 요청 0).
     *          상태와 입력 넘기기 · 디버그 드로우 명령은 `GameViewModel`(Model/, ImGui 없음)이 들고, 이 패널은 ImGui 창 상태를 읽어 넘기고 그리기만 한다.
     */
    class GameViewPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = editorpanel::kGameView.c_str();
        /** @brief 창 제목입니다(ImGui 창 이름 — 자체 테스트가 창을 찾을 때 이 상수를 쓴다). */
        static constexpr const utf8* kPanelTitle = "Game";

        /** @brief Game 창을 만듭니다. */
        GameViewPanel();

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8*    getPanelTitle() const override { return kPanelTitle; }
        EditorDockSlot getDefaultDockSlot() const override { return EditorDockSlot::Center; }
        /** @brief 화면 비율 · HUD 툴바와 게임 화면을 그립니다. */
        void drawContent() override;
        /** @brief 창이 숨겨지면 게임 뷰 포커스를 해제합니다(게임 입력이 끊긴다). */
        void onPanelCollapsed() override;
        /** @brief 개발 겹치기 텍스처의 ImGui 등록과 캡처 제외를 놓습니다. */
        void shutdown() override;

        /** @brief 마지막 프레임에 "No camera rendering" 안내를 띄웠으면 true 입니다. */
        bool wasNoCameraHintShown() const { return _model.isNoCameraHintShown(); }

    private:
        /** @brief 화면 비율 고르기와 HUD 토글을 그립니다. */
        void drawToolbar();
        /** @brief 패널 창이 주 뷰포트가 아니면 포인터 · 키를 읽어 모델에 넘깁니다(보조 창의 입력은 ImGui 백엔드만 받는다 — `GameViewModel::forwardDetachedPointer`). */
        void forwardDetachedInput();
        /**
         * @brief 개발 겹치기(디버그 HUD — 엔진이 게임 뷰 RT 대신 `CanvasFrameData::kDevOverlayTargetPath` 에 그린다)를 게임 이미지 위에 겹칩니다.
         * @details 그 텍스처는 에디터 UI 캡처에서 뺀다(`EditorUICapture::setTextureExcluded`) — 게임의 사진처럼 개발 겹치기는 캡처 뒤의 층이다.
         *          유니티 게임 뷰의 Stats 겹치기 · 언리얼 PIE 뷰포트의 `stat` 이 게임 화면 위 따로 그려지는 것과 같다.
         */
        void drawDevOverlay( const float2& imagePos, const float2& imageSize );
        /** @brief 개발 겹치기 텍스처의 ImGui 등록을 렌더 텍스처 핸들에 맞춥니다(바뀌었으면 옛 것을 놓고 새로 등록한다). */
        void syncDevOverlayTexture();
        void releaseDevOverlayTexture();

    private:
        GameViewModel _model;
        void*         _pDevOverlayTextureID; ///< 개발 겹치기 텍스처의 ImGui ID(이 패널 소유, 없으면 nullptr)
        uint64        _devOverlayTexture;    ///< 그 ID 의 RHI 텍스처
    };
} // namespace sw::editor
