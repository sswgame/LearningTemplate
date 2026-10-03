#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiDX11RendererBackend.h"

#include "Editor/Common/Backend/Render/ImGuiViewportSizeGuard.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include <imgui.h>

#if defined( SW_PLATFORM_WINDOWS )
    #include <imgui_impl_dx11.h>

namespace sw::editor
{
    SW_LOG_CALLER( "ImGuiDX11" );

    bool ImGuiDX11RendererBackend::initialize( class IRHIDevice* pRhiDevice )
    {
        _pRHIDevice = pRhiDevice;
        if ( _pRHIDevice == nullptr )
            return false;

        ID3D11Device*        pDevice  = static_cast<ID3D11Device*>( _pRHIDevice->getNativeDevice() );
        ID3D11DeviceContext* pContext = static_cast<ID3D11DeviceContext*>( _pRHIDevice->getNativeContext() );
        if ( pDevice != nullptr && pContext != nullptr )
        {
            const bool bOk = ImGui_ImplDX11_Init( pDevice, pContext );
            if ( bOk )
                ImGuiViewportSizeGuard::install();
            return bOk;
        }
        return true;
    }

    void ImGuiDX11RendererBackend::shutdown()
    {
        // `install()` 을 부른 쪽이 `clear()` 도 부른다. 가드 헤더가 "백엔드를 종료할 때 부르십시오" 라고 적어 둔 짝이다(DX12 도 같다).
        // 이 백엔드는 미룰 네이티브 자원이 없지만, 에디터가 같은 큐에 맡긴 해제(게임 뷰 렌더 타깃)가 이 모듈의 코드를 가리킨다.
        flushDrawReleases( _pRHIDevice );
        ImGuiViewportSizeGuard::clear();
        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
            ImGui_ImplDX11_Shutdown();
        _listRegisteredSrv.clear();
        _pRHIDevice = nullptr;
    }

    void ImGuiDX11RendererBackend::newFrame()
    {
        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
            ImGui_ImplDX11_NewFrame();
    }

    void ImGuiDX11RendererBackend::processTextureUpdates()
    {
        updatePendingTextures( &ImGui_ImplDX11_UpdateTexture );
    }

    void ImGuiDX11RendererBackend::render( class IRHIDevice* pRhiDevice, ImDrawData* pDrawData )
    {
        (void)pRhiDevice;
        if ( pDrawData != nullptr && _pRHIDevice != nullptr )
            ImGui_ImplDX11_RenderDrawData( pDrawData );
    }

    void* ImGuiDX11RendererBackend::registerTexture( RHITextureHandle texture )
    {
        if ( texture == 0 || _pRHIDevice == nullptr )
            return nullptr;

        ID3D11Texture2D* pTex = static_cast<ID3D11Texture2D*>( _pRHIDevice->getNativeTexturePointer( texture ) );
        if ( pTex == nullptr )
            return nullptr;

        ID3D11Device* pDevice = static_cast<ID3D11Device*>( _pRHIDevice->getNativeDevice() );
        if ( pDevice == nullptr )
            return nullptr;

        D3D11_TEXTURE2D_DESC texDesc{};
        pTex->GetDesc( &texDesc );

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                    = texDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = texDesc.MipLevels;

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        const HRESULT                                    hr = pDevice->CreateShaderResourceView( pTex, &srvDesc, srv.GetAddressOf() );
        if ( FAILED( hr ) || srv == nullptr )
        {
            SW_LOG_ERROR( "Failed to create SRV for registered texture. HRESULT: %#", hr );
            return nullptr;
        }

        ID3D11ShaderResourceView* pSrvPtr = srv.Get();
        _listRegisteredSrv.push_back( std::move( srv ) );
        return pSrvPtr;
    }

    void ImGuiDX11RendererBackend::unregisterTexture( void* pTextureID )
    {
        if ( pTextureID == nullptr )
            return;

        // 곧바로 놓아도 된다. D3D11 런타임은 컨텍스트에 걸려 있거나 GPU 가 아직 쓰는 뷰를 그 작업이 끝날 때까지 살려 둔다 — 여기서 놓는 것은
        // 이 백엔드의 참조 하나다. 그래서 Vulkan · DX12 와 달리 해제 큐에 맡기지 않는다.
        ID3D11ShaderResourceView* pSrv = static_cast<ID3D11ShaderResourceView*>( pTextureID );
        for ( auto it = _listRegisteredSrv.begin(); it != _listRegisteredSrv.end(); ++it )
        {
            if ( it->Get() == pSrv )
            {
                _listRegisteredSrv.erase( it );
                return;
            }
        }
    }
} // namespace sw::editor
#else
namespace sw::editor
{
    bool  ImGuiDX11RendererBackend::initialize( class IRHIDevice* /*pRhiDevice*/ ) { return false; }
    void  ImGuiDX11RendererBackend::shutdown() {}
    void  ImGuiDX11RendererBackend::newFrame() {}
    void  ImGuiDX11RendererBackend::processTextureUpdates() {}
    void  ImGuiDX11RendererBackend::render( class IRHIDevice* /*pRhiDevice*/, ImDrawData* /*pDrawData*/ ) {}
    void* ImGuiDX11RendererBackend::registerTexture( RHITextureHandle /*texture*/ ) { return nullptr; }
    void  ImGuiDX11RendererBackend::unregisterTexture( void* /*pTextureID*/ ) {}
} // namespace sw::editor
#endif
