/**
 * @file EngineServices.h
 * @brief App이 소유한 코어 매니저 포인터를 Engine.dll에 바인딩하는 서비스 테이블
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#if !defined( SW_ENGINE_INTERNAL ) && !defined( SW_APP_INTERNAL ) && !defined( SW_TEST_INTERNAL ) && !defined( SW_TOOL_INTERNAL )
	#error "EngineServices.h can only be included internally by the Engine, App, or Tests."
#endif

namespace sw

{
	class CommandLineManager;
	class GlobalVariableManager;
	class TaskManager;
	class TypeRegistry;
	class SceneManager;
	class InputManager;
	class IAudioSystem;
	class EventDispatcher;
	class ResourceManager;
	class LocalizationManager;
	class MemoryProfiler;
	struct EngineData;
	class AssetStreamingQueue;
	class CommandStack;
	struct DebugOverlayState;
	class DebugDrawQueue;
	class FrameDoubleBuffer;
	class RHIBackendRegistry;
	class CompressionCodecRegistry;
	class ShaderCache;
	class ComponentDefaults;
	struct GameData;
	// ------------------------------------------------------------------------------
	// 1) EngineServices — App이 소유한 매니저 포인터 묶음
	//    Engine.dll은 이 테이블만 들고, 생성/파괴는 App
	// ------------------------------------------------------------------------------
	struct EngineServices
	{
		CommandLineManager*	   _pCommandLineManager{ nullptr };
		GlobalVariableManager* _pGlobalVariableManager{ nullptr };
		LocalizationManager*   _pLocalizationManager{ nullptr };
		TaskManager*		   _pTaskManager{ nullptr };
		TypeRegistry*		   _pTypeRegistry{ nullptr };
		SceneManager*		   _pSceneManager{ nullptr };
		InputManager*		   _pInputManager{ nullptr };
		IAudioSystem*		   _pAudioSystem{ nullptr };
		EventDispatcher*	   _pEventDispatcher{ nullptr };
		ResourceManager*	   _pResourceManager{ nullptr };
		MemoryProfiler*		   _pMemoryProfiler{ nullptr };

		EngineData*				  _pEngineData{ nullptr };
		AssetStreamingQueue*	  _pAssetStreamingQueue{ nullptr };
		CommandStack*			  _pCommandStack{ nullptr };
		DebugOverlayState*		  _pDebugOverlayState{ nullptr };
		DebugDrawQueue*			  _pDebugDrawQueue{ nullptr };
		FrameDoubleBuffer*		  _pFrameDoubleBuffer{ nullptr };
		RHIBackendRegistry*		  _pRHIBackendRegistry{ nullptr };
		CompressionCodecRegistry* _pCompressionCodecRegistry{ nullptr };
		ShaderCache*			  _pShaderCache{ nullptr };
		ComponentDefaults*		  _pComponentDefaults{ nullptr };
		GameData*				  _pGameData{ nullptr };
	};

	namespace engine
	{
		// ------------------------------------------------------------------------------
		// 2) 바인딩 — initialize 때 연결, shutdown 때 해제
		//    MemoryProfiler는 선택, 나머지는 필수
		// ------------------------------------------------------------------------------
		/** @brief Engine.dll 전역 조회가 사용할 매니저 포인터를 바인딩합니다. */
		SW_API void bindEngineServices( const EngineServices& services );
		/** @brief 바인딩을 해제합니다 (앱 종료 시). */
		SW_API void unbindEngineServices();
		/** @brief 필수 매니저가 모두 바인딩되었는지 (MemoryProfiler는 선택). */
		SW_API bool areEngineServicesBound();

		// ------------------------------------------------------------------------------
		// 3) 코어 매니저 조회 — bind 이후에만 호출
		// ------------------------------------------------------------------------------
		/** @brief 바인딩된 CommandLineManager를 반환합니다. */
		SW_API CommandLineManager& getCommandLineManager();
		/** @brief 바인딩된 GlobalVariableManager를 반환합니다. */
		SW_API GlobalVariableManager& getGlobalVariableManager();
		/** @brief 바인딩된 LocalizationManager를 반환합니다. */
		SW_API LocalizationManager& getLocalizationManager();
		/** @brief 바인딩된 TaskManager를 반환합니다. */
		SW_API TaskManager& getTaskManager();
		/** @brief 바인딩된 TypeRegistry를 반환합니다. */
		SW_API TypeRegistry& getTypeRegistry();
		/** @brief 바인딩된 SceneManager를 반환합니다. */
		SW_API SceneManager& getSceneManager();
		/** @brief 바인딩된 InputManager를 반환합니다. */
		SW_API InputManager& getInputManager();
		/** @brief 바인딩된 IAudioSystem을 반환합니다. */
		SW_API IAudioSystem& getAudioSystem();
		/** @brief 바인딩된 EventDispatcher를 반환합니다. */
		SW_API EventDispatcher& getEventDispatcher();

		// ------------------------------------------------------------------------------
		// 4) 팩 에셋 · 프로파일러 · 유틸리티
		// ------------------------------------------------------------------------------
		/** @brief 바인딩된 ResourceManager를 반환합니다 (GUID · 스키마 · Material · Prefab). */
		SW_API ResourceManager& getResourceManager();
		/** @brief 바인딩된 MemoryProfiler를 반환합니다. 없으면 nullptr. */
		SW_API MemoryProfiler* getMemoryProfiler();

		SW_API const EngineData&		 getEngineData();
		SW_API AssetStreamingQueue&		 getAssetStreamingQueue();
		SW_API CommandStack&			 getCommandStack();
		SW_API DebugOverlayState&		 getDebugOverlayState();
		SW_API DebugDrawQueue&			 getDebugDrawQueue();
		SW_API FrameDoubleBuffer&		 getFrameDoubleBuffer();
		SW_API RHIBackendRegistry&		 getRHIBackendRegistry();
		SW_API CompressionCodecRegistry& getCompressionCodecRegistry();
		SW_API ShaderCache&				 getShaderCache();
		SW_API ComponentDefaults&		 getComponentDefaults();
		SW_API const GameData&			 getGameData();
	} // namespace engine
} // namespace sw
