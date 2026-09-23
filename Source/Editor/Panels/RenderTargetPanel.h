/**
 * @file RenderTargetPanel.h
 * @brief 프레임 렌더 타깃(G버퍼 · 그림자 맵 · 포스트 단계)을 찾아보고 화면으로 확인하는 에디터 창입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/IEditorPanel.h"

#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"

// `vector<RenderTargetInfo>` 를 멤버로 들고 있으므로 **완전한 타입**이 필요하다(전방 선언으로는 안 된다).

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @class RenderTargetPanel
     * @brief 파이프라인이 만든 렌더 타깃을 나열 · 검색하고, 하나를 골라 미리 봅니다.
     * @details 이것이 없을 때 "지금 G버퍼에 뭐가 들어 있나" 를 보려면 `-gv_screenshotAttachment` 로 **프로세스를 다시
     *          띄워** PPM 을 한 장 찍는 수밖에 없었습니다. 디퍼드가 아무것도 그리지 않던 버그 셋을 그 방식으로 쫓았고,
     *          한 장 볼 때마다 앱을 새로 켜야 했습니다.
     * @note 목록은 `RenderTargetRegistry` 의 **스냅샷**입니다. 렌더 스레드가 트랜지언트를 다시 만드는 동안 UI 가 이미
     *       사라진 핸들을 들고 있지 않도록, 세대 번호가 바뀌면 다시 가져옵니다.
     */
    class RenderTargetPanel : public IEditorPanel
    {
    public:
        /** @brief 렌더 타깃 창을 만듭니다(도구 창이라 닫힌 채 시작합니다). */
        RenderTargetPanel();
        /** @brief 소멸자입니다. ImGui 에 등록한 미리보기 텍스처는 shutdown() 이 놓습니다. */
        virtual ~RenderTargetPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Render Targets"; }
        /** @brief 목록·검색·미리보기를 그립니다. */
        void drawContent() override;
        /** @brief 기본 창 크기를 반환합니다. */
        float2 getInitialPanelSize() const override { return float2{ 820.0f, 520.0f }; }
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }
        /** @brief ImGui 에 등록한 미리보기 텍스처를 해제합니다. */
        void shutdown( IRHIDevice* pRhiDevice ) override;

    private:
        /** @brief 레지스트리 세대가 바뀌었으면 목록을 다시 가져옵니다. */
        void refreshTargetsIfStale();
        /** @brief 검색 필드와 요약 줄을 그립니다. */
        void drawToolbar();
        /** @brief 필터를 통과한 타깃 목록을 그리고 선택을 받습니다. */
        void drawTargetList();
        /** @brief 고른 타깃의 미리보기와 정보를 그립니다. */
        void drawPreview();
        /** @brief 고른 타깃의 ImGui 텍스처 등록을 현재 선택에 맞춥니다. */
        void syncPreviewTexture();
        /** @brief 등록한 미리보기 텍스처를 놓습니다. 여러 번 불러도 안전합니다. */
        void releasePreviewTexture();

    private:
        vector<RenderTargetInfo>              _listTarget;
        fixed_string<constant::kMaxBuffer128> _searchFilter;
        /** @brief 고른 타깃의 이름입니다. 인덱스가 아니라 이름으로 들고 있어서, 목록이 다시 만들어져도 선택이 유지됩니다. */
        string _selectedName;
        /** @brief 마지막으로 읽은 레지스트리 세대입니다. 값이 같으면 다시 읽지 않습니다. */
        uint64 _lastGeneration;
        /** @brief ImGui 에 등록한 미리보기 텍스처 id 입니다. 이 패널이 소유하며, 선택이 바뀌면 놓고 다시 등록합니다. */
        void* _pPreviewTextureId;
        /** @brief 그 id 가 가리키는 RHI 텍스처 핸들입니다. 같으면 다시 등록하지 않습니다. */
        uint64 _previewTexture;
        /** @brief 미리보기 확대 배율입니다. 0 이면 창에 맞춥니다. */
        float32 _previewZoom;
    };
} // namespace sw::editor
