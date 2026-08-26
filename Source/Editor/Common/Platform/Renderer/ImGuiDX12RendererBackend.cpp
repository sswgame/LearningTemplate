#include "pch.h"

#include "Editor/Common/Platform/Renderer/ImGuiDX12RendererBackend.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include <imgui.h>

#if defined( SW_PLATFORM_WINDOWS )
	#include <imgui_impl_dx12.h>
#endif

namespace sw::editor
{
#if defined( SW_PLATFORM_WINDOWS )
	namespace
	{
		void ( *s_OrigCreateWindow )( ImGuiViewport* )			= nullptr;
		void ( *s_OrigSetWindowSize )( ImGuiViewport*, ImVec2 ) = nullptr;

		void ImGuiAllocSrv( ImGui_ImplDX12_InitInfo* pInfo, D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpu, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpu )
		{
			ImGuiDX12RendererBackend* pSelf = static_cast<ImGuiDX12RendererBackend*>( pInfo->UserData );
			if ( pSelf == nullptr || pSelf->allocSrvDescriptor( pOutCpu, pOutGpu ) == false )
			{
				pOutCpu->ptr = 0;
				pOutGpu->ptr = 0;
			}
		}

		void ImGuiFreeSrv( ImGui_ImplDX12_InitInfo* pInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu )
		{
			ImGuiDX12RendererBackend* pSelf = static_cast<ImGuiDX12RendererBackend*>( pInfo->UserData );
			if ( pSelf != nullptr )
				pSelf->freeSrvDescriptor( cpu, gpu );
		}

		/** @brief DXGI_SCALING_NONE은 HWND 클라이언트 크기와 스왑체인 크기가 일치해야 함. */
		void syncViewportSizeFromHwnd( ImGuiViewport* pViewport )
		{
			if ( pViewport == nullptr )
				return;

			HWND hwnd = static_cast<HWND>( pViewport->PlatformHandleRaw ? pViewport->PlatformHandleRaw : pViewport->PlatformHandle );
			if ( hwnd == nullptr )
				return;

			RECT rc{};
			if ( GetClientRect( hwnd, &rc ) == FALSE )
				return;

			const float32 width	 = static_cast<float32>( rc.right - rc.left );
			const float32 height = static_cast<float32>( rc.bottom - rc.top );
			if ( width >= 1.0f && height >= 1.0f )
			{
				pViewport->Size.x = width;
				pViewport->Size.y = height;
			}
		}

		void GuardedCreateWindow( ImGuiViewport* pViewport )
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

		void GuardedSetWindowSize( ImGuiViewport* pViewport, ImVec2 size )
		{
			if ( pViewport == nullptr || s_OrigSetWindowSize == nullptr )
				return;

			// imgui_impl_dx12의 ResizeBuffers(0,w,h)는 w/h==0 이면 DXGI_ERROR_INVALID_CALL → device removed.
			if ( size.x < 1.0f || size.y < 1.0f )
				return;

			s_OrigSetWindowSize( pViewport, size );
		}

		void installViewportGuards()
		{
			ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
			if ( platformIO.Renderer_CreateWindow != nullptr && platformIO.Renderer_CreateWindow != &GuardedCreateWindow )
			{
				s_OrigCreateWindow				 = platformIO.Renderer_CreateWindow;
				platformIO.Renderer_CreateWindow = &GuardedCreateWindow;
			}
			if ( platformIO.Renderer_SetWindowSize != nullptr && platformIO.Renderer_SetWindowSize != &GuardedSetWindowSize )
			{
				s_OrigSetWindowSize				  = platformIO.Renderer_SetWindowSize;
				platformIO.Renderer_SetWindowSize = &GuardedSetWindowSize;
			}
		}

		void clearViewportGuards()
		{
			s_OrigCreateWindow	= nullptr;
			s_OrigSetWindowSize = nullptr;
		}

	} // namespace
#endif

#if defined( SW_PLATFORM_WINDOWS )

