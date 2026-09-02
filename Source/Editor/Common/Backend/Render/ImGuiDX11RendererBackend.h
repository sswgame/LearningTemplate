/**
 * @file ImGuiDX11RendererBackend.h
 * @brief ImGui Direct3D 11 렌더러 백엔드
 */
#pragma once
#include "Core/Container/vector.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"

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
        bool initialize( IRHIDevice* pRhiDevice ) override;
        /** @brief D3D11 ImGui 렌더러를 종료합니다. */
        void shutdown() override;

        // ------------------------------------------------------------------------------
        // 2) IImGuiRendererBackend — 프레임/텍스처
        // ------------------------------------------------------------------------------
        /** @brief ImGui D3D11 프레임을 시작합니다. */
        void newFrame() override;
        /** @brief 대기 중인 폰트 아틀라스/텍스처 갱신을 UI 스레드에서 처리합니다. */
        void processTextureUpdates() override;
        /** @brief ImGui draw data를 D3D11로 그립니다. */
        void render( IRHIDevice* pRhiDevice, ImDrawData* pDrawData ) override;

        /** @brief RHI 텍스처를 ImGui용 SRV로 등록합니다. */
        void* registerTexture( RHITextureHandle texture ) override;
        /** @brief 등록된 ImGui SRV를 해제합니다. */
        void unregisterTexture( void* pTextureID ) override;

    private:
        IRHIDevice* _pRHIDevice{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
        vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> _listRegisteredSrv;
#endif
    };
} // namespace sw::editor
