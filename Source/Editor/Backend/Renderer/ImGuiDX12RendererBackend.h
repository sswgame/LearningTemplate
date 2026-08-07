#pragma once
/**
 * @file ImGuiDX12RendererBackend.h
 * @brief ImGui DirectX12 렌더러 백엔드
 */

#include "Editor/Backend/IImGuiRendererBackend.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include <wrl/client.h>
	#include <d3d12.h>
	#include <vector>
#endif

namespace sw
{
	class IRHIDevice;

	class ImGuiDX12RendererBackend : public IImGuiRendererBackend
	{
	public:
		ImGuiDX12RendererBackend()			 = default;
		~ImGuiDX12RendererBackend() override = default;

		bool  initialize( IRHIDevice* rhiDevice ) override;
		void  shutdown() override;
		void  newFrame() override;
		void  render( IRHIDevice* rhiDevice ) override;
		void* registerTexture( RHITextureHandle texture ) override;

#if defined( SW_PLATFORM_WINDOWS )
		bool allocSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu );
		void freeSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu );
#endif

	private:
#if defined( SW_PLATFORM_WINDOWS )
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _d3d12SrvHeap;
		UINT										 _descriptorSize   = 0;
		uint32										 _maxDescriptors   = 64;
		uint32										 _nextDescriptor   = 0;
		std::vector<uint32>							 _freeDescriptors;
#endif
	};
}
