#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiDX11RendererBackend.h"

#include "Editor/Common/Backend/Render/ImGuiViewportSizeGuard.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include <imgui.h>

#if defined( SW_PLATFORM_WINDOWS )
    #include <imgui_impl_dx11.h>

namespace sw::editor
{
    namespace
    {
        struct ImGuiDX11RendererBackendInternal
        {
        };
    } // namespace
} // namespace sw::editor

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
