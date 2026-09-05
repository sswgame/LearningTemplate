#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiDX12RendererBackend.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include <imgui.h>

#if defined( SW_PLATFORM_WINDOWS )
    #include <imgui_impl_dx12.h>

namespace sw::editor
{
    namespace
    {
        struct ImGuiDX12RendererBackendInternal
        {
            inline static void ( *s_OrigCreateWindow )( ImGuiViewport* )          = nullptr;
            inline static void ( *s_OrigSetWindowSize )( ImGuiViewport*, ImVec2 ) = nullptr;

            static void ImGuiAllocSrv( ImGui_ImplDX12_InitInfo* pInfo, D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpu, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpu )
            {
                ImGuiDX12RendererBackend* pSelf = static_cast<ImGuiDX12RendererBackend*>( pInfo->UserData );
                if ( pSelf == nullptr || pSelf->allocSrvDescriptor( pOutCpu, pOutGpu ) == false )
                {
                    pOutCpu->ptr = 0;
                    pOutGpu->ptr = 0;
                }
            }

            static void ImGuiFreeSrv( ImGui_ImplDX12_InitInfo* pInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu )
            {
                ImGuiDX12RendererBackend* pSelf = static_cast<ImGuiDX12RendererBackend*>( pInfo->UserData );
                if ( pSelf != nullptr )
                    pSelf->freeSrvDescriptor( cpu, gpu );
            }

            /** @brief DXGI_SCALING_NONE은 HWND 클라이언트 크기와 스왑체인 크기가 일치해야 함. */
            static void syncViewportSizeFromHwnd( ImGuiViewport* pViewport )
            {
                if ( pViewport == nullptr )
                    return;

                HWND hwnd = static_cast<HWND>( pViewport->PlatformHandleRaw ? pViewport->PlatformHandleRaw : pViewport->PlatformHandle );
                if ( hwnd == nullptr )
                    return;

                RECT rc{};
                if ( GetClientRect( hwnd, &rc ) == FALSE )
                    return;

                const float32 width  = static_cast<float32>( rc.right - rc.left );
                const float32 height = static_cast<float32>( rc.bottom - rc.top );
                if ( width >= 1.0f && height >= 1.0f )
                {
                    pViewport->Size.x = width;
                    pViewport->Size.y = height;
                }
            }

            static void GuardedCreateWindow( ImGuiViewport* pViewport )
            {
                if ( pViewport == nullptr || s_OrigCreateWindow == nullptr )
                    return;

                syncViewportSizeFromHwnd( pViewport );
                if ( pViewport->Size.x < 1.0f )
                    pViewport->Size.x = 1.0f;
                if ( pViewport->Size.y < 1.0f )
                    pViewport->Size.y = 1.0f;

                s_OrigCreateWindow( pViewport );
            }

            static void GuardedSetWindowSize( ImGuiViewport* pViewport, ImVec2 size )
            {
                if ( pViewport == nullptr || s_OrigSetWindowSize == nullptr )
                    return;

                // imgui_impl_dx12의 ResizeBuffers(0,w,h)는 w/h==0 이면 DXGI_ERROR_INVALID_CALL → device removed.
                if ( size.x < 1.0f || size.y < 1.0f )
                    return;

                s_OrigSetWindowSize( pViewport, size );
            }

            static void installViewportGuards()
            {
                ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
                if ( platformIO.Renderer_CreateWindow != nullptr && platformIO.Renderer_CreateWindow != &GuardedCreateWindow )
                {
                    s_OrigCreateWindow               = platformIO.Renderer_CreateWindow;
                    platformIO.Renderer_CreateWindow = &GuardedCreateWindow;
                }
                if ( platformIO.Renderer_SetWindowSize != nullptr && platformIO.Renderer_SetWindowSize != &GuardedSetWindowSize )
                {
                    s_OrigSetWindowSize               = platformIO.Renderer_SetWindowSize;
                    platformIO.Renderer_SetWindowSize = &GuardedSetWindowSize;
                }
            }

