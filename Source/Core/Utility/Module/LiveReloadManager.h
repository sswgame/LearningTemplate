#pragma once
/**
 * @file LiveReloadManager.h
 * @brief 모듈 DLL 섀도 복사 기반 핫 리로드
 */
#include "Core/Common/Common.h"
#include "Core/Utility/File/FileUtil.h"

namespace sw
{
	/** @brief DLL을 임시 경로에 복사해 로드하는 핫 리로드 매니저 */
	class LiveReloadManager
	{
	public:
		using OnBeforeReloadDelegate = sw::Delegate<void()>;
		using OnAfterReloadDelegate	 = sw::Delegate<void( void* hLibraryModule )>;

		LiveReloadManager();
		~LiveReloadManager();

		/** @brief 매니저를 초기화합니다. */
		bool initialize();
		/** @brief 로드된 모듈을 언로드하고 종료합니다. */
		void shutdown();

		/** @brief 모듈 이름을 등록하고 섀도 복사본을 로드합니다. */
		bool  registerModule( const std::string& moduleName );
		/** @brief 다음 update에서 해당 모듈 리로드를 예약합니다. */
		void  triggerReload( const std::string& moduleName );
		/** @brief 예약된 리로드를 수행합니다. */
		void  update();
		/** @brief 현재 로드된 모듈 핸들을 반환합니다. */
		void* getModuleHandle( const std::string& moduleName ) const;

		/** @brief 리로드 직전 콜백을 설정합니다. */
		void setOnBeforeReload( const std::string& moduleName, OnBeforeReloadDelegate delegate );
		/** @brief 리로드 직후 콜백을 설정합니다. */
		void setOnAfterReload( const std::string& moduleName, OnAfterReloadDelegate delegate );

	private:
		struct ModuleContext
		{
			std::string			   _moduleName;
			std::string			   _originalDllPath;
			std::string			   _tempDllPath;
			void*				   _hLibraryModule = nullptr;
			bool				   _bPendingReload = false;
			OnBeforeReloadDelegate _onBeforeReload;
			OnAfterReloadDelegate  _onAfterReload;
		};

		/** @brief 원본 DLL을 임시 경로에 복사한 뒤 로드합니다. */
		bool loadShadowCopyModule( ModuleContext& ctx );

		std::unordered_map<std::string, ModuleContext> _modules;
	};
}
