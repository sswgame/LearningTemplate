#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiDX12RendererBackend.h"

#include "Editor/Common/Backend/Render/ImGuiViewportSizeGuard.h"

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
            static void ImGuiAllocSrv( ImGui_ImplDX12_InitInfo* pInfo, D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpu, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpu )
            {
                ImGuiDX12RendererBackend* pSelf = static_cast<ImGuiDX12RendererBackend*>( pInfo->UserData );
                if ( pSelf == nullptr || pSelf->allocateSrvDescriptor( pOutCpu, pOutGpu ) == false )
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
        const bool bInitialized = ImGui_ImplDX12_Init( &initInfo );
        SW_LOG_TRACE( "ImGui_ImplDX12_Init Returned: %#", bInitialized );
        if ( bInitialized )
            ImGuiViewportSizeGuard::install();
        return bInitialized;
    }

    void ImGuiDX12RendererBackend::shutdown()
    {
        // 렌더 스레드 · GPU 를 기다려 미뤄 둔 디스크립터 반환을 모두 끝낸 뒤에 힙을 놓는다(GPU 가 아직 읽는 힙을 놓지 않는다).
        flushDrawReleases( _pRHIDevice );
        ImGuiViewportSizeGuard::clear();
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
                // 디바이스 제거는 복구되지 않아 이후 모든 프레임이 여기로 들어온다. 그래서 한 번만 남긴다.
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

        ID3D12Resource* pResource = static_cast<ID3D12Resource*>( _pRHIDevice->getNativeTexturePointer( texture ) );
        if ( pResource == nullptr )
            return nullptr;

        ID3D12Device* pDevice = static_cast<ID3D12Device*>( _pRHIDevice->getNativeDevice() );
        if ( pDevice == nullptr )
            return nullptr;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        if ( allocateSrvDescriptor( &cpuHandle, &gpuHandle ) == false )
            return nullptr;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                        = pResource->GetDesc().Format;
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip     = 0;
        srvDesc.Texture2D.MipLevels           = pResource->GetDesc().MipLevels;
        srvDesc.Texture2D.PlaneSlice          = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        pDevice->CreateShaderResourceView( pResource, &srvDesc, cpuHandle );

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

        // 반환한 칸은 다음 registerTexture 가 곧바로 덮어쓴다. 이미 낸 draw 스냅샷이 이 칸을 아직 그릴 수 있으므로, 그것을 그린 마지막 프레임의
        // GPU 완료 뒤에 반환한다.
        getDrawReleaseQueue().enqueue( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [this, cpuHandle, gpuHandle]()
        { freeSrvDescriptor( cpuHandle, gpuHandle ); } ) );
    }

    bool ImGuiDX12RendererBackend::allocateSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpu, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpu )
    {
        if ( _d3d12SrvHeap == nullptr || pOutCpu == nullptr || pOutGpu == nullptr )
            return false;

        std::scoped_lock<mutex> lock{ _descriptorMutex };
        uint32                  index{ 0 };
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
        if ( index >= _maxDescriptors )
            return;
        std::scoped_lock<mutex> lock{ _descriptorMutex };
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
