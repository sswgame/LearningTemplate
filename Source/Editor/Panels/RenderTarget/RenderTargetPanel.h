/**
 * @file RenderTargetPanel.h
 * @brief 프레임 렌더 타깃(G버퍼, 그림자 맵, 포스트 단계)을 찾아보고 화면으로 확인하는 에디터 창입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/RenderTarget/Model/RenderTargetViewModel.h"

namespace sw::editor
{
    /**
     * @class RenderTargetPanel
     * @brief 파이프라인이 만든 렌더 타깃을 나열하고 검색하며, 하나를 골라 미리 봅니다.
     * @details "지금 G버퍼에 무엇이 들어 있는지" 를 앱을 다시 띄우지 않고 확인할 수 있다. 앱 밖에서는 `-gv_screenshotAttachment` 로
     *          PPM 을 한 장 찍는다. RGBA 그대로 볼 때는 GPU 텍스처를 그대로 보여 주고, 채널 하나, 깊이(선형화), 픽셀 값은 렌더 스레드가
     *          CPU 로 읽은 스냅샷으로 본다. 스냅샷은 타깃을 고를 때, Refresh 할 때, Auto 일 때 다시 읽는다.
     * @note 목록 · 스냅샷 · 텍스처 등록은 `RenderTargetViewModel` 이 맡고 이 창은 `RenderTargetViewState` 만 그린다(MVVM 계약).
     */
    class RenderTargetPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "render_targets";

        /** @brief 렌더 타깃 창을 만듭니다. 도구 창이므로 닫힌 채로 시작합니다. */
        RenderTargetPanel();
        /** @brief 소멸자입니다. ImGui 에 등록한 미리보기 텍스처는 shutdown() 이 놓습니다. */
        virtual ~RenderTargetPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel: 제목과 그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Render Targets"; }
        /** @brief 목록, 검색, 미리보기를 그립니다. */
        void drawContent() override;
        /** @brief 기본 창 크기를 반환합니다. */
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 109.0f, 27.0f ); }
        /** @brief 필요할 때 여는 도구이므로 닫힌 채로 시작합니다. */
        /** @brief ImGui 에 등록한 미리보기 텍스처를 해제합니다. */
        void shutdown() override;

        // ------------------------------------------------------------------------------
        // 2) 테스트 프로브. 시나리오가 읽는 값이다
        // ------------------------------------------------------------------------------
        /** @brief 선택한 타깃의 CPU 스냅샷이 있으면 true 입니다. */
        bool hasSnapshot() const { return _viewState._bHasSnapshot != SW_FALSE; }
        /** @brief 선택한 타깃이 깊이 어태치먼트이면 true 입니다. */
        bool isSelectedDepth() const;
        /** @brief 현재 보고 있는 채널을 반환합니다. */
        RenderTargetChannel getChannel() const { return _viewState._viewSettings._channel; }
        /** @brief 깊이를 선형화해 보면 true 입니다. */
        bool isDepthLinearized() const { return _viewState._viewSettings._bLinearizeDepth != SW_FALSE; }
        /** @brief 마우스 아래 픽셀 값을 마지막으로 읽었으면 true 입니다. 마우스가 미리보기 밖으로 나가도 값은 남습니다. */
        bool hasProbedPixel() const { return _viewState._bProbedPixel != SW_FALSE; }
        /** @brief 마지막으로 읽은 픽셀 값입니다. */
        const float4& getProbedPixel() const { return _viewState._probedPixel; }
        /** @brief 선택한 타깃에 쓰는 패스 수와 읽는 패스 수의 합입니다(생산자 · 소비자 표시 — 감시 값 `Editor.RenderTargetPassUses`). 선택이 없으면 0 입니다. */
        uint32 getSelectedPassUseCount() const;

    private:
        /** @brief 검색 입력란과 요약 행을 그립니다. */
        void drawToolbar();
        /** @brief 필터를 통과한 타깃 목록을 그리고 선택 입력을 받습니다. */
        void drawTargetList();
        /** @brief 선택한 타깃의 미리보기와 정보를 그립니다. */
        void drawPreview();
        /** @brief 채널과 깊이 보기 행을 그립니다. */
        void drawViewControls( const RenderTargetInfo& selected );
        /** @brief 그려진 미리보기 위에서 마우스가 가리키는 픽셀 값을 읽어 툴팁으로 보여 줍니다. */
        void probeHoveredPixel( const float2& imageMin, const float2& imageSize );

    private:
        RenderTargetViewModel                   _viewModel;
        RenderTargetViewState                   _viewState; ///< 프레임마다 `gather` 가 채운다
        RenderTargetPixelProbe                  _probe;     ///< 툴팁 픽셀 읽기 버퍼(풀어낸 값 목록을 다시 쓴다)
        fixed_string<constant::buffer::kMax128> _searchFilter;
        /** @brief 미리보기 확대 배율입니다. 0 이면 창에 맞춥니다. */
        float32 _previewZoom;
    };
} // namespace sw::editor
