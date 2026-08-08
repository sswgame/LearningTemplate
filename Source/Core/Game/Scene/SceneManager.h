#pragma once

#include "Core/Common/CommonHeaders.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Game/Scene/SceneDescriptor.h"
#include "Core/Utility/Task/TaskTypes.h"

#include <atomic>
#include <mutex>

namespace sw
{
	class IRHIDevice;
	class FrameRenderer;

	/**
	 * @class SceneManager
	 * @brief 로드된 씬들을 관리하고, Active Scene을 추적합니다.
	 */
	class SW_API SceneManager
	{
	public:
		SceneManager()	= default;
		~SceneManager() = default;

		SceneManager( const SceneManager& )			   = delete;
		SceneManager& operator=( const SceneManager& ) = delete;

		bool initialize();
		void shutdown();

		/** @brief Optional device used to initialize scenes created by async load. */
		void setRhiDevice( IRHIDevice* device ) { _rhiDevice = device; }
		/** @brief Optional FrameRenderer propagated to newly activated scenes. */
		void setFrameRenderer( FrameRenderer* frameRenderer ) { _frameRenderer = frameRenderer; }

		/** @brief 새로운 씬을 생성하고 활성 씬으로 지정합니다. */
		Scene* createScene( const std::string& name );

		/** @brief 현재 활성화된(주요) 씬 반환 */
		Scene* getActiveScene() const { return _activeScene; }

		/** @brief Request async load of a scene descriptor XML (worker via TaskManager). */
		bool requestLoadAsync( const std::string& path );

		/** @brief Main-thread: swap in completed async loads. Call once per frame. */
		void tickTransitions();

		/** @brief True while an async load is outstanding or waiting to swap. */
		bool isTransitioning() const;

		/** @brief 로드된 모든 씬 업데이트 */
		void update( float32 deltaTime );

		/** @brief 로드된 씬 모두 반환 */
		const std::vector<std::unique_ptr<Scene>>& getLoadedScenes() const { return _loadedScenes; }

	private:
		void applyPendingDescriptor( const SceneDescriptor& desc );

		std::vector<std::unique_ptr<Scene>> _loadedScenes;
		Scene*								_activeScene	= nullptr;
		IRHIDevice*							_rhiDevice		= nullptr;
		FrameRenderer*						_frameRenderer	= nullptr;

		mutable std::mutex	 _pendingMutex;
		SceneDescriptor		 _pendingDescriptor{};
		std::atomic<bool>	 _bPendingReady{ false };
		std::atomic<bool>	 _bLoadInFlight{ false };
		TaskHandle			 _loadHandle{};
	};
} // namespace sw

