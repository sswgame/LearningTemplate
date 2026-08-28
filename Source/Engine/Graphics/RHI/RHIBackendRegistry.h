/**
 * @file RHIBackendRegistry.h
 * @brief 플러그형 RHI 백엔드 팩토리 레지스트리 (정적 링크 또는 SW_RHI_AS_MODULES DLL)
 *
 * @note 수명: create()로 만든 디바이스는 unloadModules() / 레지스트리 해체 전에 파괴해야 합니다.
 *       RHI::shutdown()이 활성 디바이스 reset 후 unloadModules를 호출합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	class IRHIDevice;

	using RHIDeviceFactoryDelegate = Delegate<unique_ptr<IRHIDevice>()>;

	// ------------------------------------------------------------------------------
	// 1) RHIBackendEntry — 백엔드 + 팩토리 + capability
	// ------------------------------------------------------------------------------
	struct RHIBackendEntry
	{
		RHIBackend				 _backend{};
		RHIDeviceFactoryDelegate _factory;
		RHICapabilities			 _caps{};
	};

	/**
	 * @class RHIBackendRegistry
	 * @brief 백엔드 팩토리 등록·조회·MODULE 로드
	 */
	class SW_API RHIBackendRegistry
	{
	public:
	public:
		// ------------------------------------------------------------------------------
		// 2) 등록 · 조회 · 생성
		// ------------------------------------------------------------------------------
		/** @brief 백엔드 팩토리와 capability를 등록합니다. */
		void registerBackend( RHIBackend backend, const RHIDeviceFactoryDelegate& factory, const RHICapabilities& caps );
		/** @brief 등록된 백엔드 항목을 찾습니다. */
		const RHIBackendEntry* findBackend( RHIBackend backend ) const;

		/** @brief 등록된 팩토리로 IRHIDevice를 만듭니다. */
		unique_ptr<IRHIDevice> createDevice( RHIBackend backend ) const;

		// ------------------------------------------------------------------------------
		// 3) MODULE — createRHIDevice export DLL, 언로드 전 디바이스 파괴 필수
		// ------------------------------------------------------------------------------
		/** @brief createRHIDevice를 export하는 DLL을 선택 로드합니다. */
		bool tryLoadModule( RHIBackend backend, string_view modulePath );

		/**
		 * @brief MODULE 팩토리를 지운 뒤 FreeLibrary합니다.
		 * @warning MODULE 백엔드에서 만든 IRHIDevice를 먼저 모두 파괴하세요.
		 */
		void unloadModules();

	private:
	public:
		/** @brief 빈 레지스트리. */
		RHIBackendRegistry();
		/** @brief 로드된 모듈을 정리합니다. */
		~RHIBackendRegistry();

		/** @brief 복사를 금지합니다. */
		RHIBackendRegistry( const RHIBackendRegistry& ) = delete;
		/** @brief 대입을 금지합니다. */
		RHIBackendRegistry& operator=( const RHIBackendRegistry& ) = delete;

		struct LoadedModule
		{
			RHIBackend _backend{};
			void*	   _pHandle{ nullptr };
		};

		vector<RHIBackendEntry> _listEntry;
		vector<LoadedModule>	_listLoadedModule;
	};
} // namespace sw
