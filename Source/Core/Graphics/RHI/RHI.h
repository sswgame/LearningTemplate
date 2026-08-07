#pragma once

#include "Core/CoreMinimal.h"
#include "IRHIDevice.h"
#include "Core/Graphics/Shader/LiveShaderManager.h"

namespace sw
{
	class LiveShaderManager;
} // namespace sw

/**
 * @file RHI.h
 * @brief RHI 팩토리 및 플랫 플랫폼 헬퍼 클래스
 */

namespace sw
{
	enum class ShaderTargetFormat : uint8;

	extern SW_API RHIBackend gv_RHIBackend;

	/**
	 * @class RHI
	 * @brief 플랫폼 및 백엔드 타입에 맞는 IRHIDevice 인스턴스를 생성하는 팩토리 클래스
	 */
	class SW_API RHI
	{
	public:
		RHI()  = default;
		~RHI() = default;

		bool initialize();
		void shutdown();

		/** @brief Soft recreate: destroy current device and create backend without full App restart. */
		bool recreateDevice( RHIBackend backend );

		IRHIDevice&		   getDevice() const { return *_device; }
		LiveShaderManager& getLiveShaderManager() const { return *_liveShaderManager; }

	private:
		std::unique_ptr<LiveShaderManager> _liveShaderManager;
		std::unique_ptr<IRHIDevice>		   _device;

	public:
		/**
		 * @brief 지정된 백엔드 타입(DirectX11, DirectX12, Vulkan, OpenGL)에 해당하는 RHI 디바이스 생성
		 * @param backend 생성할 백엔드 종류
		 * @return 생성된 IRHIDevice의 unique_ptr (실패 시 nullptr)
		 */
		static std::unique_ptr<IRHIDevice> createDevice( RHIBackend backend );

		/** @brief 백엔드 열거형의 표시용(Pretty Name) 문자열 반환 */
		static const utf8* getBackendTypeName( RHIBackend backend );

		/** @brief 현재 OS 플랫폼에서 최적인 기본 RHI 백엔드(Windows: DX12, Linux: Vulkan 등) 반환 */
		static RHIBackend getDefaultPlatformBackend();

		/** @brief 해당 RHI 백엔드에 대응하는 셰이더 타깃 포맷(DXIL, SPIR-V, DXBC 등) 반환 */
		static ShaderTargetFormat getShaderTargetFormat( RHIBackend backend );
	};
} // namespace sw
