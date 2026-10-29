#include "pch.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiDX11RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiDX12RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiOpenGLRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiVulkanRendererBackend.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ImGuiRendererBackendInternal
        {
            /**
             * @brief `ImTextureStatus_WantDestroy` 텍스처를 파괴하기 전에 기다릴 프레임 수입니다.
             * @details UI 스레드가 낸 draw 스냅샷(최대 `kMaxFrameCountInFlight` 장)을 렌더 스레드가 그리고, 그 GPU 작업이 또 `kMaxFrameCountInFlight`
             *          프레임까지 겹친다. 그 사이의 스냅샷이 옛 아틀라스를 아직 그릴 수 있으므로, ImGui 가 세는 미사용 프레임(`UnusedFrames`)이
             *          그 둘을 넘은 뒤에 파괴한다.
             */
            static constexpr int32 kTextureDestroyDelayFrames = static_cast<int32>( constant::kMaxFrameCountInFlight * 2u + 1u );

            /**
             * @brief 이 텍스처의 갱신(생성 · 갱신 · 파괴)을 지금 백엔드에 넘길지 판정합니다(네 렌더러 백엔드 공통 — 넘기는 곳이 `updatePendingTextures` 하나다).
             * @details 파괴는 `kTextureDestroyDelayFrames` 만큼 미룬다 — 바로 넘기면 백엔드가 이미지 · 디스크립터를 즉시 놓아, 그것을 그리는
             *          스냅샷이 아직 도는 렌더 스레드 · GPU 가 해제된 자원을 읽는다. ImGui 는 그동안 `WantDestroy` 로 두고 `UnusedFrames` 를 센다.
             */
            static bool isTextureUpdateDue( const ImTextureData& texture )
            {
                if ( texture.Status == ImTextureStatus_OK || texture.Status == ImTextureStatus_Destroyed )
                    return false;
                if ( texture.Status == ImTextureStatus_WantDestroy )
                    return texture.UnusedFrames >= kTextureDestroyDelayFrames;
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void IImGuiRendererBackend::updatePendingTextures( void ( *pUpdateTexture )( ImTextureData* ) )
    {
        if ( pUpdateTexture == nullptr || ImGui::GetIO().BackendRendererUserData == nullptr )
            return;

        for ( ImTextureData* pTexture : ImGui::GetPlatformIO().Textures )
        {
            if ( pTexture != nullptr && ImGuiRendererBackendInternal::isTextureUpdateDue( *pTexture ) )
                pUpdateTexture( pTexture );
        }
    }

    void IImGuiRendererBackend::renderPlatformWindows()
    {
        ImGui::RenderPlatformWindowsDefault();
    }

    void IImGuiRendererBackend::flushDrawReleases( IRHIDevice* pRHIDevice )
    {
        if ( pRHIDevice != nullptr )
            pRHIDevice->waitIdle( RHIWaitIdleReason::Tool );
        _drawReleaseQueue.flushAll();
    }

    unique_ptr<IImGuiRendererBackend> IImGuiRendererBackend::createRendererBackend( RHIBackend backend )
    {
        switch ( backend )
        {
            case RHIBackend::DirectX11:
                return make_unique<ImGuiDX11RendererBackend>();
            case RHIBackend::DirectX12:
                return make_unique<ImGuiDX12RendererBackend>();
            case RHIBackend::OpenGL:
                return make_unique<ImGuiOpenGLRendererBackend>();
            case RHIBackend::Vulkan:
                return make_unique<ImGuiVulkanRendererBackend>();
        }
        // 이 백엔드용 ImGui 렌더러가 없다(범위 밖 값). 다른 백엔드 것으로 대신 만들지 않는다 — 엉뚱한 API 로 그리다 조용히 무너진다.
        // 새 백엔드를 붙이면 -Wswitch 가 위 switch 를 짚는다. "에디터가 이 백엔드를 지원하는가" 의 원본이 이 답이다.
        return nullptr;
    }
} // namespace sw::editor
