#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/SceneDescriptor.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Task/TaskManager.h"

#include "Core/Concurrency/mutex.h"

namespace sw
{
	SceneManager::SceneManager()
		: _listLoadedScenes{}
		, _pActiveScene{ nullptr }
		, _pRHIDevice{ nullptr }
		, _pFrameRenderer{ nullptr }
		, _asyncLoad{ sw::make_shared<AsyncLoadSlot>() }
		, _queuedPath{}
		, _bLoadInFlight{ false }
		, _loadHandle{}
		, _bInitialized{ false }
	{
	}

	SceneManager::~SceneManager()
	{
		shutdown();
	}

	/**
	 * @brief 씬 매니저를 초기화하고 비동기 로드 슬롯을 준비합니다.
	 */
	bool SceneManager::initialize()
	{
		if ( _asyncLoad == nullptr )
			_asyncLoad = sw::make_shared<AsyncLoadSlot>();
		_asyncLoad->_bReady.store( false, std::memory_order_release );
		_asyncLoad->_bAccepting.store( true, std::memory_order_release );
		{
			std::scoped_lock<mutex> lock{ _asyncLoad->_mutex };
			_asyncLoad->_scene.reset();
		}
		_bLoadInFlight = false;
		_queuedPath.clear();
		_bInitialized = true;

		SW_LOG_INFO( "[SceneManager] Initialized." );
		return true;
	}

	/**
	 * @brief 로드 중인 비동기 작업을 안전하게 완료 대기하고 모든 활성 씬을 해제합니다.
	 */
	void SceneManager::shutdown()
	{
		if ( _bInitialized == false )
			return;

		_bInitialized = false;
		_queuedPath.clear();
		if ( _asyncLoad != nullptr )
			_asyncLoad->_bAccepting.store( false, std::memory_order_release );

		const bool bHasAsyncLoad = ( _asyncLoad != nullptr );
		const bool bAsyncReady	 = bHasAsyncLoad && ( _asyncLoad->_bReady.load( std::memory_order_acquire ) );
		const bool bLoading		 = ( _bLoadInFlight.load( std::memory_order_acquire ) );
		const bool bNeedWait	 = engine::areEngineServicesBound() && ( bLoading || bAsyncReady );

		if ( bNeedWait )
			engine::getTaskManager().waitAll();

		if ( _asyncLoad != nullptr )
		{
			std::scoped_lock<mutex> lock{ _asyncLoad->_mutex };
			if ( _asyncLoad->_scene != nullptr )
				_asyncLoad->_scene->shutdown();
			_asyncLoad->_scene.reset();
			_asyncLoad->_bReady.store( false, std::memory_order_release );
		}
		_bLoadInFlight = false;
		for ( auto& scene : _listLoadedScenes )
		{
			if ( scene == nullptr )
				continue;
			scene->shutdown();
		}
		_listLoadedScenes.clear();
		_pActiveScene = nullptr;
		if ( engine::areEngineServicesBound() )
		{
			engine::getCommandStack().clear();
		}
		SW_LOG_INFO( "[SceneManager] Shut down." );
	}

	/**
	 * @brief 새로운 빈 씬을 생성하여 등록하고 활성 씬이 없으면 활성 씬으로 지정합니다.
	 */
	Scene* SceneManager::createScene( string_view name )
	{
		unique_ptr<Scene> scene	 = sw::make_unique<Scene>( name );
		Scene*			  pScene = scene.get();

		_listLoadedScenes.push_back( std::move( scene ) );

		if ( _pActiveScene == nullptr )
			_pActiveScene = pScene;

		return pScene;
	}

