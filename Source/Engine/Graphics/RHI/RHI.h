/**
 * @file RHI.h
 * @brief RHI 팩토리와 플랫폼 헬퍼
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	class IRHIDevice;
	class LiveShaderManager;
	enum class ShaderTargetFormat : uint8;
} // namespace sw

namespace sw
{
	extern SW_API RHIBackend		 gv_rhiBackend;
	extern SW_API RHICommandListMode gv_rhiCommandListMode;

	/**
	 * @class RHI
	 * @brief 플랫폼/백엔드에 맞는 IRHIDevice를 만드는 팩토리
	 */
	class SW_API RHI
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 수명 — 복사/이동 금지, initialize/shutdown, 소프트 재생성
		// ------------------------------------------------------------------------------
		/** @brief 빈 RHI (디바이스 없음). */
		RHI();
		/** @brief 디바이스와 LiveShaderManager를 정리합니다. */
		~RHI();

		/** @brief 복사를 금지합니다. */
		RHI( const RHI& ) = delete;
		/** @brief 대입을 금지합니다. */
		RHI& operator=( const RHI& ) = delete;
		/** @brief 이동을 금지합니다. */
		RHI( RHI&& ) = delete;
		/** @brief 이동 대입을 금지합니다. */
		RHI& operator=( RHI&& ) = delete;

		/** @brief 기본 백엔드로 디바이스를 초기화합니다. */
		bool initialize();
		/** @brief CLI에 --VSYNC가 없을 때 쓸 스왑체인 VSync입니다. initialize 전에 호출합니다. */
		void setPreferredVSync( bool bVSync ) { _bPreferredVSync = bVSync ? SW_TRUE : SW_FALSE; }
		/** @brief 디바이스를 종료하고 모듈을 언로드합니다. */
		void shutdown();

		/** @brief 앱 재시작 없이 현재 디바이스를 파괴하고 백엔드를 다시 만듭니다. */
		bool recreateDevice( RHIBackend backend );

		/** @brief gv_rhiBackend 변경 시 핫스왑을 예약합니다. */
		void schedulePendingBackendChange( RHIBackend requested );
		/** @brief 대기 중인 백엔드 변경이 있는지 반환합니다. */
		bool hasPendingBackendChange() const { return _bPendingBackendChange == SW_TRUE; }
		/** @brief 대기 중인 백엔드를 가져가고 플래그를 해제합니다. */
		RHIBackend consumePendingBackendChange();
		/** @brief 커밋된 백엔드를 설정합니다 (핵스왁 성공/실패 후 동기화용). */
		void setCommittedBackend( RHIBackend backend ) { _committedRHIBackend = backend; }
		/** @brief 커밋된 백엔드를 반환합니다. */
		RHIBackend getCommittedBackend() const { return _committedRHIBackend; }

		// ------------------------------------------------------------------------------
		// 2) 팩토리 — 백엔드 생성, 표시 이름, 플랫폼 기본, 셰이더 타깃
		// ------------------------------------------------------------------------------
		/**
		 * @brief 지정 백엔드(DirectX11, DirectX12, Vulkan, OpenGL)의 RHI 디바이스를 만듭니다.
		 * @param backend 생성할 백엔드 종류
		 * @return 생성된 IRHIDevice unique_ptr (실패 시 nullptr)
		 */
		static unique_ptr<IRHIDevice> createDevice( RHIBackend backend );

		/** @brief 백엔드 열거형의 표시용 이름을 반환합니다. */
		static const utf8* getBackendTypeName( RHIBackend backend );

		/** @brief 현재 OS에서 기본 RHI 백엔드를 반환합니다 (Windows: DX12, Linux: Vulkan 등). */
		static RHIBackend getDefaultPlatformBackend();

		/** @brief 해당 RHI 백엔드의 셰이더 타깃 포맷(DXIL, SPIR-V, DXBC 등)을 반환합니다. */
		static ShaderTargetFormat getShaderTargetFormat( RHIBackend backend );

		// ------------------------------------------------------------------------------
		// 3) 조회 — initialize 이후
		// ------------------------------------------------------------------------------
		/** @brief 디바이스가 활성화되어 있는지 여부를 반환합니다. */
		bool hasDevice() const { return _device != nullptr; }
		/** @brief 활성 IRHIDevice를 반환합니다. */
		IRHIDevice& getDevice() const { return *_device; }
		/** @brief 활성 LiveShaderManager를 반환합니다. */
		LiveShaderManager& getLiveShaderManager() const { return *_liveShaderManager; }

	private:
		unique_ptr<LiveShaderManager> _liveShaderManager;
		unique_ptr<IRHIDevice>		  _device;
		RHIBackend					  _pendingRHIBackend;
		RHIBackend					  _committedRHIBackend;
		uint8						  _bPreferredVSync		 : 1;
		uint8						  _bPendingBackendChange : 1;
		[[maybe_unused]] uint8		  _reserved				 : 6;
	};
} // namespace sw