            static void clearViewportGuards()
            {
                s_OrigCreateWindow  = nullptr;
                s_OrigSetWindowSize = nullptr;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "ImGuiDX12" );

    bool ImGuiDX12RendererBackend::initialize( class IRHIDevice* pRhiDevice )
    {
        SW_LOG_TRACE( "Initialize start." );
        _pRHIDevice = pRhiDevice;
        if ( _pRHIDevice == nullptr )
            return false;

        ID3D12Device* pDevice = static_cast<ID3D12Device*>( _pRHIDevice->getNativeDevice() );
        if ( pDevice == nullptr )
            return false;

        SW_LOG_TRACE( "Creating D3D12 Descriptor Heap for ImGui" );
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors             = _maxDescriptors;
        desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if ( FAILED( pDevice->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &_d3d12SrvHeap ) ) ) )
            return false;

        _descriptorSize = pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        _nextDescriptor = 0;
        _listFreeDescriptor.clear();

        SW_LOG_TRACE( "Populating ImGui_ImplDX12_InitInfo" );
        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device                  = pDevice;
        initInfo.CommandQueue            = static_cast<ID3D12CommandQueue*>( _pRHIDevice->getNativeCommandQueue() );
        initInfo.NumFramesInFlight       = 3;
        initInfo.RTVFormat               = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.SrvDescriptorHeap       = _d3d12SrvHeap.Get();
        initInfo.UserData                = this;
        initInfo.SrvDescriptorAllocFn    = &ImGuiDX12RendererBackendInternal::ImGuiAllocSrv;
        initInfo.SrvDescriptorFreeFn     = &ImGuiDX12RendererBackendInternal::ImGuiFreeSrv;

        SW_LOG_TRACE( "Calling ImGui_ImplDX12_Init" );
        const bool bRet = ImGui_ImplDX12_Init( &initInfo );
        SW_LOG_TRACE( "ImGui_ImplDX12_Init Returned: %#", bRet );
        if ( bRet )
            ImGuiDX12RendererBackendInternal::installViewportGuards();
        return bRet;
    }

    void ImGuiDX12RendererBackend::shutdown()
    {
        ImGuiDX12RendererBackendInternal::clearViewportGuards();
        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
        {
            ImGui_ImplDX12_Shutdown();
            _d3d12SrvHeap.Reset();
        }
        _nextDescriptor = 0;
        _listFreeDescriptor.clear();
        _descriptorSize = 0;
        _pRHIDevice     = nullptr;
    }

    void ImGuiDX12RendererBackend::newFrame()
    {
        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
            ImGui_ImplDX12_NewFrame();
    }

    void ImGuiDX12RendererBackend::processTextureUpdates()
    {
        updatePendingTextures( &ImGui_ImplDX12_UpdateTexture );
    }

    void ImGuiDX12RendererBackend::render( class IRHIDevice* pRhiDevice, ImDrawData* pDrawData )
    {
        ID3D12Device* pDevice = static_cast<ID3D12Device*>( pRhiDevice->getNativeDevice() );
        if ( pDevice != nullptr )
        {
            const HRESULT removed = pDevice->GetDeviceRemovedReason();
            if ( FAILED( removed ) )
            {
                // 디바이스 제거는 복구되지 않아 이후 모든 프레임이 여기로 들어온다 — 1회만 남긴다.
                if ( _bDeviceRemovedLogged == false )
                {
                    _bDeviceRemovedLogged = true;
                    SW_LOG_ERROR( "Device removed before RenderDrawData (hr=%#)", static_cast<uint32>( removed ) );
                }
                return;
            }
        }

        ID3D12GraphicsCommandList* pCmdList = static_cast<ID3D12GraphicsCommandList*>( pRhiDevice->getNativeContext() );
        if ( pCmdList != nullptr && pDrawData != nullptr && _d3d12SrvHeap != nullptr )
        {
            ID3D12DescriptorHeap* heaps[] = { _d3d12SrvHeap.Get() };
            pCmdList->SetDescriptorHeaps( 1, heaps );
            ImGui_ImplDX12_RenderDrawData( pDrawData, pCmdList );
        }
    }