	/**
	 * @brief XML 씬 파일 경로로부터 백그라운드 워커 스레드 비동기 로드를 요청합니다.
	 */
	bool SceneManager::requestLoadAsync( string_view path )
	{
		if ( path.empty() )
		{
			SW_LOG_WARNING( "[SceneManager] requestLoadAsync: empty path" );
			return false;
		}
		if ( _asyncLoad == nullptr || _asyncLoad->_bAccepting.load( std::memory_order_acquire ) == false )
		{
			SW_LOG_WARNING( "[SceneManager] requestLoadAsync: manager is shutting down" );
			return false;
		}

		bool expected{ false };
		if ( _bLoadInFlight.compare_exchange_strong( expected, true ) == false )
		{
			_queuedPath = path;
			SW_LOG_INFO( "[SceneManager] Async load in flight — queued '%#'", path );
			return true;
		}

		_asyncLoad->_bReady.store( false, std::memory_order_release );
		SW_LOG_INFO( "[SceneManager] requestLoadAsync: %#", path );

		shared_ptr<AsyncLoadSlot> slot = _asyncLoad;
		_loadHandle					   = engine::getTaskManager().emplaceTask( "SceneLoadAsync",
																			   SW_DELEGATE_LAMBDA( TaskDelegate, [slot, pathStr = string( path )]()
		{
			if ( slot == nullptr || slot->_bAccepting.load( std::memory_order_acquire ) == false )
				return;

			SceneDescriptor desc{};
			const bool		ok = loadSceneDescriptor( pathStr, desc );

			sw::unique_ptr<Scene> newScene;
			if ( ok )
			{
				newScene = sw::make_unique<Scene>( desc._name.empty() ? "LoadedScene" : desc._name );
				newScene->setSourcePath( pathStr );

				GameObjectManager* pObjects = newScene->getObjectManager();
				if ( pObjects != nullptr )
				{
					vector<std::pair<GameObject*, string_view>> listRebindTargets;
					listRebindTargets.reserve( desc._listEntities.size() );

					for ( const SceneEntityPlaceholder& ent : desc._listEntities )
					{
						if ( slot->_bAccepting.load( std::memory_order_relaxed ) == false )
						{
							newScene->shutdown();
							newScene.reset();
							return;
						}
						SW_LOG_INFO( "[SceneManager] Spawning entity '%#' prefab '%#'", ent._name, ent._prefab );
						GameObject* pGo{ nullptr };
						if ( ent._prefab.empty() == false )
						{
							pGo = engine::getResourceManager().getPrefabManager().spawn( pObjects, ent._prefab, ent._name.c_str() );
							if ( pGo == nullptr )
								SW_LOG_WARNING( "[SceneManager] Prefab spawn failed '%#' (%#)", ent._name, ent._prefab );
						}
						else
							pGo = pObjects->createGameObject( hashed_string( ent._name.c_str() ) );

						if ( pGo != nullptr && ent._embeddedXml.empty() == false )
						{
							if ( ObjectStateSerializer::loadFromXmlString( pGo, ent._embeddedXml ) == false )
								SW_LOG_WARNING( "[SceneManager] Embedded state apply failed for '%#'", ent._name );

							const bool bHasHierarchy = ( ent._embeddedXml.find( "parent=" ) != string::npos ||
														 ent._embeddedXml.find( "<ParentGO>" ) != string::npos );
							if ( bHasHierarchy )
								listRebindTargets.emplace_back( pGo, ent._embeddedXml );
						}
					}

					for ( const auto& [pTargetGo, xmlView] : listRebindTargets )
					{
						if ( pTargetGo != nullptr )
							ObjectStateSerializer::rebindSceneHierarchy( pTargetGo, xmlView );
					}

					pObjects->mergePendingAdds();
					pObjects->flushSceneTransforms();
				}
			}

			if ( slot->_bAccepting.load( std::memory_order_acquire ) == false )
			{
				if ( newScene != nullptr )
				{
					newScene->shutdown();
					newScene.reset();
				}
				return;
			}

			const bool bSuccess = ( newScene != nullptr );
			{
				std::scoped_lock<mutex> lock{ slot->_mutex };
				slot->_scene = std::move( newScene );
			}
			slot->_bReady.store( true, std::memory_order_release );
			if ( bSuccess )
			{
				SW_LOG_INFO( "[SceneManager] Async load task completed for '%#'", pathStr );
			}
			else
			{
				SW_LOG_ERROR( "[SceneManager] Async load task failed for '%#'", pathStr );
			}
		} ) );

		_loadHandle.submit();
		return _loadHandle.isValid();
	}

	void SceneManager::cancelPendingAsyncLoads()
	{
		if ( _asyncLoad != nullptr )
			_asyncLoad->_bAccepting.store( false, std::memory_order_release );

		if ( _loadHandle.isValid() )
			engine::getTaskManager().waitAll();

		_bLoadInFlight.store( false, std::memory_order_release );
		_queuedPath.clear();

		if ( _asyncLoad != nullptr )
		{
			std::scoped_lock<mutex> lock{ _asyncLoad->_mutex };
			_asyncLoad->_scene.reset();
			_asyncLoad->_bReady.store( false, std::memory_order_release );
			_asyncLoad->_bAccepting.store( true, std::memory_order_release );
		}
	}

