/**
 * @file RHICapabilities.h
 * @brief RHI 백엔드 능력과 OS/빌드 가용성 조회
 */
#pragma once
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) RHICapabilities — bindless / 컴퓨트 / 에디터 / 인디렉트
	// ------------------------------------------------------------------------------
	/** @note 비트필드 대신 uint8 — DLL/컴파일러 간 패킹·true 대입 이슈를 피합니다. 0/1만 사용. */
	struct SW_API RHICapabilities
	{
		uint8 _bBindless{ 0 };			   ///< 디스크립터 인덱스 테이블 (드로우 시 바인드로 에뮬 가능)
		uint8 _bNativeBindless{ 0 };	   ///< 하드웨어 디스크립터 인덱싱 / bindless 샘플링
		uint8 _bCompute{ 1 };			   ///< 컴퓨트 셰이더
		uint8 _bOffscreenRT{ 0 };		   ///< createTexture2D + 오프스크린 경로
		uint8 _bImGuiHooks{ 0 };		   ///< ImGui 렌더러 훅 (멀티 뷰포트 등)
		uint8 _bEditorSupported{ 0 };	   ///< 이 백엔드에서 EditorModule 실행 가능
		uint8 _bComputeRootConstants{ 0 }; ///< 컴퓨트 루트/푸시 상수 (DX12 네이티브, DX11/GL CB/UBO 심)
		uint8 _bIndirectDraw{ 0 };		   ///< drawIndirect / dispatchIndirect
		uint8 _bIndexedDraw{ 0 };		   ///< setIndexBuffer + drawIndexedIndirect
		uint8 _bGpuCulling{ 0 };				   ///< 컴퓨트 컬 + 인디렉트 인자 경로
		uint8 _bMultiDrawIndirect{ 0 };			   ///< 멀티 드로우 / count 버퍼 (DX12/VK/GL; DX11은 루프)
		uint8 _bParallelCommandRecording{ 0 };	   ///< 멀티스레드 커맨드 리스트 병렬 기록 및 제출 지원 (DX12/VK)

		/** @brief 기본값 (컴퓨트만 켠 보수적 기본). */
		RHICapabilities() noexcept = default;
	};

	// ------------------------------------------------------------------------------
	// 2) RHIAvailability — OS/빌드에서 생성 가능 여부 + 백엔드별 능력표
	// ------------------------------------------------------------------------------
	struct RHIAvailability
	{
		/** @brief 이 OS/빌드에서 해당 백엔드를 만들 수 있으면 true. */
		static bool isAvailable( RHIBackend backend ) noexcept
		{
			switch ( backend )
			{
				case RHIBackend::DirectX11:
				case RHIBackend::DirectX12:
#if defined( SW_PLATFORM_WINDOWS )
					return true;
#else
					return false;
#endif
				case RHIBackend::Vulkan:
				case RHIBackend::OpenGL:
					return true;
			}
			return false;
		}

		/** @brief 백엔드의 정적 capability 표를 반환합니다. */
		static RHICapabilities query( RHIBackend backend ) noexcept
		{
			RHICapabilities caps{};
			switch ( backend )
			{
				case RHIBackend::DirectX12:
					caps._bBindless					= 1;
					caps._bNativeBindless			= 1; // 후보. 런타임은 Device::getCapabilities()
					caps._bCompute					= 1;
					caps._bOffscreenRT				= 1;
					caps._bImGuiHooks				= 1;
					caps._bEditorSupported			= 1;
					caps._bComputeRootConstants		= 1;
					caps._bIndirectDraw				= 1;
					caps._bIndexedDraw				= 1;
					caps._bGpuCulling				= 1;
					caps._bMultiDrawIndirect		= 1;
					caps._bParallelCommandRecording = 1;
					break;
				case RHIBackend::DirectX11:
					caps._bBindless					= 1;
					caps._bNativeBindless			= 0;
					caps._bCompute					= 1;
					caps._bOffscreenRT				= 1;
					caps._bImGuiHooks				= 1;
					caps._bEditorSupported			= 1;
					caps._bComputeRootConstants		= 1;
					caps._bIndirectDraw				= 1;
					caps._bIndexedDraw				= 1;
					caps._bGpuCulling				= 1;
					caps._bMultiDrawIndirect		= 1;
					caps._bParallelCommandRecording = 0;
					break;
				case RHIBackend::OpenGL:
					caps._bBindless					= 1;
					caps._bNativeBindless			= 0;
					caps._bCompute					= 1;
					caps._bOffscreenRT				= 1;
					caps._bImGuiHooks				= 1;
					caps._bEditorSupported			= 1;
					caps._bComputeRootConstants		= 1;
					caps._bIndirectDraw				= 1;
					caps._bIndexedDraw				= 1;
					caps._bGpuCulling				= 1;
					caps._bMultiDrawIndirect		= 1;
					caps._bParallelCommandRecording = 0;
					break;
				case RHIBackend::Vulkan:
					caps._bBindless					= 1;
					caps._bNativeBindless			= 1; // 후보. 런타임은 supportsNativeBindlessSampling()
					caps._bCompute					= 1;
					caps._bOffscreenRT				= 1;
					caps._bImGuiHooks				= 1;
					caps._bEditorSupported			= 1;
					caps._bComputeRootConstants		= 1;
					caps._bIndirectDraw				= 1;
					caps._bIndexedDraw				= 1;
					caps._bGpuCulling				= 1;
					caps._bMultiDrawIndirect		= 1;
					caps._bParallelCommandRecording = 1;
					break;
			}
			return caps;
		}
	};
} // namespace sw
