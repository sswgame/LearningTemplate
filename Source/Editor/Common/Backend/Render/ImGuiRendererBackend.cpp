#include "pch.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiDX11RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiDX12RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiOpenGLRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiVulkanRendererBackend.h"

#include <imgui.h>

namespace sw::editor
{
    void IImGuiRendererBackend::updatePendingTextures( void ( *pUpdateTexture )( ImTextureData* ) )
    {
        if ( pUpdateTexture == nullptr || ImGui::GetIO().BackendRendererUserData == nullptr )
            return;

        for ( ImTextureData* pTexture : ImGui::GetPlatformIO().Textures )
        {
            if ( pTexture != nullptr && pTexture->Status != ImTextureStatus_OK )
                pUpdateTexture( pTexture );
        }
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
            default:
                // 이 백엔드용 ImGui 렌더러가 없다 — **DX11 것으로 대신 만들지 않는다.** 예전에는 그랬고,
                // 새 백엔드가 붙으면 엉뚱한 API 로 그리다 조용히 무너진다. "에디터가 이 백엔드를 감당하는가" 의
                // 정본이 바로 이 답이다.
                return nullptr;
        }
    }
} // namespace sw::editor
