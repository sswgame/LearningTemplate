/**
 * @file ImGuiDX12RendererBackend.cpp
 * @brief ImGui DirectX12 렌더러 백엔드 구현
 */
#include "ImGuiDX12RendererBackend.h"
#include <imgui.h>

#if defined( SW_PLATFORM_WINDOWS )
	#include <imgui_impl_dx12.h>
#endif

#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
#if defined( SW_PLATFORM_WINDOWS )
	namespace
	{
		void ( *s_OrigCreateWindow )( ImGuiViewport* )			= nullptr;
		void ( *s_OrigSetWindowSize )( ImGuiViewport*, ImVec2 ) = nullptr;

		void ImGuiAllocSrv( ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu )
		{
			auto* self = static_cast<ImGuiDX12RendererBackend*>( info->UserData );
			if ( self == nullptr || self->allocSrvDescriptor( outCpu, outGpu ) == false )
			{
				outCpu->ptr = 0;
				outGpu->ptr = 0;
			}
		}

		void ImGuiFreeSrv( ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu )
		{
			auto* self = static_cast<ImGuiDX12RendererBackend*>( info->UserData );
			if ( self != nullptr )
				self->freeSrvDescriptor( cpu, gpu );
		}

		/** @brief DXGI_SCALING_NONE은 HWND 클라이언트 크기와 스왑체인 크기가 일치해야 함. */
		void syncViewportSizeFromHwnd( ImGuiViewport* viewport )
		{
			if ( viewport == nullptr )
				return;

			const HWND hwnd = static_cast<HWND>( viewport->PlatformHandleRaw ? viewport->PlatformHandleRaw : viewport->PlatformHandle );
			if ( hwnd == nullptr )
				return;

			RECT rc{};
			if ( GetClientRect( hwnd, &rc ) == FALSE )
				return;

			const float width  = static_cast<float>( rc.right - rc.left );
			const float height = static_cast<float>( rc.bottom - rc.top );
			if ( width >= 1.0f && height >= 1.0f )
			{
				viewport->Size.x = width;
				viewport->Size.y = height;
			}
		}

		void GuardedCreateWindow( ImGuiViewport* viewport )
		{
			if ( viewport == nullptr || s_OrigCreateWindow == nullptr )
				return;

			syncViewportSizeFromHwnd( viewport );
			if ( viewport->Size.x < 1.0f )
				viewport->Size.x = 1.0f;
			if ( viewport->Size.y < 1.0f )
				viewport->Size.y = 1.0f;

			s_OrigCreateWindow( viewport );
		}

		void GuardedSetWindowSize( ImGuiViewport* viewport, ImVec2 size )
		{
			if ( viewport == nullptr || s_OrigSetWindowSize == nullptr )
				return;

			// imgui_impl_dx12의 ResizeBuffers(0,w,h)는 w/h==0 이면 DXGI_ERROR_INVALID_CALL → device removed.
			if ( size.x < 1.0f || size.y < 1.0f )
				return;

			s_OrigSetWindowSize( viewport, size );
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
	}
#endif

	bool ImGuiDX12RendererBackend::allocSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( _d3d12SrvHeap == nullptr || outCpu == nullptr || outGpu == nullptr )
			return false;

		uint32 index = 0;
		if ( _freeDescriptors.empty() == false )
		{
			index = _freeDescriptors.back();
			_freeDescriptors.pop_back();
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

		outCpu->ptr = _d3d12SrvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>( index ) * _descriptorSize;
		outGpu->ptr = _d3d12SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>( index ) * _descriptorSize;
		return true;
#else
		(void)outCpu;
		(void)outGpu;
		return false;
#endif
	}

	void ImGuiDX12RendererBackend::freeSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE /*gpu*/ )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( _d3d12SrvHeap == nullptr || _descriptorSize == 0 )
			return;

		const SIZE_T start = _d3d12SrvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
		if ( cpu.ptr < start )
			return;

		const uint32 index = static_cast<uint32>( ( cpu.ptr - start ) / _descriptorSize );
		if ( index < _maxDescriptors )
			_freeDescriptors.push_back( index );
#else
		(void)cpu;
#endif
	}

	bool ImGuiDX12RendererBackend::initialize( class IRHIDevice* rhiDevice )
	{
#if defined( SW_PLATFORM_WINDOWS )
		SW_LOG_INFO( "ImGuiDX12RendererBackend::initialize Start" );
		ID3D12Device* device = static_cast<ID3D12Device*>( rhiDevice->getNativeDevice() );
		if ( device == nullptr )
			return false;

		SW_LOG_INFO( "Creating D3D12 Descriptor Heap for ImGui" );
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type						= D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors				= _maxDescriptors;
		desc.Flags						= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if ( FAILED( device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &_d3d12SrvHeap ) ) ) )
			return false;

		_descriptorSize	  = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		_nextDescriptor	  = 0;
		_freeDescriptors.clear();

		SW_LOG_INFO( "Populating ImGui_ImplDX12_InitInfo" );
		ImGui_ImplDX12_InitInfo initInfo = {};
		initInfo.Device					 = device;
		initInfo.CommandQueue			 = static_cast<ID3D12CommandQueue*>( rhiDevice->getNativeCommandQueue() );
		initInfo.NumFramesInFlight		 = 2;
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
		(void)rhiDevice;
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
		_freeDescriptors.clear();
		_descriptorSize = 0;
#endif
	}

	void ImGuiDX12RendererBackend::newFrame()
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplDX12_NewFrame();
#endif
	}

	void ImGuiDX12RendererBackend::render( class IRHIDevice* rhiDevice )
	{
#if defined( SW_PLATFORM_WINDOWS )
		ID3D12Device* device = static_cast<ID3D12Device*>( rhiDevice->getNativeDevice() );
		if ( device != nullptr )
		{
			const HRESULT removed = device->GetDeviceRemovedReason();
			if ( FAILED( removed ) )
			{
				SW_LOG_ERROR( "[ImGuiDX12] Device removed before RenderDrawData (hr=%#)", static_cast<uint32>( removed ) );
				return;
			}
		}

		ID3D12GraphicsCommandList* cmdList = static_cast<ID3D12GraphicsCommandList*>( rhiDevice->getNativeContext() );
		if ( cmdList != nullptr && _d3d12SrvHeap != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr )
		{
			ID3D12DescriptorHeap* heaps[] = { _d3d12SrvHeap.Get() };
			cmdList->SetDescriptorHeaps( 1, heaps );
			ImGui_ImplDX12_RenderDrawData( ImGui::GetDrawData(), cmdList );
		}
#else
		(void)rhiDevice;
#endif
	}

	void* ImGuiDX12RendererBackend::registerTexture( RHITextureHandle texture )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( texture == 0 || _d3d12SrvHeap == nullptr )
			return nullptr;

		auto*		  res	 = reinterpret_cast<ID3D12Resource*>( texture );
		ID3D12Device* device = nullptr;
		res->GetDevice( IID_PPV_ARGS( &device ) );
		if ( device == nullptr )
			return nullptr;

		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
		if ( allocSrvDescriptor( &cpuHandle, &gpuHandle ) == false )
		{
			device->Release();
			return nullptr;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format						  = res->GetDesc().Format;
		srvDesc.ViewDimension				  = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping		  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip	  = 0;
		srvDesc.Texture2D.MipLevels			  = res->GetDesc().MipLevels;
		srvDesc.Texture2D.PlaneSlice		  = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView( res, &srvDesc, cpuHandle );
		device->Release();

		return reinterpret_cast<void*>( gpuHandle.ptr );
#else
		(void)texture;
		return nullptr;
#endif
	}
}
