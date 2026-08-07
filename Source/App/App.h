#pragma once
/**
 * @file App.h
 * @brief 런타임 클라이언트 앱
 *
 * 소유권: App이 매니저 unique_ptr을 소유하고, bindCoreServices로 전역 조회를 연결합니다.
 */

#include "Core/Graphics/RHI/RHITypes.h"
#include "Core/Graphics/Shader/ShaderReflection.h"
#include "Core/Utility/File/ReloadFileManager.h"
#include "Runtime/EditorAPI.h"
#include "Runtime/EditorUIContext.h"
#include "Runtime/GameAPI.h"

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
	class SceneManager;
	struct NativeWindowEvent;

	/** @brief 윈도우·RHI·에디터/게임 모듈을 구동하는 메인 애플리케이션 */
	class App
	{
	public:
		App();
		~App();

		/** @brief 서브시스템·윈도우·RHI·모듈을 초기화합니다. */
		bool initialize( int argc, char* argv[] );
		/** @brief 메인 루프를 실행합니다. */
		void run();
		/** @brief 모듈·RHI·매니저를 종료합니다. */
		void shutdown();

		/** @brief 레거시: soft recreate로 대체되어 항상 false */
		bool shouldRestartForBackendChange() const { return false; }

	private:
		/** @brief 로거·커맨드라인·태스크 등 코어 매니저를 생성·바인딩합니다. */
		bool initializeSubsystems( int argc, char* argv[] );

		/** @brief 윈도우 리사이즈 시 스왑체인/뷰포트를 갱신합니다. */
		void onResize( uint32 w, uint32 h );
		/** @brief 네이티브 윈도우 이벤트를 에디터/앱에 전달합니다. */
		bool onWindowMessage( const NativeWindowEvent& event );

		/** @brief EditorModule 핫 리로드 직전 에디터를 해제합니다. */
		void onBeforeEditorReload();
		/** @brief EditorModule 핫 리로드 직후 API를 다시 바인딩합니다. */
		void onAfterEditorReload( void* hLibraryModule );

		/** @brief SWGame 핫 리로드 직전 게임을 해제합니다. */
		void onBeforeGameReload();
		/** @brief SWGame 핫 리로드 직후 API를 다시 바인딩합니다. */
		void onAfterGameReload( void* hLibraryModule );

		/** @brief fillEditorAPI로 함수 테이블을 채우고 에디터를 생성합니다. */
		bool bindEditorAPI( void* hLibraryModule );
		/** @brief fillGameAPI로 함수 테이블을 채우고 게임을 생성합니다. */
		bool bindGameAPI( void* hLibraryModule );
		/** @brief Game View용 오프스크린 렌더 타깃을 생성합니다. */
		bool createGameViewportTexture();
		/** @brief 대기 중인 RHI 백엔드 변경을 soft recreate로 적용합니다. */
		bool applyPendingBackendChange();

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

		ShaderReflectionData _reflectionData;
		EditorUIContext		 _editorCtx;
		EditorAPI			 _editorApi{};
		GameAPI				 _gameApi{};
		float32				 _clearColor[4] = { 0.12f, 0.15f, 0.18f, 1.0f };

		FileWatchHandle	 _shaderWatchHandle{};
		RHITextureHandle _gameRenderTarget = 0;
		void*			 _gameTextureID	   = nullptr;
		EditorHandle	 _editor		   = nullptr;
		GameHandle		 _game			   = nullptr;

		RHIBackend _pendingRHIBackend	= RHIBackend::DirectX12;
		RHIBackend _committedRHIBackend = RHIBackend::DirectX12;

		uint8 _bEnableEditor		 : 1 = 0;
		uint8 _bAppRunning			 : 1 = 0;
		uint8 _bPendingBackendChange : 1 = 0;
		uint8 _reservedFlags		 : 5 = 0;
	};
}
