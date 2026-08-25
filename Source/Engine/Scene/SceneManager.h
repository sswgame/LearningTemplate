#pragma once
#include "Engine/Utility/Task/TaskTypes.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include <atomic>
#include <memory>

namespace sw
{
	class IRHIDevice;
	class FrameRenderer;
	class Scene;

	/**
	 * @class SceneManager
	 * @brief 로드된 씬들을 관리하고, Active Scene을 추적합니다.
	 */
	class SW_API SceneManager
	{
	public:
		/** @brief 빈 매니저. */
		SceneManager();
		/** @brief 매니저를 해제합니다. */
		~SceneManager();

		/** @brief 복사를 금지합니다. */
		SceneManager( const SceneManager& ) = delete;
		/** @brief 대입을 금지합니다. */
		SceneManager& operator=( const SceneManager& ) = delete;

		/** @brief 매니저를 초기화합니다. */
		bool initialize();
		/** @brief 로드된 씬을 내리고 종료합니다. */
		void shutdown();

		/** @brief 새로운 씬을 생성하고 활성 씬으로 지정합니다. */
		Scene* createScene( string_view name );
		/** @brief 씬 디스크립터 XML 비동기 로드를 요청합니다 (TaskManager 워커). */
		bool requestLoadAsync( string_view path );
		/**
		 * @brief 활성 씬 루트를 디스크립터 XML로 저장합니다.
		 * @param path 리소스 상대 또는 절대. 비어 있으면 씬 소스 경로를 씁니다.
		 */
		bool saveActiveScene( string_view path = {} );
		/** @brief 메인 스레드: 완료된 비동기 로드를 교체합니다. 프레임당 한 번 호출하세요. */
		void tickTransitions();
		/** @brief 활성 씬만 tick. App 메인 루프가 호출한다 (게임 모듈에서 중복 호출하지 말 것). */
		void tick( float32 deltaTime );

		/** @brief 비동기 로드로 만든 씬 초기화에 쓸 디바이스를 설정합니다. */
		void setRhiDevice( IRHIDevice* pRhiDevice ) { _pRHIDevice = pRhiDevice; }
		/** @brief 새로 활성화된 씬에 전달할 FrameRenderer를 설정합니다. */
		void setFrameRenderer( FrameRenderer* pFrameRenderer ) { _pFrameRenderer = pFrameRenderer; }

		/** @brief 현재 활성화된(주요) 씬 반환 */
		Scene* getActiveScene() const { return _pActiveScene; }
		/** @brief 비동기 로드가 진행 중이거나 교체 대기면 true입니다. */
		bool isTransitioning() const;
		/** @brief 진행 중인 비동기 씬 로드 작업을 즉시 취소/대기하고 큐를 비웁니다. (핫리로드/종료 펜스용) */
		void cancelPendingAsyncLoads();
		/** @brief 로드된 씬 모두 반환 */
		const vector<unique_ptr<Scene>>& getLoadedScenes() const { return _listLoadedScenes; }

	private:
		/** @brief 씬을 언로드하고 목록에서 제거합니다. */
		void unloadScene( Scene* pScene );

	private:
		struct AsyncLoadSlot
		{
			mutex			  _mutex;
			unique_ptr<Scene> _scene;
			std::atomic<bool> _bReady{ false };
			std::atomic<bool> _bAccepting{ true };
		};

		vector<unique_ptr<Scene>> _listLoadedScenes;
		Scene*					  _pActiveScene;
		IRHIDevice*				  _pRHIDevice;
		FrameRenderer*			  _pFrameRenderer;

		shared_ptr<AsyncLoadSlot> _asyncLoad;
		string					  _queuedPath;
		std::atomic<bool>		  _bLoadInFlight;
		TaskHandle				  _loadHandle;
		bool					  _bInitialized;
	};
} // namespace sw
