#pragma once
/**
 * @file App.h
 * @brief 런타임 클라이언트 앱
 *
 * 소유권: App이 매니저 unique_ptr을 소유하고, bindCoreServices로 전역 조회를 연결합니다.
 */

#include "Core/Graphics/RHI/RHITypes.h"
#include "Core/Graphics/Shader/ShaderReflection.h"
#include "Runtime/EditorAPI.h"
#include "Runtime/EditorUIContext.h"
#include "Runtime/GameAPI.h"

#include <memory>

namespace sw
{
	class IWindow;
	class Logger;
	class CommandLineManager;
	class TaskManager;
	class GlobalVariableManager;
	class TypeRegistry;
	class ComponentManager;
	class RHI;
	class LiveReloadManager;
	class ReloadFileManager;
	class SceneManager;
	struct NativeWindowEvent;

	class App
	{
	public:
		App();
		~App();

		bool initialize( int argc, char* argv[] );
		void run();
		void shutdown();

		/** @brief RHI 백엔드 변경으로 run()이 종료된 경우 true (main에서 App 재생성) */
		bool shouldRestartForBackendChange() const { return _bBackendChanged; }

	private:
		bool initializeSubsystems( int argc, char* argv[] );

		void onResize( uint32 w, uint32 h );
		bool onWindowMessage( const NativeWindowEvent& event );

		void onBeforeEditorReload();
		void onAfterEditorReload( void* hLibraryModule );

		void onBeforeGameReload();
		void onAfterGameReload( void* hLibraryModule );

		bool bindEditorAPI( void* hLibraryModule );
		bool bindGameAPI( void* hLibraryModule );
		bool createGameViewportTexture();

		std::unique_ptr<Logger>				   _logger;
		std::unique_ptr<CommandLineManager>	   _commandLineManager;
		std::unique_ptr<TaskManager>		   _taskManager;
		std::unique_ptr<GlobalVariableManager> _globalVariableManager;
		std::unique_ptr<TypeRegistry>		   _typeRegistry;
		std::unique_ptr<ComponentManager>	   _componentManager;
		std::unique_ptr<IWindow>			   _window;
		std::unique_ptr<RHI>				   _rhi;
		std::unique_ptr<LiveReloadManager>	   _liveReloadManager;
		std::unique_ptr<ReloadFileManager>	   _reloadFileManager;
		std::unique_ptr<SceneManager>		   _sceneManager;

		bool _bEnableEditor	  = false;
		bool _bAppRunning	  = false;
		bool _bBackendChanged = false;

		RHITextureHandle _gameRenderTarget = 0;
		void*			 _gameTextureID	   = nullptr;

		EditorAPI	 _editorApi{};
		EditorHandle _editor = nullptr;

		GameAPI		_gameApi{};
		GameHandle	_game = nullptr;

		ShaderReflectionData _reflectionData;
		EditorUIContext		 _editorCtx;
		float32				 _clearColor[4] = { 0.12f, 0.15f, 0.18f, 1.0f };
	};
}
