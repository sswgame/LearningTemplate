/**
 * @file EngineLoop.h
 * @brief 엔진 코어 메인 루프 및 서브시스템 소유권 관리
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/memory.h"

#include "Engine/Common/Common.h"
#include "Engine/Graphics/RenderPass/RenderFramePacket.h"

namespace sw
{
	class Logger;
	class DeadlockDetector;
	class MemoryProfiler;
	class ConfigManager;
	class CommandLineManager;
	class TaskManager;
	class GlobalVariableManager;
	class TypeRegistry;
	class RHI;
	class LiveReloadManager;
	class ReloadFileManager;
	class SceneManager;
	class InputManager;
	class ActionMap;
	class IAudioSystem;
	class EventDispatcher;
	class FrameRenderer;
	class RenderThread;
	class ResourceManager;
	class LocalizationManager;
	struct EngineData;
	class AssetStreamingQueue;
	class CommandStack;
	struct DebugOverlayState;
	class DebugDrawQueue;
	class FrameDoubleBuffer;
	class RHIBackendRegistry;

	class IRHIDevice;

	/**
	 * @class EngineLoop
	 * @brief App이 보유하던 코어 매니저들을 캡슐화하고 메인 루프(tick)를 담당합니다.
	 */
	class SW_API EngineLoop
	{
	public:
		EngineLoop();
		~EngineLoop();

		EngineLoop( const EngineLoop& )			   = delete;
		EngineLoop& operator=( const EngineLoop& ) = delete;

		/** @brief 서브시스템(윈도우, RHI 포함)을 초기화합니다. */
		bool initialize( int32 argc, utf8* pArgv[] );
		/** @brief 매니저들을 종료하고 정리합니다. */
		void shutdown();

		/** @brief 입력 등을 시작하는 프레임의 첫 단계입니다. */
		void beginFrame();
		/**
		 * @brief 씬 업데이트, RHI 제출 등을 수행합니다.
		 * @param deltaTime 델타 타임
		 * @param bEnableEditor 에디터 활성화 여부
		 * @param gameRenderTarget 에디터 Game View RT 식별자 (없으면 0)
		 * @param vpWidth 뷰포트 너비
		 * @param vpHeight 뷰포트 높이
		 */
		void tick( float32 deltaTime, bool bEnableEditor, uint64 gameRenderTarget, uint32 vpWidth, uint32 vpHeight );
		/** @brief 입력 종료 등 프레임의 마지막 단계입니다. */
		void endFrame();

		/** @brief 대기 중인 RHI 핫스왑을 수행합니다. */
		bool applyPendingBackendChange();

		// ----------------------------------------------------------------------
		// 헬퍼
		// ----------------------------------------------------------------------
		void setPresentHook( sw::PresentHookDelegate presentHook );
		void updateShellActions( float32 deltaTime );
		void pollDebugHotkeys( bool bEnableEditor, const Delegate<void( const utf8* )>& forceReloadCallback );

		// ----------------------------------------------------------------------
		// Getter (App이 ModuleHost 등과 연동하기 위해 필요)
		// ----------------------------------------------------------------------
		LiveReloadManager*	 getLiveReloadManager() const { return _liveReloadManager.get(); }
		ConfigManager*		 getConfigManager() const { return _configManager.get(); }
		CommandLineManager*	 getCommandLineManager() const { return _commandLineManager.get(); }
		LocalizationManager* getLocalizationManager() const { return _localizationManager.get(); }
		RHI*				 getRHI() const { return _rhi.get(); }
		RenderThread*		 getRenderThread() const { return _renderThread.get(); }

	private:
		unique_ptr<Logger>				  _logger;
		unique_ptr<DeadlockDetector>	  _deadlockDetector;
		unique_ptr<MemoryProfiler>		  _memoryProfiler;
		unique_ptr<CommandLineManager>	  _commandLineManager;
		unique_ptr<TaskManager>			  _taskManager;
		unique_ptr<GlobalVariableManager> _globalVariableManager;
		unique_ptr<TypeRegistry>		  _typeRegistry;
		unique_ptr<ConfigManager>		  _configManager;
		unique_ptr<LocalizationManager>	  _localizationManager;
		unique_ptr<ResourceManager>		  _resourceManager;
		unique_ptr<RHI>					  _rhi;
		unique_ptr<LiveReloadManager>	  _liveReloadManager;
		unique_ptr<ReloadFileManager>	  _reloadFileManager;
		unique_ptr<SceneManager>		  _sceneManager;
		unique_ptr<InputManager>		  _inputManager;
		unique_ptr<ActionMap>			  _mapDebugAction;
		unique_ptr<IAudioSystem>		  _audioSystem;
		unique_ptr<EventDispatcher>		  _eventDispatcher;
		unique_ptr<FrameRenderer>		  _frameRenderer;
		unique_ptr<RenderThread>		  _renderThread;
		unique_ptr<EngineData>			  _engineData;
		unique_ptr<AssetStreamingQueue>	  _assetStreamingQueue;
		unique_ptr<CommandStack>		  _commandStack;
		unique_ptr<DebugOverlayState>	  _debugOverlayState;
		unique_ptr<DebugDrawQueue>		  _debugDrawQueue;
		unique_ptr<FrameDoubleBuffer>	  _frameDoubleBuffer;
		unique_ptr<RHIBackendRegistry>	  _rhiBackendRegistry;

		bool _bShellActionsBound;
	};
} // namespace sw
