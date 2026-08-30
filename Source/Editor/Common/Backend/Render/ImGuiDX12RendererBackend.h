/**
 * @file ImGuiDX12RendererBackend.h
 * @brief ImGui DirectX12 렌더러 백엔드
 */
#pragma once
#include "Core/Container/vector.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"

struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct D3D12_GPU_DESCRIPTOR_HANDLE;

namespace sw
{
	class IRHIDevice;
} // namespace sw

namespace sw::editor
{
	/** @brief ImGui DirectX12 렌더러 (SRV 힙 풀 포함) */
	class ImGuiDX12RendererBackend : public IImGuiRendererBackend
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 생명주기 — 생성은 기본값, GPU 리소스는 initialize / shutdown
		// ------------------------------------------------------------------------------
		/** @brief 멤버만 기본값으로 둡니다. 실제 생성은 initialize()에서 합니다. */
		ImGuiDX12RendererBackend() = default;
		/** @brief 리소스는 shutdown()에서 해제합니다. */
		virtual ~ImGuiDX12RendererBackend() override = default;

		/** @brief D3D12 ImGui 렌더러와 SRV 힙을 초기화합니다. */
		bool initialize( IRHIDevice* pRhiDevice ) override;
		/** @brief D3D12 ImGui 렌더러를 종료합니다. */
		void shutdown() override;

		// ------------------------------------------------------------------------------
		// 2) IImGuiRendererBackend — 프레임/텍스처
		// ------------------------------------------------------------------------------
		/** @brief ImGui D3D12 프레임을 시작합니다. */
		void newFrame() override;
		/** @brief 대기 중인 폰트 아틀라스/텍스처 갱신을 UI 스레드에서 처리합니다. */
		void processTextureUpdates() override;
		/** @brief ImGui draw data를 D3D12로 그립니다. */
		void render( IRHIDevice* pRhiDevice, ImDrawData* pDrawData ) override;
		/** @brief RHI 텍스처를 ImGui용 SRV로 등록합니다. */
		void* registerTexture( RHITextureHandle texture ) override;
		/** @brief 등록된 ImGui SRV 디스크립터를 풀에 반환합니다. */
		void unregisterTexture( void* pTextureID ) override;

#if defined( SW_PLATFORM_WINDOWS )
		// ------------------------------------------------------------------------------
		// 3) SRV 힙 풀
		// ------------------------------------------------------------------------------
		/** @brief SRV 힙에서 CPU/GPU 디스크립터를 할당합니다. */
		bool allocSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpu, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpu );
		/** @brief 할당했던 SRV 디스크립터를 풀에 반환합니다. */
		void freeSrvDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu );
#endif

	private:
		IRHIDevice* _pRHIDevice{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _d3d12SrvHeap;
		vector<uint32>								 _listFreeDescriptor;
		UINT										 _descriptorSize{ 0 };
		uint32										 _maxDescriptors = 128;
		uint32										 _nextDescriptor{ 0 };
#endif
	};
} // namespace sw::editor