	/**
	 * @brief 현재 활성화된 씬의 최상위 계층 오브젝트 상태들을 XML 씬 서술자 파일로 직렬화 저장합니다.
	 */
	bool SceneManager::saveActiveScene( string_view path )
	{
		Scene* pScene = getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
		{
			SW_LOG_WARNING( "[SceneManager] saveActiveScene: no active scene" );
			return false;
		}

		string outPath( path );
		if ( outPath.empty() )
			outPath = pScene->getSourcePath();
		if ( outPath.empty() )
			outPath = "Assets/Scenes/DefaultScene.scene";

		SceneDescriptor desc{};
		desc._name		 = pScene->getName();
		desc._sourcePath = outPath;
		desc._bValid	 = true;

		// 부모가 없는 루트 게임 오브젝트들만 수집하여 재귀적으로 XML 저장
		for ( GameObject* pGo : pScene->getObjectManager()->getAllGameObjects() )
		{
			if ( pGo == nullptr || pGo->getParent() != nullptr )
				continue;
			SceneEntityPlaceholder ent{};
			ent._name		 = pGo->getName().c_str();
			ent._embeddedXml = ObjectStateSerializer::saveToXmlString( pGo );
			if ( ent._embeddedXml.empty() == false )
				desc._listEntities.push_back( std::move( ent ) );
		}

		if ( saveSceneDescriptorToXml( outPath, desc ) == false )
			return false;

		pScene->setSourcePath( outPath );
		return true;
	}

	/**
	 * @brief 백그라운드에서 완료된 비동기 로드 결과를 메인 스레드 안전 시점에 활성 씬으로 교체(Swap) 적용합니다.
	 */
	void SceneManager::tickTransitions()
	{
		if ( _asyncLoad == nullptr || _asyncLoad->_bReady.load( std::memory_order_acquire ) == false )
			return;

		unique_ptr<Scene> pendingScene;
		{
			std::scoped_lock<mutex> lock{ _asyncLoad->_mutex };
			pendingScene = std::move( _asyncLoad->_scene );
		}
		_asyncLoad->_bReady.store( false, std::memory_order_release );
		_bLoadInFlight.store( false, std::memory_order_release );
		_loadHandle = {};

		// 연속된 씬 전환 요청이 큐잉되어 있는 경우 이전 로드 결과를 버리고 다음 요청 즉시 디스패치
		if ( _queuedPath.empty() == false )
		{
			const string nextPath = std::move( _queuedPath );
			_queuedPath.clear();
			if ( pendingScene != nullptr && StringUtil::toLower( pendingScene->getSourcePath().c_str() ) == StringUtil::toLower( nextPath.c_str() ) )
			{
				// 이미 동일한 경로가 로드 완료됨 (대소문자 무관)
			}
			else
			{
				SW_LOG_INFO( "[SceneManager] Discarding completed load in favor of queued '%#'", nextPath );
				if ( pendingScene != nullptr )
				{
					pendingScene->shutdown();
					pendingScene.reset();
				}
				requestLoadAsync( nextPath );
				return;
			}
		}

		if ( pendingScene == nullptr )
		{
			SW_LOG_ERROR( "[SceneManager] Async load failed" );
			return;
		}

		if ( _pRHIDevice != nullptr )
		{
			_pRHIDevice->waitIdle();
			pendingScene->initialize( _pRHIDevice );
		}
		if ( _pFrameRenderer != nullptr )
			pendingScene->setFrameRenderer( _pFrameRenderer );

		Scene* const previousActive = _pActiveScene;
		_pActiveScene				= pendingScene.get();
		_listLoadedScenes.push_back( std::move( pendingScene ) );

		SW_LOG_INFO( "[SceneManager] Active scene swapped to '%#'", _pActiveScene->getName() );
		engine::getCommandStack().clear();

		// 이전 활성 씬 자동 언로드
		if ( previousActive != nullptr && previousActive != _pActiveScene )
			unloadScene( previousActive );

		// 씬 스왑 직후 더 이상 참조되지 않는 이전 씬의 에셋들을 정리합니다.
		if ( engine::areEngineServicesBound() )
			engine::getResourceManager().garbageCollectUnusedAssets();
	}

	/**
	 * @brief 활성 씬을 매 프레임 업데이트합니다.
	 */
	void SceneManager::tick( float32 deltaTime )
	{
		if ( _pActiveScene == nullptr )
			return;

		_pActiveScene->tick( deltaTime );
	}

	/**
	 * @brief 현재 비동기 씬 로드 또는 전환이 진행 중인지 여부를 반환합니다.
	 */
	bool SceneManager::isTransitioning() const
	{
		const bool pendingReady = _asyncLoad && _asyncLoad->_bReady.load( std::memory_order_acquire );
		return _bLoadInFlight.load( std::memory_order_acquire ) ||
			   pendingReady ||
			   _queuedPath.empty() == false;
	}

	/**
	 * @brief 지정된 씬을 메모리에서 해제하고 로드된 씬 목록에서 제거합니다.
	 */
	void SceneManager::unloadScene( Scene* pScene )
	{
		if ( pScene == nullptr )
			return;

		if ( _pActiveScene == pScene )
			_pActiveScene = nullptr;

		pScene->shutdown();

		_listLoadedScenes.erase(
			std::remove_if( _listLoadedScenes.begin(), _listLoadedScenes.end(),
							[pScene]( const unique_ptr<Scene>& owned )
		{ return owned.get() == pScene; } ),
			_listLoadedScenes.end() );
	}
} // namespace sw
