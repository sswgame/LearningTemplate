/**
 * @file ImGuiDX11RendererBackend.h
 * @brief ImGui Direct3D 11 렌더러 백엔드
 */
#pragma once
#include "Core/Concurrency/OnceInit.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiViewportSizeGuard.h"

#include "Engine/Common/EnginePlatformHeaders.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /** @brief ImGui Direct3D 11 렌더러 (SRV 목록) */
    class ImGuiDX11RendererBackend : public IImGuiRendererBackend
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 생명주기 — 생성은 기본값, GPU 리소스는 initialize / shutdown
        // ------------------------------------------------------------------------------
        /** @brief 멤버만 기본값으로 둡니다. 실제 생성은 initialize()에서 합니다. */
        ImGuiDX11RendererBackend() = default;
        /** @brief 리소스는 shutdown()에서 해제합니다. */
        virtual ~ImGuiDX11RendererBackend() override = default;

        /** @brief D3D11 ImGui 렌더러를 초기화합니다. */
        [[nodiscard]] bool initialize( IRHIDevice* pRHIDevice ) override;
        /** @brief D3D11 ImGui 렌더러를 종료합니다. */
        void shutdown() override;

        // ------------------------------------------------------------------------------
        // 2) IImGuiRendererBackend — 프레임/텍스처
        // ------------------------------------------------------------------------------
        /** @brief ImGui D3D11 프레임을 시작합니다. */
        void newFrame() override;
        /** @brief 대기 중인 폰트 아틀라스/텍스처 갱신을 렌더 스레드에서 즉시 컨텍스트 잠금 안에서 처리합니다. */
        void processTextureUpdates() override;
        /** @brief ImGui draw data를 D3D11로 그립니다. 즉시 컨텍스트 잠금 안에서 그립니다. */
        void render( IRHIDevice* pRHIDevice, ImDrawData* pDrawData ) override;
        /**
         * @brief 텍스처 갱신 · 보조 뷰포트 렌더를 렌더 스레드에서 하므로 true 입니다.
         * @details ImGui 는 Init 에서 받은 즉시 컨텍스트로 파이프라인 상태(렌더 타깃 · 셰이더 · 뷰포트)를 바꾼다. 즉시 컨텍스트의 상태는 프레임 스트림과
         *          하나라, UI 스레드에서 그리면 렌더 스레드가 기록하는 프레임 중간에 렌더 타깃이 바뀐다. 호출 하나씩의 런타임 보호로는 막지 못한다.
         */
        bool requiresRenderThreadContext() const override { return true; }
        /** @brief 보조 뷰포트를 즉시 컨텍스트 잠금 안에서 그리고, 그 백버퍼를 컨텍스트에서 뗍니다(UI 스레드의 `ResizeBuffers`). */
        void renderPlatformWindows() override;

        /** @brief RHI 텍스처를 ImGui용 SRV로 등록합니다. */
        void* registerTexture( RHITextureHandle texture ) override;
        /** @brief 등록된 ImGui SRV를 해제합니다. */
        void unregisterTexture( void* pTextureID ) override;

    private:
#if defined( SW_PLATFORM_WINDOWS )
        OnceInit<IRHIDevice*>                                    _pRHIDevice;
        vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> _listRegisteredSRV;
        ImGuiViewportSizeGuard                                   _viewportSizeGuard; ///< DXGI 창 크기 보정 — 초기화 성공 뒤 걸고 종료에서 푼다
#endif
    };
} // namespace sw::editor