	bool ImGuiDX12RendererBackend::initialize( class IRHIDevice* pRhiDevice )
	{
	#if defined( SW_PLATFORM_WINDOWS )
		SW_LOG_INFO( "ImGuiDX12RendererBackend::initialize Start" );
		_pRHIDevice = pRhiDevice;
		if ( _pRHIDevice == nullptr )
			return false;

		ID3D12Device* pDevice = static_cast<ID3D12Device*>( _pRHIDevice->getNativeDevice() );
		if ( pDevice == nullptr )
			return false;

		SW_LOG_INFO( "Creating D3D12 Descriptor Heap for ImGui" );
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type						= D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors				= _maxDescriptors;
		desc.Flags						= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if ( FAILED( pDevice->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &_d3d12SrvHeap ) ) ) )
			return false;

		_descriptorSize = pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		_nextDescriptor = 0;
		_listFreeDescriptors.clear();

		SW_LOG_INFO( "Populating ImGui_ImplDX12_InitInfo" );
		ImGui_ImplDX12_InitInfo initInfo = {};
		initInfo.Device					 = pDevice;
		initInfo.CommandQueue			 = static_cast<ID3D12CommandQueue*>( _pRHIDevice->getNativeCommandQueue() );
		initInfo.NumFramesInFlight		 = 3;
		initInfo.RTVFormat				 = DXGI_FORMAT_R8G8B8A8_UNORM;
		initInfo.SrvDescriptorHeap		 = _d3d12SrvHeap.Get();
		initInfo.UserData				 = this;
		initInfo.SrvDescriptorAllocFn	 = &ImGuiAllocSrv;
		initInfo.SrvDescriptorFreeFn	 = &ImGuiFreeSrv;

		SW_LOG_INFO( "Calling ImGui_ImplDX12_Init" );
		const bool bRet = ImGui_ImplDX12_Init( &initInfo );
		SW_LOG_INFO( "ImGui_ImplDX12_Init Returned: %#", bRet );
		if ( bRet )
			installViewportGuards();
		return bRet;
	#else
		(void)pRhiDevice;
		return true;
	#endif
	}

	void ImGuiDX12RendererBackend::shutdown()
	{
	#if defined( SW_PLATFORM_WINDOWS )
		clearViewportGuards();
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
		{
			ImGui_ImplDX12_Shutdown();
			_d3d12SrvHeap.Reset();
		}
		_nextDescriptor = 0;
		_listFreeDescriptors.clear();
		_descriptorSize = 0;
		_pRHIDevice		= nullptr;
	#endif
	}
#endif

	void ImGuiDX12RendererBackend::newFrame()
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplDX12_NewFrame();
#endif
	}

	void ImGuiDX12RendererBackend::render( class IRHIDevice* pRhiDevice )
	{
#if defined( SW_PLATFORM_WINDOWS )
		ID3D12Device* pDevice = static_cast<ID3D12Device*>( pRhiDevice->getNativeDevice() );
		if ( pDevice != nullptr )
		{
			const HRESULT removed = pDevice->GetDeviceRemovedReason();
			if ( FAILED( removed ) )
			{
				SW_LOG_ERROR( "[ImGuiDX12] Device removed before RenderDrawData (hr=%#)", static_cast<uint32>( removed ) );
				return;
			}
		}

		ID3D12GraphicsCommandList* pCmdList	 = static_cast<ID3D12GraphicsCommandList*>( pRhiDevice->getNativeContext() );
		ImDrawData*				   pDrawData = ImGui::GetDrawData();
		if ( pCmdList != nullptr && pDrawData != nullptr && _d3d12SrvHeap != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr )
		{
			ID3D12DescriptorHeap* heaps[] = { _d3d12SrvHeap.Get() };
			pCmdList->SetDescriptorHeaps( 1, heaps );
			ImGui_ImplDX12_RenderDrawData( pDrawData, pCmdList );
		}
#else
		(void)pRhiDevice;
#endif
	}

	void* ImGuiDX12RendererBackend::registerTexture( RHITextureHandle texture )
	{
#if defined( SW_PLATFORM_WINDOWS )
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
		srvDesc.Format						  = pRes->GetDesc().Format;
		srvDesc.ViewDimension				  = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping		  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip	  = 0;
		srvDesc.Texture2D.MipLevels			  = pRes->GetDesc().MipLevels;
		srvDesc.Texture2D.PlaneSlice		  = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		pDevice->CreateShaderResourceView( pRes, &srvDesc, cpuHandle );

		return reinterpret_cast<void*>( gpuHandle.ptr );
#else
		(void)texture;
		return nullptr;
#endif
	}

	void ImGuiDX12RendererBackend::unregisterTexture( void* pTextureID )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( pTextureID == nullptr || _d3d12SrvHeap == nullptr || _descriptorSize == 0 )
			return;

		const SIZE_T gpuStart = _d3d12SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr;
		const SIZE_T gpuPtr	  = reinterpret_cast<SIZE_T>( pTextureID );
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
#else
		(void)pTextureID;
#endif
	}

	bool ImGuiDX12RendererBackend::allocSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpu, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpu )
	{
		if ( _d3d12SrvHeap == nullptr || pOutCpu == nullptr || pOutGpu == nullptr )
			return false;

		uint32 index{ 0 };
		if ( _listFreeDescriptors.empty() == false )
		{
			index = _listFreeDescriptors.back();
			_listFreeDescriptors.pop_back();
		}
		else
		{
			if ( _nextDescriptor >= _maxDescriptors )
			{
				SW_LOG_ERROR( "[ImGuiDX12] SRV descriptor heap exhausted (%#)", _maxDescriptors );
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
			_listFreeDescriptors.push_back( index );
	}
} // namespace sw::editor
