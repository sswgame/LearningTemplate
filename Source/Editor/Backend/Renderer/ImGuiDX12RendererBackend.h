#pragma once
/**
 * @file ImGuiDX12RendererBackend.h
 * @brief ImGui DirectX12 렌더러 백엔드
 */

#include "Editor/Backend/IImGuiRendererBackend.h"

struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct D3D12_GPU_DESCRIPTOR_HANDLE;

namespace sw
{
	class IRHIDevice;

	/** @brief ImGui DirectX12 렌더러 (SRV 힙 풀 포함) */
	class ImGuiDX12RendererBackend : public IImGuiRendererBackend
	{
	public:
		ImGuiDX12RendererBackend()			 = default;
		~ImGuiDX12RendererBackend() override = default;

		/** @brief D3D12 ImGui 렌더러와 SRV 힙을 초기화합니다. */
		bool initialize( IRHIDevice* rhiDevice ) override;
		/** @brief D3D12 ImGui 렌더러를 종료합니다. */
		void shutdown() override;
		/** @brief ImGui D3D12 프레임을 시작합니다. */
		void newFrame() override;
		/** @brief ImGui draw data를 D3D12로 그립니다. */
		void render( IRHIDevice* rhiDevice ) override;
		/** @brief RHI 텍스처를 ImGui용 SRV로 등록합니다. */
		void* registerTexture( RHITextureHandle texture ) override;
		/** @brief 등록된 ImGui SRV 디스크립터를 풀에 반환합니다. */
		void unregisterTexture( void* textureID ) override;

#if defined( SW_PLATFORM_WINDOWS )
		/** @brief SRV 힙에서 CPU/GPU 디스크립터를 할당합니다. */
		bool allocSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu );
		/** @brief 할당했던 SRV 디스크립터를 풀에 반환합니다. */
		void freeSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu );
#endif

	private:
#if defined( SW_PLATFORM_WINDOWS )
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _d3d12SrvHeap;
		UINT										 _descriptorSize = 0;
		uint32										 _maxDescriptors = 128;
		uint32										 _nextDescriptor = 0;
		std::vector<uint32>							 _freeDescriptors;
#endif
	};
} // namespace sw
