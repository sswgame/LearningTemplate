/**
 * @file RenderTargetPanel.h
 * @brief 프레임 렌더 타깃(G버퍼, 그림자 맵, 포스트 단계)을 찾아보고 화면으로 확인하는 에디터 창입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/RenderTarget/RenderTargetViewUtil.h"

#include "Engine/Graphics/Debug/RenderTargetRegistry.h"

// `vector<RenderTargetInfo>` 와 `RenderTargetReadback` 을 멤버로 가지므로 완전한 타입이 필요하다. 전방 선언으로는 안 된다.

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @class RenderTargetPanel
     * @brief 파이프라인이 만든 렌더 타깃을 나열하고 검색하며, 하나를 골라 미리 봅니다.
     * @details "지금 G버퍼에 무엇이 들어 있는지" 를 앱을 다시 띄우지 않고 확인할 수 있다. 앱 밖에서는 `-gv_screenshotAttachment` 로
     *          PPM 을 한 장 찍는다. RGBA 그대로 볼 때는 GPU 텍스처를 그대로 보여 주고, 채널 하나, 깊이(선형화), 픽셀 값은 렌더 스레드가
     *          CPU 로 읽은 스냅샷으로 본다. 스냅샷은 타깃을 고를 때, Refresh 할 때, Auto 일 때 다시 읽는다.
     * @note 목록은 `RenderTargetRegistry` 의 스냅샷이다. 렌더 스레드가 트랜지언트를 다시 만드는 동안 UI 가 이미
     *       사라진 핸들을 가지고 있지 않도록, 세대 번호가 바뀌면 목록을 다시 가져온다.
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
        float2 getInitialPanelSize() const override { return float2{ 820.0f, 520.0f }; }
        /** @brief 필요할 때 여는 도구이므로 닫힌 채로 시작합니다. */
        bool isToolPanel() const override { return true; }
        /** @brief ImGui 에 등록한 미리보기 텍스처를 해제합니다. */
        void shutdown( IRHIDevice* pRHIDevice ) override;

        // ------------------------------------------------------------------------------
        // 2) 테스트 프로브. 시나리오가 읽는 값이다
        // ------------------------------------------------------------------------------
        /** @brief 선택한 타깃의 CPU 스냅샷이 있으면 true 입니다. */
        bool hasSnapshot() const;
        /** @brief 선택한 타깃이 깊이 어태치먼트이면 true 입니다. */
        bool isSelectedDepth() const;
        /** @brief 현재 보고 있는 채널을 반환합니다. */
        RenderTargetChannel getChannel() const { return _viewSettings._channel; }
        /** @brief 깊이를 선형화해 보면 true 입니다. */
        bool isDepthLinearized() const { return _viewSettings._bLinearizeDepth != SW_FALSE; }
        /** @brief 마우스 아래 픽셀 값을 마지막으로 읽었으면 true 입니다. 마우스가 미리보기 밖으로 나가도 값은 남습니다. */
        bool hasProbedPixel() const { return _bProbedPixel != SW_FALSE; }
        /** @brief 마지막으로 읽은 픽셀 값입니다. */
        const float4& getProbedPixel() const { return _probedPixel; }

    private:
        /** @brief 레지스트리 세대가 바뀌었으면 목록을 다시 가져옵니다. */
        void refreshTargetsIfStale();
        /** @brief 검색 입력란과 요약 행을 그립니다. */
        void drawToolbar();
        /** @brief 필터를 통과한 타깃 목록을 그리고 선택 입력을 받습니다. */
        void drawTargetList();
        /** @brief 선택한 타깃의 미리보기와 정보를 그립니다. */
        void drawPreview();
        /** @brief 선택한 타깃의 ImGui 텍스처 등록을 현재 선택에 맞춥니다. */
        void syncPreviewTexture();
        /** @brief 등록한 미리보기 텍스처를 해제합니다. 여러 번 호출해도 안전합니다. */
        void releasePreviewTexture();
        /** @brief 채널과 깊이 보기 행을 그립니다. */
        void drawViewControls( const RenderTargetInfo& selected );
        /** @brief 스냅샷을 다시 읽을 때가 되었으면 요청하고, 도착한 결과를 받아 미리보기 이미지를 다시 만듭니다. */
        void syncSnapshot();
        /** @brief 스냅샷으로 미리보기 RGBA8 텍스처를 다시 만듭니다. */
        void rebuildSnapshotTexture();
        /** @brief 스냅샷 미리보기 텍스처와 그 ImGui 등록을 해제합니다. */
        void releaseSnapshotTexture( IRHIDevice* pRHIDevice );
        /** @brief 그려진 미리보기 위에서 마우스가 가리키는 픽셀 값을 읽어 툴팁으로 보여 줍니다. */
        void probeHoveredPixel( const RenderTargetInfo& selected, const float2& imageMin, const float2& imageSize );

    private:
        vector<RenderTargetInfo>              _listTarget;
        fixed_string<constant::kMaxBuffer128> _searchFilter;
        /** @brief 선택한 타깃의 이름입니다. 인덱스가 아니라 이름으로 보관하므로 목록을 다시 만들어도 선택이 유지됩니다. */
        string _selectedName;
        /** @brief 마지막으로 읽은 레지스트리 세대입니다. 값이 같으면 다시 읽지 않습니다. */
        uint64 _lastGeneration;
        /** @brief ImGui 에 등록한 미리보기 텍스처 id 입니다. 이 패널이 소유하며, 선택이 바뀌면 해제하고 다시 등록합니다. */
        void* _pPreviewTextureID;
        /** @brief 그 id 가 가리키는 RHI 텍스처 핸들입니다. 같으면 다시 등록하지 않습니다. */
        uint64 _previewTexture;
        /** @brief 미리보기 확대 배율입니다. 0 이면 창에 맞춥니다. */
        float32 _previewZoom;

        /** @brief 채널과 깊이 선형화 설정입니다. */
        RenderTargetViewSettings _viewSettings;
        /** @brief 선택한 타깃을 CPU 로 읽은 마지막 결과입니다. `_texture` 가 선택한 타깃과 다르면 쓰지 않습니다. */
        RenderTargetReadback _snapshot;
        /** @brief 스냅샷으로 만든 미리보기 RGBA8 텍스처입니다. 이 패널이 소유합니다. */
        uint64 _snapshotTexture;
        /** @brief 그 텍스처의 ImGui 등록 id 입니다. */
        void* _pSnapshotTextureID;
        /** @brief 미리보기 이미지를 만들 때 쓴 깊이 범위입니다. 회색 0 ~ 1 에 대응합니다. */
        float2 _snapshotDepthRange;
        /** @brief 마지막으로 읽은 픽셀 값입니다. */
        float4 _probedPixel;
        /** @brief 마지막으로 읽은 픽셀 좌표입니다. */
        int2 _probedTexel;
        /** @brief 처리를 기다리는 읽기 요청 번호입니다. 0 이면 없습니다. */
        uint32 _pendingReadbackSerial;
        /** @brief Auto 일 때 다음 읽기까지 남은 UI 프레임 수입니다. */
        uint32 _autoRefreshCountdown;
        /** @brief 다음 프레임에 스냅샷을 다시 읽도록 요청합니다(Refresh). */
        uint8 _bRefreshRequested;
        /** @brief 스냅샷 미리보기 이미지를 다시 만들어야 합니다. 설정이 바뀌었거나 새 스냅샷이 도착했을 때입니다. */
        uint8 _bSnapshotImageDirty;
        /** @brief 일정 간격으로 스냅샷을 다시 읽습니다. */
        uint8 _bAutoRefresh;
        /** @brief `_probedPixel` 이 유효하면 1 입니다. */
        uint8 _bProbedPixel;
    };
} // namespace sw::editor
