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

    bool ImGuiDX11RendererBackend::initialize( class IRHIDevice* pRHIDevice )
    {
        _pRHIDevice.set( pRHIDevice );
        if ( _pRHIDevice.get() == nullptr )
            return false;

        // 핸들은 백엔드마다 뜻이 다른 `void*` 다 — D3D11 디바이스인지와 값이 있는지를 `queryNativeHandles` 한 곳에서 확인한다.
        RHINativeHandles handles{};
        if ( _pRHIDevice->queryNativeHandles( RHIBackend::DirectX11, handles ) == false )
        {
            SW_LOG_ERROR( "The ImGui DX11 renderer needs a D3D11 device" );
            return false;
        }
        ID3D11Device* pDevice = static_cast<ID3D11Device*>( handles._pDevice );

        // ImGui 는 받은 즉시 컨텍스트를 들고 있다. 그것을 쓰는 호출(newFrame · processTextureUpdates · render · 보조 뷰포트)은
        // 렌더 스레드에서만 한다(`requiresRenderThreadContext`). 즉시 컨텍스트의 파이프라인 상태는 프레임 스트림과 하나라 다른 스레드가 쓰면 섞인다.
        bool       bInitialized{ false };
        const bool bContextVisited = _pRHIDevice->withNativeContext( RHIBackend::DirectX11, SW_DELEGATE_LAMBDA( RHINativeContextVisitor, [pDevice, &bInitialized]( void* pContext )
        {
            if ( pContext != nullptr )
                bInitialized = ImGui_ImplDX11_Init( pDevice, static_cast<ID3D11DeviceContext*>( pContext ) );
        } ) );
        if ( bContextVisited == false || bInitialized == false )
        {
            SW_LOG_ERROR( "The ImGui DX11 renderer needs a D3D11 immediate context" );
            return false;
        }
        _viewportSizeGuard.install();
        return true;
    }

    void ImGuiDX11RendererBackend::shutdown()
    {
        // `install()` 을 부른 쪽이 `clear()` 도 부른다. 가드 헤더가 "백엔드를 종료할 때 부르십시오" 라고 적어 둔 짝이다(DX12 도 같다).
        // 이 백엔드는 미룰 네이티브 자원이 없지만, 에디터가 같은 큐에 맡긴 해제(게임 뷰 렌더 타깃)가 이 모듈의 코드를 가리킨다.
        flushDrawReleases( _pRHIDevice.get() );
        _viewportSizeGuard.clear();
        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
            ImGui_ImplDX11_Shutdown();
        _listRegisteredSRV.clear();
        _pRHIDevice.reset();
    }

    void ImGuiDX11RendererBackend::newFrame()
    {
        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
            ImGui_ImplDX11_NewFrame();
    }

    void ImGuiDX11RendererBackend::processTextureUpdates()
    {
        if ( _pRHIDevice.get() == nullptr )
            return;
        // UpdateSubresource 묶음이 다른 스레드의 전송 · 폴링과 섞이지 않게 즉시 컨텍스트 잠금 안에서 한다.
        // initialize 가 백엔드를 확인했고, 다르면 withNativeContext 가 로그를 남긴다.
        (void)_pRHIDevice->withNativeContext( RHIBackend::DirectX11, SW_DELEGATE_LAMBDA( RHINativeContextVisitor, []( void* /*pContext*/ )
        { updatePendingTextures( &ImGui_ImplDX11_UpdateTexture ); } ) );
    }

    void ImGuiDX11RendererBackend::render( class IRHIDevice* pRHIDevice, ImDrawData* pDrawData )
    {
        (void)pRHIDevice;
        if ( pDrawData == nullptr || _pRHIDevice.get() == nullptr )
            return;
        // ImGui 가 상태를 백업 · 설정 · 복원하는 묶음을 즉시 컨텍스트 잠금 안에서 한다(컨텍스트는 Init 에서 받은 것을 쓴다).
        // initialize 가 백엔드를 확인했고, 다르면 withNativeContext 가 로그를 남긴다.
        (void)_pRHIDevice->withNativeContext( RHIBackend::DirectX11, SW_DELEGATE_LAMBDA( RHINativeContextVisitor, [pDrawData]( void* /*pContext*/ )
        { ImGui_ImplDX11_RenderDrawData( pDrawData ); } ) );
    }

    void ImGuiDX11RendererBackend::renderPlatformWindows()
    {
        if ( _pRHIDevice.get() == nullptr )
            return;
        // initialize 가 백엔드를 확인했고, 다르면 withNativeContext 가 로그를 남긴다.
        (void)_pRHIDevice->withNativeContext( RHIBackend::DirectX11, SW_DELEGATE_LAMBDA( RHINativeContextVisitor, []( void* pContext )
        {
            ImGui::RenderPlatformWindowsDefault();
            // 보조 뷰포트의 백버퍼를 컨텍스트에 걸어 두지 않는다. UI 스레드의 창 크기 변경(ResizeBuffers)은 컨텍스트에 걸린 백버퍼가 있으면
            // 실패하는데, 렌더 스레드는 다음 렌더 패스까지 렌더 타깃을 다시 걸지 않는다.
            if ( pContext != nullptr )
                static_cast<ID3D11DeviceContext*>( pContext )->OMSetRenderTargets( 0, nullptr, nullptr );
        } ) );
    }

    void* ImGuiDX11RendererBackend::registerTexture( RHITextureHandle texture )
    {
        if ( texture == 0 || _pRHIDevice.get() == nullptr )
            return nullptr;

        ID3D11Texture2D* pTex = static_cast<ID3D11Texture2D*>( _pRHIDevice->tryGetNativeTexturePointer( RHIBackend::DirectX11, texture ) );
        if ( pTex == nullptr )
            return nullptr;

        RHINativeHandles handles{};
        if ( _pRHIDevice->queryNativeHandles( RHIBackend::DirectX11, handles ) == false )
            return nullptr;
        ID3D11Device* pDevice = static_cast<ID3D11Device*>( handles._pDevice );

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

        ID3D11ShaderResourceView* pSRVPtr = srv.Get();
        _listRegisteredSRV.push_back( std::move( srv ) );
        return pSRVPtr;
    }

    void ImGuiDX11RendererBackend::unregisterTexture( void* pTextureID )
    {
        if ( pTextureID == nullptr )
            return;

        // 곧바로 놓아도 된다. D3D11 런타임은 컨텍스트에 걸려 있거나 GPU 가 아직 쓰는 뷰를 그 작업이 끝날 때까지 살려 둔다 — 여기서 놓는 것은
        // 이 백엔드의 참조 하나다. 그래서 Vulkan · DX12 와 달리 해제 큐에 맡기지 않는다.
        ID3D11ShaderResourceView* pSRV = static_cast<ID3D11ShaderResourceView*>( pTextureID );
        for ( auto it = _listRegisteredSRV.begin(); it != _listRegisteredSRV.end(); ++it )
        {
            if ( it->Get() == pSRV )
            {
                _listRegisteredSRV.erase( it );
                return;
            }
        }
    }
} // namespace sw::editor
#else
namespace sw::editor
{
    bool ImGuiDX11RendererBackend::initialize( class IRHIDevice* /*pRHIDevice*/ )
    {
        return false;
    }
    void ImGuiDX11RendererBackend::shutdown()
    {
    }
    void ImGuiDX11RendererBackend::newFrame()
    {
    }
    void ImGuiDX11RendererBackend::processTextureUpdates()
    {
    }
    void ImGuiDX11RendererBackend::render( class IRHIDevice* /*pRHIDevice*/, ImDrawData* /*pDrawData*/ )
    {
    }
    void ImGuiDX11RendererBackend::renderPlatformWindows()
    {
    }
    void* ImGuiDX11RendererBackend::registerTexture( RHITextureHandle /*texture*/ )
    {
        return nullptr;
    }
    void ImGuiDX11RendererBackend::unregisterTexture( void* /*pTextureID*/ )
    {
    }
} // namespace sw::editor
#endif
