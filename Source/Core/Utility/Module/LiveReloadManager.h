#pragma once
/**
 * @file LiveReloadManager.h
 * @brief 모듈 DLL 섀도 복사 기반 핫 리로드
 */
#include "Core/Common/Common.h"
#include "Core/Utility/File/FileUtil.h"

namespace sw
{
	class LiveReloadManager
	{
	public:
		using OnBeforeReloadDelegate = sw::Delegate<void()>;
		using OnAfterReloadDelegate	 = sw::Delegate<void( void* hLibraryModule )>;

		LiveReloadManager();
		~LiveReloadManager();

		bool initialize();
		void shutdown();

		bool  registerModule( const std::string& moduleName );
		void  triggerReload( const std::string& moduleName );
		void  update();
		void* getModuleHandle( const std::string& moduleName ) const;

		void setOnBeforeReload( const std::string& moduleName, OnBeforeReloadDelegate delegate );
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

		bool loadShadowCopyModule( ModuleContext& ctx );

		std::unordered_map<std::string, ModuleContext> _modules;
	};
}