    void* ImGuiDX12RendererBackend::registerTexture( RHITextureHandle texture )
    {
        if ( texture == 0 || _d3d12SrvHeap == nullptr || _pRHIDevice == nullptr )
            return nullptr;

        ID3D12Resource* pRes = static_cast<ID3D12Resource*>( _pRHIDevice->getNativeTexturePointer( texture ) );
        if ( pRes == nullptr )
            return nullptr;

        ID3D12Device* pDevice = static_cast<ID3D12Device*>( _pRHIDevice->getNativeDevice() );
        if ( pDevice == nullptr )
            return nullptr;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        if ( allocSrvDescriptor( &cpuHandle, &gpuHandle ) == false )
            return nullptr;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                        = pRes->GetDesc().Format;
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip     = 0;
        srvDesc.Texture2D.MipLevels           = pRes->GetDesc().MipLevels;
        srvDesc.Texture2D.PlaneSlice          = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        pDevice->CreateShaderResourceView( pRes, &srvDesc, cpuHandle );

        return reinterpret_cast<void*>( gpuHandle.ptr );
    }

    void ImGuiDX12RendererBackend::unregisterTexture( void* pTextureID )
    {
        if ( pTextureID == nullptr || _d3d12SrvHeap == nullptr || _descriptorSize == 0 )
            return;

        const SIZE_T gpuStart = _d3d12SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr;
        const SIZE_T gpuPtr   = reinterpret_cast<SIZE_T>( pTextureID );
        if ( gpuPtr < gpuStart )
            return;

        const uint32 index = static_cast<uint32>( ( gpuPtr - gpuStart ) / _descriptorSize );
        if ( index >= _maxDescriptors )
            return;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        cpuHandle.ptr = _d3d12SrvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>( index ) * _descriptorSize;
        gpuHandle.ptr = gpuPtr;
        freeSrvDescriptor( cpuHandle, gpuHandle );
    }

    bool ImGuiDX12RendererBackend::allocSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpu, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpu )
    {
        if ( _d3d12SrvHeap == nullptr || pOutCpu == nullptr || pOutGpu == nullptr )
            return false;

        uint32 index{ 0 };
        if ( _listFreeDescriptor.empty() == false )
        {
            index = _listFreeDescriptor.back();
            _listFreeDescriptor.pop_back();
        }
        else
        {
            if ( _nextDescriptor >= _maxDescriptors )
            {
                SW_LOG_ERROR( "SRV descriptor heap exhausted (%#)", _maxDescriptors );
                return false;
            }
            index = _nextDescriptor++;
        }

        pOutCpu->ptr = _d3d12SrvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>( index ) * _descriptorSize;
        pOutGpu->ptr = _d3d12SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>( index ) * _descriptorSize;
        return true;
    }

    void ImGuiDX12RendererBackend::freeSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE /*gpu*/ )
    {
        if ( _d3d12SrvHeap == nullptr || _descriptorSize == 0 )
            return;

        const SIZE_T start = _d3d12SrvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
        if ( cpu.ptr < start )
            return;

        const uint32 index = static_cast<uint32>( ( cpu.ptr - start ) / _descriptorSize );
        if ( index < _maxDescriptors )
            _listFreeDescriptor.push_back( index );
    }
} // namespace sw::editor
#else
namespace sw::editor
{
    bool  ImGuiDX12RendererBackend::initialize( class IRHIDevice* /*pRhiDevice*/ ) { return false; }
    void  ImGuiDX12RendererBackend::shutdown() {}
    void  ImGuiDX12RendererBackend::newFrame() {}
    void  ImGuiDX12RendererBackend::processTextureUpdates() {}
    void  ImGuiDX12RendererBackend::render( class IRHIDevice* /*pRhiDevice*/, ImDrawData* /*pDrawData*/ ) {}
    void* ImGuiDX12RendererBackend::registerTexture( RHITextureHandle /*texture*/ ) { return nullptr; }
    void  ImGuiDX12RendererBackend::unregisterTexture( void* /*pTextureID*/ ) {}
} // namespace sw::editor
#endif
