/**
 * @file UiPreviewPanel.h
 * @brief 런타임 UI 문서(`*.ui.xml`) 미리보기 창입니다 — 해상도 견본 · UI/글자 배율 · 안전 영역 · 테마 · 레이아웃 사각형 · 위젯 트리 선택.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Gui/IEditorPanel.h"
#include "Editor/Panels/UiPreviewLogic.h"

#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Screen/UiScreen.h"

namespace sw
{
    class IRHIDevice;
    class UiSystem;
} // namespace sw

namespace sw::editor
{
    /**
     * @class UiPreviewPanel
     * @brief UI 문서를 게임 UI 와 따로 **오프스크린 화면**(`UiSystem::openOffscreenScreen` — 렌더 텍스처 대상 캔버스)으로 지어 ImGui 이미지로 봅니다
     *        (유니티 UI Builder · UMG 디자이너의 미리보기 자리).
     * @details 해상도 견본은 게임과 같은 배율 규칙(`UiSystem::getScaleSettings`)으로 뷰포트를 만들고, UI 배율 · 글자 배율 · 안전 영역 · 테마는 이 미리보기에만 겁니다.
     *          문서를 고치면 핫 리로드가 미리보기도 다시 짓습니다. 레이아웃 사각형 · 고른 위젯은 ImGui 그리기 목록으로 그림 위에 얹습니다(캔버스를 바꾸지 않는다).
     *          판단(뷰포트 · 맞춤 · 점 옮기기 · 트리 줄)은 `UiPreviewLogic`(ImGui 없음 — EditorTest).
     */
    class UiPreviewPanel : public IEditorPanel
    {
    public:
        UiPreviewPanel();
        ~UiPreviewPanel() override = default;

        const utf8* getPanelTitle() const override { return "UI Preview"; }
        void        drawContent() override;
        float2      getInitialPanelSize() const override { return float2{ 1100.0f, 680.0f }; }
        bool        isToolPanel() const override { return true; }
        /** @brief 미리보기 화면을 닫고 ImGui 텍스처를 놓습니다. */
        void shutdown( IRHIDevice* pRhiDevice ) override;

    private:
        /** @brief 문서 · 뷰포트가 바뀌었으면 오프스크린 화면을 다시 열고(크기가 바뀌면 렌더 텍스처 경로도), 이번 프레임의 보기를 겁니다. */
        void syncPreview( UiSystem& ui );
        /** @brief 렌더 텍스처를 ImGui 텍스처로 등록합니다(핸들이 바뀌면 다시). */
        void syncTexture();
        void releaseTexture();
        void closePreview();
        void drawToolbar( UiSystem& ui );
        void drawWidgetList( const UiScreen& screen );
        void drawImage( const UiScreen& screen );

    private:
        vector<UiPreviewWidgetRow> _listRow;
        string                     _documentPath;
        string                     _openedDocument; ///< 지금 미리보기 화면의 문서
        string                     _targetPath;     ///< 지금 렌더 텍스처 경로
        string                     _error;          ///< 마지막으로 열지 못한 까닭
        hashed_string              _theme;          ///< 빈 이름 = 게임의 지금 테마
        UiViewport                 _viewport;
        void*                      _pTextureID; ///< ImGui 텍스처 id(이 패널 소유)
        uint64                     _texture;    ///< 그 id 의 RHI 텍스처
        UiScreenHandle             _screen;
        WidgetID                   _selected;
        uint32                     _resolutionIndex;
        float32                    _uiScale;
        float32                    _textScale;
        float32                    _safeZone;
        bool                       _bShowLayout;
    };
} // namespace sw::editor
