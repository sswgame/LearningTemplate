#include "pch.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Backend/ImGuiTextureUpdate.h"
#include "Editor/Common/Backend/Render/ImGuiDX11RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiDX12RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiOpenGLRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiVulkanRendererBackend.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include <imgui.h>

namespace sw::editor
{
    void IImGuiRendererBackend::updatePendingTextures( void ( *pUpdateTexture )( ImTextureData* ) )
    {
        if ( pUpdateTexture == nullptr || ImGui::GetIO().BackendRendererUserData == nullptr )
            return;

        for ( ImTextureData* pTexture : ImGui::GetPlatformIO().Textures )
        {
            if ( pTexture != nullptr && isTextureUpdateDue( *pTexture ) )
                pUpdateTexture( pTexture );
        }
    }

    void IImGuiRendererBackend::flushDrawReleases( IRHIDevice* pRhiDevice )
    {
        if ( pRhiDevice != nullptr )
            pRhiDevice->waitIdle();
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
        // 새 백엔드를 붙이면 -Wswitch 가 위 switch 를 짚는다. "에디터가 이 백엔드를 지원하는가" 의 정본이 이 답이다.
        return nullptr;
    }
} // namespace sw::editor
