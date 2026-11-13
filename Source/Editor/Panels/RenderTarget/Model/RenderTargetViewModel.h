/**
 * @file RenderTargetViewModel.h
 * @brief 렌더 타깃 창의 ViewModel — 레지스트리 목록 · 고른 타깃 · CPU 스냅샷 · 미리보기 텍스처 등록을 맡고(`gather`), 고르기 · 다시 읽기 · 보기 설정 · 픽셀 읽기 명령을 받습니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 색 그대로(RGBA)는 GPU 텍스처를 그대로 ImGui 이미지로 등록하고, 채널 하나 · 깊이(선형화) · 픽셀 값은 렌더 스레드가 CPU 로 읽은 스냅샷으로 만든다.
 *          스냅샷은 타깃을 고를 때, 다시 읽기를 요청할 때, 자동일 때 간격마다 다시 읽는다. 렌더 타깃 보기는 문서 데이터가 아니라 되돌리기에 남지 않는다.
 * @note 목록은 `RenderTargetRegistry` 의 스냅샷이다. 렌더 스레드가 트랜지언트를 다시 만드는 동안 이미 사라진 핸들을 그리지 않도록, 세대 번호가 바뀌면 목록을 다시 가져오고 등록을 놓는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Panels/RenderTarget/Model/RenderTargetViewSettings.h"

#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Renderer/Pipeline/GBufferLayout.h"

namespace sw::editor
{
    /** @brief 렌더 타깃 창이 한 프레임 그릴 값입니다. 텍스처 id 는 ImGui 이미지 id(불투명 값)이고 모델 포인터는 두지 않는다(MVVM 계약). */
    struct RenderTargetViewState
    {
        vector<RenderTargetInfo> _listTarget{};         ///< 레지스트리 목록 사본(목록이 바뀐 프레임에만 다시 복사한다)
        uint64                   _listSerial{ 0 };      ///< 사본이 속한 목록 번호(`RenderTargetViewModel` 이 다시 가져올 때마다 늘린다)
        string                   _selectedName{};       ///< 고른 타깃 이름(없으면 빈 글)
        RenderTargetViewSettings _viewSettings{};       ///< 채널과 깊이 보기 설정
        void*                    _pLiveTextureID{};     ///< 고른 타깃의 GPU 텍스처를 등록한 ImGui id(깊이 · 등록 실패는 nullptr)
        void*                    _pSnapshotTextureID{}; ///< CPU 스냅샷으로 만든 미리보기 이미지의 ImGui id(아직 없으면 nullptr)
        float2                   _snapshotDepthRange{ 0.0f, 1.0f };
        float4                   _probedPixel{};            ///< 마지막으로 읽은 픽셀 값
        int2                     _probedTexel{};            ///< 마지막으로 읽은 픽셀 좌표
        uint8                    _bHasSnapshot{ SW_FALSE }; ///< 고른 타깃의 CPU 스냅샷이 있다
        uint8                    _bAutoRefresh{ SW_FALSE }; ///< 일정 간격으로 스냅샷을 다시 읽는다
        uint8                    _bProbedPixel{ SW_FALSE }; ///< `_probedPixel` 이 유효하다
    };

    /** @brief 미리보기 위 한 픽셀을 읽은 결과입니다(값만). */
    struct RenderTargetPixelProbe
    {
        int2                        _texel{};
        float4                      _value{};
        float32                     _linearDepth{ 0.0f }; ///< 깊이 타깃일 때 보기 설정의 면으로 선형화한 거리
        uint32                      _channelCount{ 0 };   ///< 색 타깃의 채널 수
        const utf8*                 _arrMeaning[4]{};     ///< G버퍼 첨부 채널의 뜻(명세의 정적 글 — 모르면 nullptr)
        vector<GBufferDecodedValue> _listDecodedValue{};  ///< G버퍼 첨부면 명세로 푼 값
        uint8                       _bDepth{ SW_FALSE };
        uint8                       _bGBuffer{ SW_FALSE }; ///< 고른 타깃이 G버퍼 첨부다(`GBufferLayout::findAttachment`)
    };

    /** @brief 렌더 타깃 창의 ViewModel 입니다. */
    class RenderTargetViewModel
    {
    public:
        RenderTargetViewModel();

        /** @brief 목록을 맞추고 선택 · 텍스처 등록 · 스냅샷 읽기를 진행한 뒤 창이 그릴 값을 @p outState 에 채웁니다(프레임마다 한 번). */
        void gather( RenderTargetViewState& outState );
        /** @brief ImGui 에 등록한 텍스처와 스냅샷 텍스처를 놓습니다(패널 shutdown). */
        void shutdown();

        /** @brief 타깃 @p name 을 고릅니다. */
        void selectTarget( const string& name );
        /** @brief 다음 프레임에 스냅샷을 다시 읽습니다. */
        void requestRefresh();
        /** @brief 일정 간격으로 스냅샷을 다시 읽을지 정합니다. */
        void setAutoRefresh( bool bAutoRefresh );
        /** @brief 보여 줄 채널을 정합니다(스냅샷 이미지를 다시 만든다). */
        void setChannel( RenderTargetChannel channel );
        /** @brief 깊이를 거리로 되돌려 볼지 정합니다. */
        void setLinearizeDepth( bool bLinearize );
        /** @brief 깊이 회색 범위를 화면 값에 맞출지 정합니다. */
        void setAutoDepthRange( bool bAutoRange );
        /** @brief 깊이 선형화에 쓰는 가까운 · 먼 면을 정합니다. */
        void setDepthPlanes( float32 nearPlane, float32 farPlane );
        /**
         * @brief 고른 타깃의 스냅샷에서 미리보기 좌표 @p localPos (그린 크기 @p drawSize 안)의 픽셀을 읽어 @p outProbe 에 채웁니다. 마지막 읽은 값으로도 남긴다.
         * @return 스냅샷이 없거나 좌표가 밖이면 false 입니다.
         */
        [[nodiscard]] bool probePixel( const float2& localPos, const float2& drawSize, RenderTargetPixelProbe& outProbe );

    private:
        /** @brief 레지스트리 세대가 바뀌었으면 목록을 다시 가져옵니다. */
        void refreshTargetsIfStale();
        /** @brief 사라진 선택을 놓고, 선택이 없으면 화면에 나가는 것(없으면 첫 색 타깃)을 고릅니다. */
        void commitSelection();
        /** @brief 고른 타깃의 스냅샷이 있으면 true 입니다. */
        bool hasSnapshot() const;
        /** @brief 고른 타깃의 ImGui 텍스처 등록을 현재 선택에 맞춥니다. */
        void syncPreviewTexture();
        /** @brief 등록한 미리보기 텍스처를 해제합니다. 여러 번 불러도 안전합니다. */
        void releasePreviewTexture();
        /** @brief 스냅샷을 다시 읽을 때가 되었으면 요청하고, 도착한 결과를 받아 미리보기 이미지를 다시 만듭니다. */
        void syncSnapshot();
        /** @brief 스냅샷으로 미리보기 RGBA8 텍스처를 다시 만듭니다. */
        void rebuildSnapshotTexture();
        /** @brief 엔진이 스냅샷 텍스처를 올렸으면 ImGui 에 등록합니다. */
        void applySnapshotTextureIfReady();
        /** @brief 스냅샷 미리보기 텍스처와 그 ImGui 등록을 해제합니다. */
        void releaseSnapshotTexture();

    private:
        vector<RenderTargetInfo> _listTarget;
        uint64                   _listSerial;
        /** @brief 고른 타깃의 이름입니다. 인덱스가 아니라 이름으로 보관하므로 목록을 다시 만들어도 선택이 유지됩니다. */
        string _selectedName;
        /** @brief 마지막으로 읽은 레지스트리 세대입니다. 값이 같으면 다시 읽지 않습니다. */
        uint64 _lastGeneration;
        /** @brief ImGui 에 등록한 미리보기 텍스처 id 입니다. 선택이 바뀌면 해제하고 다시 등록합니다. */
        void* _pPreviewTextureID;
        /** @brief 그 id 가 가리키는 RHI 텍스처 핸들입니다. 같으면 다시 등록하지 않습니다. */
        uint64 _previewTexture;

        RenderTargetViewSettings _viewSettings;
        /** @brief 고른 타깃을 CPU 로 읽은 마지막 결과입니다. `_texture` 가 고른 타깃과 다르면 쓰지 않습니다. */
        RenderTargetReadback _snapshot;
        /**
         * @brief 스냅샷으로 만든 미리보기 RGBA8 이미지의 엔진 CPU 텍스처(`cpu/editor/…`) 경로와 로드 핸들입니다. GPU 자원은 엔진이 렌더 쪽에서 올린다 —
         *        에디터는 RHI 를 부르지 않는다(유니티 `Texture2D.SetPixels` · `Apply` 와 같은 자리).
         */
        string        _snapshotPath;
        TextureHandle _snapshotHandle;
        uint32        _snapshotSerial;
        void*         _pSnapshotTextureID;
        /** @brief 미리보기 이미지를 만들 때 쓴 깊이 범위입니다. 회색 0 ~ 1 에 대응합니다. */
        float2 _snapshotDepthRange;
        float4 _probedPixel;
        int2   _probedTexel;
        /** @brief 처리를 기다리는 읽기 요청 번호입니다. 0 이면 없습니다. */
        uint32 _pendingReadbackSerial;
        /** @brief 자동일 때 다음 읽기까지 남은 UI 프레임 수입니다. */
        uint32 _autoRefreshCountdown;
        uint8  _bRefreshRequested;
        /** @brief 스냅샷 미리보기 이미지를 다시 만들어야 합니다. 설정이 바뀌었거나 새 스냅샷이 도착했을 때입니다. */
        uint8 _bSnapshotImageDirty;
        uint8 _bAutoRefresh;
        uint8 _bProbedPixel;
    };
} // namespace sw::editor
