#include "pch.h"

#include "Engine/Scene/SceneManager.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/GameTimer.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Utility/CommandStack.h"

namespace sw
{
    SW_LOG_CALLER( "SceneManager" );

    SceneManager::SceneManager()
        : _listLoadedScene{}
        , _pActiveScene{ nullptr }
        , _bWorldPlaying{ false }
        , _sceneGeneration{ 0 }
        , _pRHIDevice{ nullptr }
        , _asyncLoad{ sw::make_shared<AsyncLoadSlot>() }
        , _queuedPath{}
        , _queuedPromise{}
        , _bLoadInFlight{ false }
        , _loadHandle{}
        , _saveBlockReason{}
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

        SW_LOG_INFO( "Initialized." );
        return true;
    }

    /**
     * @brief 진행 중인 비동기 로드가 끝나기를 기다린 뒤 로드된 씬을 모두 해제합니다.
     */
    void SceneManager::shutdown()
    {
        if ( _bInitialized == false )
            return;

        _bInitialized = false;
        if ( _queuedPath.empty() == false )
        {
            _queuedPath.clear();
            _queuedPromise.setValue( nullptr );
        }
        if ( _asyncLoad != nullptr )
            _asyncLoad->_bAccepting.store( false, std::memory_order_release );

        const bool bHasAsyncLoad = ( _asyncLoad != nullptr );
        const bool bAsyncReady   = bHasAsyncLoad && ( _asyncLoad->_bReady.load( std::memory_order_acquire ) );
        const bool bLoading      = ( _bLoadInFlight.load( std::memory_order_acquire ) );
        const bool bNeedWait     = engine::areEngineServicesBound() && ( bLoading || bAsyncReady );

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
        // 활성을 **씬을 내리기 전에** 비운다 — 플레이 중이면 여기서 활성 씬의 플레이가 끝난다(onEndPlay 가 살아 있는 씬을 본다).
        activateScene( nullptr );
        _bWorldPlaying = false;
        for ( auto& scene : _listLoadedScene )
        {
            if ( scene == nullptr )
                continue;
            scene->shutdown();
        }
        _listLoadedScene.clear();
#if !defined( SW_SHIPPING )
        if ( engine::areEngineServicesBound() )
            engine::getCommandStack().clear();
#endif
        SW_LOG_INFO( "Shut down." );
    }

    /**
     * @brief 새 빈 씬을 만들어 등록하고, 활성 씬이 없으면 활성 씬으로 정합니다.
     */
    Scene* SceneManager::createScene( string_view name )
    {
        SW_MEMORY_SCOPE( Scene );
        unique_ptr<Scene> scene  = sw::make_unique<Scene>( name );
        Scene*            pScene = scene.get();

        _listLoadedScene.push_back( std::move( scene ) );

        if ( _pActiveScene == nullptr )
        {
            activateScene( pScene );
            ++_sceneGeneration;
        }

        return pScene;
    }

    Scene* SceneManager::createEmptyActiveScene( string_view name )
    {
        SW_MEMORY_SCOPE( Scene );
        unique_ptr<Scene> scene     = sw::make_unique<Scene>( name );
        Scene*            pScene    = scene.get();
        Scene*            pPrevious = _pActiveScene;

        _listLoadedScene.push_back( std::move( scene ) );
        activateScene( pScene );
        ++_sceneGeneration;

        if ( _pRHIDevice != nullptr )
            pScene->initialize( _pRHIDevice );

#if !defined( SW_SHIPPING )
        if ( engine::areEngineServicesBound() )
            engine::getCommandStack().clear();
#endif

        if ( pPrevious != nullptr && pPrevious != pScene )
            unloadScene( pPrevious );

        if ( engine::areEngineServicesBound() )
            engine::getAssetManager().garbageCollectUnusedAssets();

        return pScene;
    }

    /**
     * @brief 씬 파일을 워커 스레드에서 비동기로 로드하도록 요청하고 TaskFuture<Scene*> 를 반환합니다.
     */
    TaskFuture<Scene*> SceneManager::requestLoadFuture( string_view path )
    {
        if ( path.empty() )
        {
            SW_LOG_WARNING( "requestLoadFuture: empty path" );
            return {};
        }
        if ( _asyncLoad == nullptr || _asyncLoad->_bAccepting.load( std::memory_order_acquire ) == false )
        {
            SW_LOG_WARNING( "requestLoadFuture: manager is shutting down" );
            return {};
        }
        // 씬은 모든 타입 공급자가 등록을 끝낸 뒤(기동 단계 `ModuleTypes`)에만 읽는다. 그 전에 지으면 아직 오르지 않은 모듈의 컴포넌트가
        // `MissingComponent` 로 지어지고, 그 씬을 저장하면 그대로 남는다.
        if ( engine::getTypeRegistry().areAllModuleTypesRegistered() == false )
        {
            SW_LOG_ERROR( "Scene '%#' was requested before every module registered its types - refused (load it after the ModuleTypes startup step)", path );
            return {};
        }

        bool expected{ false };
        if ( _bLoadInFlight.compare_exchange_strong( expected, true ) == false )
        {
            // 대기열은 **한 자리**다. 앞에 있던 요청은 여기서 밀려나므로 그 요청자에게
            // 실패를 알린다. `_queuedPath` 만 덮어쓰면 밀려난 쪽이 쥔 future 는
            // 아무도 채우지 않은 채로 남는다(영원히 끝나지 않는다).
            if ( _queuedPath.empty() == false )
                _queuedPromise.setValue( nullptr );

            _queuedPath    = path;
            _queuedPromise = TaskPromise<Scene*>{};
            SW_LOG_TRACE( "Async load in flight — queued '%#'", path );
            return _queuedPromise.getFuture();
        }

        // `TaskPromise` 는 공유 상태를 가리키는 핸들이라 복사해도 같은 약속이다. 여기서
        // 복사해 넘기고 원본으로 future 를 뽑는다(양쪽 return 이 prvalue 라야 복사가 안 생긴다).
        TaskPromise<Scene*> promise{};
        if ( dispatchLoad( path, promise ) == false )
            return {};
        return promise.getFuture();
    }

    bool SceneManager::dispatchLoad( string_view path, TaskPromise<Scene*> promise )
    {
        SW_MEMORY_SCOPE( Scene );
        _bLoadInFlight.store( true, std::memory_order_release );
        _asyncLoad->_bReady.store( false, std::memory_order_release );
        _asyncLoad->_promise             = std::move( promise );
        _asyncLoad->_typeTableGeneration = engine::getTypeRegistry().getGeneration();
        SW_LOG_TRACE( "dispatchLoad: %#", path );

        shared_ptr<AsyncLoadSlot> slot = _asyncLoad;
        _loadHandle                    = engine::getTaskManager().emplaceTask(
            "SceneLoadAsync",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, SceneManager::loadSceneAsyncJob ),
            MakeTaskArgs( slot, string( path ) ) );

        _loadHandle.submit();
        if ( _loadHandle.isValid() == false )
        {
            _bLoadInFlight.store( false, std::memory_order_release );
            _asyncLoad->_promise.setValue( nullptr );
            return false;
        }
        return true;
    }

    bool SceneManager::requestLoadAsync( string_view path )
    {
        return requestLoadFuture( path ).isValid();
    }

    void SceneManager::loadSceneAsyncJob( const TaskArgs& args )
    {
        shared_ptr<AsyncLoadSlot> slot    = args.get<shared_ptr<AsyncLoadSlot>>( 0 );
        const string              pathStr = args.get<string>( 1 );
        if ( slot == nullptr || slot->_bAccepting.load( std::memory_order_acquire ) == false )
            return;

        // **로드는 프레임 밖에서 한 번 일어난다.** 그래서 `SW_PROFILE_SCOPE` 의 프레임 집계에는
        // 잡히지 않으므로 여기서 따로 잰다.
        //
        // 재는 것은 `ScopedTimer` 가 한다. 스코프 동안 재고 소멸할 때 남긴다. 로그가
        // 사라지는 빌드에서는 경과 계산도 함께 사라지므로 따로 가려 줄 것이 없다.
        SceneDocument doc{};
        bool          ok = false;
        {
            ScopedTimer parseTimer{ "Scene.load.parse" };
            ok = doc.load( pathStr );
        }

        sw::unique_ptr<Scene> newScene;
        if ( ok )
        {
            ScopedTimer instantiateTimer{ "Scene.load.instantiate" };
            newScene = sw::make_unique<Scene>( doc._name.empty() ? "LoadedScene" : doc._name );
            newScene->setSourcePath( pathStr );
            if ( newScene->instantiate( doc ) == false )
                SW_LOG_WARNING( "[SceneLoad] '%#' could not be instantiated", pathStr );
        }

        SW_LOG_INFO( "[SceneLoad] '%#' 엔티티 %#개", pathStr, static_cast<uint32>( doc._listSceneObjectNode.size() ) );

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
            SW_LOG_TRACE( "Async load task completed for '%#'", pathStr );
        else
            SW_LOG_ERROR( "Async load task failed for '%#'", pathStr );
    }

    void SceneManager::cancelPendingAsyncLoads()
    {
        if ( _asyncLoad != nullptr )
            _asyncLoad->_bAccepting.store( false, std::memory_order_release );

        if ( _loadHandle.isValid() )
            engine::getTaskManager().waitAll();

        _bLoadInFlight.store( false, std::memory_order_release );
        if ( _queuedPath.empty() == false )
        {
            _queuedPath.clear();
            _queuedPromise.setValue( nullptr );
        }

        if ( _asyncLoad != nullptr )
        {
            std::scoped_lock<mutex> lock{ _asyncLoad->_mutex };
            _asyncLoad->_scene.reset();
            _asyncLoad->_bReady.store( false, std::memory_order_release );
            _asyncLoad->_bAccepting.store( true, std::memory_order_release );
            _asyncLoad->_promise.setValue( nullptr );
        }
    }

    /**
     * @brief 활성 씬의 루트 오브젝트 상태를 XML 씬 파일로 저장합니다.
     */
    bool SceneManager::saveActiveScene( string_view path )
    {
        if ( _saveBlockReason.empty() == false )
        {
            SW_LOG_ERROR( "Scene save refused — %#", _saveBlockReason );
            return false;
        }
        Scene* pScene = getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            SW_LOG_WARNING( "saveActiveScene: no active scene" );
            return false;
        }

        // 경로가 없으면 씬이 온 곳에 쓴다. 둘 다 없으면 쓰지 않는다(경로를 지어내지 않는다). 쓰는 이름은 쿠커가 쿠킹하는 씬 이름이다
        // (`AssetCookPath::toSourcePath`).
        const string_view requestedPath = path.empty() ? string_view( pScene->getSourcePath() ) : path;
        if ( requestedPath.empty() )
        {
            SW_LOG_ERROR( "saveActiveScene: no path was given and the scene has no source path - nothing is saved" );
            return false;
        }
        const string outPath = AssetCookPath::toSourcePath( requestedPath, AssetKind::Scene );
        if ( outPath != requestedPath )
            SW_LOG_WARNING( "Scene path '%#' is not a name the cooker cooks - saving as '%#'", requestedPath, outPath.c_str() );

        SceneDocument doc{};
        if ( pScene->serializeToDocument( doc ) == false )
            return false;
        doc._sourcePath = outPath;

        if ( doc.saveXML( outPath ) == false )
            return false;

        pScene->setSourcePath( outPath );
        return true;
    }

    /**
     * @brief 백그라운드에서 끝난 비동기 로드 결과를 메인 스레드의 안전한 시점에 활성 씬으로 바꿔 넣습니다.
     */
    void SceneManager::tickTransitions()
    {
        SW_MEMORY_SCOPE( Scene );
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

        // 짓는 동안 타입 표가 바뀌었으면(핫 리로드가 키트 · SWGame 을 내렸다 다시 올렸다) 그 씬은 **워커가 지을 때의**
        // 표로 지어져, 아직 오르지 않은 모듈의 컴포넌트가 빠진다(못 만든 컴포넌트는 `MissingComponent` 로 남는다). 버리고 같은 경로를 다시
        // 띄운다. 대기열이 **다른** 경로면 어차피 이 결과를 버리므로 아래에 맡긴다. 대기열이 **같은** 경로면 아래는 이 결과를 그대로 쓰므로
        // 여기서 다시 짓는다. 대기열은 그대로 두어, 다시 지은 결과가 두 요청자를 함께 채운다.
        const bool bQueuedSamePath = _queuedPath.empty() == false && pendingScene != nullptr &&
                                     FileUtil::pathsEqualNormalized( pendingScene->getSourcePath(), _queuedPath );
        if ( pendingScene != nullptr && ( _queuedPath.empty() || bQueuedSamePath ) &&
             _asyncLoad->_typeTableGeneration != engine::getTypeRegistry().getGeneration() )
        {
            const string path = pendingScene->getSourcePath();
            SW_LOG_INFO( "Reflected types changed while '%#' was loading — loading it again", path );
            pendingScene->shutdown();
            pendingScene.reset();
            dispatchLoad( path, std::move( _asyncLoad->_promise ) );
            return;
        }

        // 씬 전환 요청이 대기열에 있으면 이번 로드 결과를 버리고 다음 요청을 바로 띄운다
        bool bQueuedSatisfiedByThisLoad{ false };
        if ( _queuedPath.empty() == false )
        {
            const string nextPath = std::move( _queuedPath );
            _queuedPath.clear();
            if ( pendingScene != nullptr && FileUtil::pathsEqualNormalized( pendingScene->getSourcePath(), nextPath ) )
            {
                // 같은 경로가 이미 로드됐다(대소문자 무관). 아래 스왑에서 대기열 요청자도 같이 채운다.
                bQueuedSatisfiedByThisLoad = true;
            }
            else
            {
                SW_LOG_TRACE( "Discarding completed load in favor of queued '%#'", nextPath );
                if ( _asyncLoad != nullptr )
                    _asyncLoad->_promise.setValue( nullptr );
                if ( pendingScene != nullptr )
                {
                    pendingScene->shutdown();
                    pendingScene.reset();
                }
                // 대기열 요청자의 약속을 **그대로 들고 간다.** `requestLoadFuture` 를 다시 불러 약속을 새로 만들면
                // 대기열에 넣은 쪽이 쥔 future 는 바로 위에서 nullptr 로 닫힌 것이 되어, 자기 씬이 활성이 되는데도 실패를 받는다.
                dispatchLoad( nextPath, std::move( _queuedPromise ) );
                return;
            }
        }

        if ( pendingScene == nullptr )
        {
            SW_LOG_ERROR( "Async load failed" );
            if ( _asyncLoad != nullptr )
                _asyncLoad->_promise.setValue( nullptr );
            if ( bQueuedSatisfiedByThisLoad )
                _queuedPromise.setValue( nullptr );
            return;
        }

        if ( _pRHIDevice != nullptr )
        {
            _pRHIDevice->waitIdle();
            pendingScene->initialize( _pRHIDevice );
        }

        Scene* const pPreviousActive = _pActiveScene;
        activateScene( pendingScene.get() );
        _listLoadedScene.push_back( std::move( pendingScene ) );
        ++_sceneGeneration;

        SW_LOG_INFO( "Active scene swapped to '%#'", _pActiveScene->getName() );
        if ( _asyncLoad != nullptr )
            _asyncLoad->_promise.setValue( _pActiveScene );
        // 대기열이 같은 경로를 가리키고 있었으면 그 요청자도 이 씬이 답이다.
        if ( bQueuedSatisfiedByThisLoad )
            _queuedPromise.setValue( _pActiveScene );

#if !defined( SW_SHIPPING )
        engine::getCommandStack().clear();
#endif

        // 이전 활성 씬을 언로드한다
        if ( pPreviousActive != nullptr && pPreviousActive != _pActiveScene )
            unloadScene( pPreviousActive );

        // 씬을 바꾼 직후, 더 이상 참조되지 않는 이전 씬의 에셋을 정리한다.
        if ( engine::areEngineServicesBound() )
            engine::getAssetManager().garbageCollectUnusedAssets();
    }

    /**
     * @brief 활성 씬을 매 프레임 갱신합니다.
     */
    void SceneManager::tick( float32 deltaTime )
    {
        SW_MEMORY_SCOPE( Scene );
        if ( _pActiveScene == nullptr )
            return;

        _pActiveScene->tick( deltaTime );
    }

    /**
     * @brief 비동기 씬 로드나 전환이 진행 중인지 반환합니다.
     */
    bool SceneManager::isTransitioning() const
    {
        const bool pendingReady = _asyncLoad && _asyncLoad->_bReady.load( std::memory_order_acquire );
        return _bLoadInFlight.load( std::memory_order_acquire ) ||
               pendingReady ||
               _queuedPath.empty() == false;
    }

    void SceneManager::markPersistent( GameObject* pRoot )
    {
        if ( pRoot == nullptr )
            return;
        // 편집 중에는 받지 않는다(유니티도 플레이 모드에서만). 받아 두면 편집 중 씬을 바꿀 때 옮겨 간다.
        if ( _bWorldPlaying == false )
        {
            SW_LOG_WARNING( "markPersistent: '%#' ignored - objects carry across scene loads only while the world is playing", pRoot->getName().c_str() );
            return;
        }
        if ( pRoot->getParent() != nullptr )
        {
            SW_LOG_WARNING( "markPersistent: '%#' is not a root object - only roots (with their children) carry across scene loads",
                            pRoot->getName().c_str() );
            return;
        }
        if ( isPersistent( pRoot ) == false )
            _listPersistentObjectID.push_back( pRoot->getObjectID() );
    }

    bool SceneManager::isPersistent( const GameObject* pObject ) const
    {
        return pObject != nullptr &&
               std::find( _listPersistentObjectID.begin(), _listPersistentObjectID.end(), pObject->getObjectID() ) != _listPersistentObjectID.end();
    }

    void SceneManager::carryPersistentObjects( Scene* pFrom, Scene* pTo )
    {
        GameObjectManager* pSource = ( pFrom != nullptr ) ? pFrom->getObjectManager() : nullptr;
        GameObjectManager* pTarget = ( pTo != nullptr ) ? pTo->getObjectManager() : nullptr;
        if ( pSource == nullptr || pTarget == nullptr || _listPersistentObjectID.empty() )
            return;

        struct CarriedObject
        {
            GameObject*    _pSource{ nullptr };
            ObjectIdentity _identity{};
            vector<uint8>  _bytes{};
            GameObject*    _pTarget{ nullptr };
        };

        vector<uint64> listKept;
        for ( const uint64 rootID : _listPersistentObjectID )
        {
            GameObject* pRoot = pSource->findGameObjectByID( rootID );
            if ( pRoot == nullptr )
                continue; // 그새 파괴됐다

            // 루트와 자손(부모 먼저).
            vector<CarriedObject> listCarried;
            vector<GameObject*>   listChild;
            listCarried.push_back( CarriedObject{ pRoot } );
            for ( size_t cursor = 0; cursor < listCarried.size(); ++cursor )
            {
                listCarried[cursor]._pSource->getChildren( listChild );
                for ( GameObject* pChild : listChild )
                {
                    listCarried.push_back( CarriedObject{ pChild } );
                }
            }

            // 상태를 모두 찍은 뒤에 만든다 — 만드는 동안 원본은 그대로다.
            for ( CarriedObject& carried : listCarried )
            {
                carried._identity = ObjectStateSerializer::captureIdentity( carried._pSource );
                if ( ObjectStateSerializer::saveToBinaryBuffer( carried._pSource, carried._bytes ) == false )
                    carried._bytes.clear(); // 아래 로드가 실패로 알린다
            }
            // 옮겨 심은 것끼리의 부착(부모 · 소켓 · 오브젝트 안)은 묶음이 **원래 id** 로 잇는다. 이름으로 찾지 않는다 — 들어오는 씬에 같은
            // 이름이 있으면 옮긴 오브젝트의 이름이 바뀌어(`MusicPlayer_2`), 이름으로 찾으면 들어오는 씬의 것에 붙는다.
            ObjectStateBatch batch( ObjectIDSpace::Live );
            for ( CarriedObject& carried : listCarried )
            {
                carried._pTarget = pTarget->createGameObjectWithID( carried._pSource->getName(), carried._identity._objectID );
                ObjectLoadContext context{};
                context._pIdentity = &carried._identity;
                context._pBatch    = &batch;
                if ( carried._pTarget == nullptr ||
                     ObjectStateSerializer::loadFromBinaryBuffer( carried._pTarget, carried._bytes.data(), carried._bytes.size(), context ) == 0 )
                    SW_LOG_WARNING( "Persistent object '%#' could not be carried into '%#'", carried._pSource->getName().c_str(), pTo->getName() );
            }
            batch.finish();
            if ( listCarried.front()._pTarget != nullptr )
                listKept.push_back( rootID );
        }
        _listPersistentObjectID = std::move( listKept );
    }

    void SceneManager::activateScene( Scene* pScene )
    {
        if ( _pActiveScene == pScene )
            return;
        // 플레이 중이면 영속 루트를 들어오는 씬으로 옮겨 심는다 — 나가는 씬이 끝나기(endPlay) 전에, 상태가 살아 있을 때.
        if ( _bWorldPlaying && _pActiveScene != nullptr && pScene != nullptr )
            carryPersistentObjects( _pActiveScene, pScene );
        // 플레이 중이면 나가는 씬을 끝내고 들어오는 씬을 시작한다(씬을 내리기 전 — 언로드는 활성을 먼저 비운다).
        if ( _bWorldPlaying && _pActiveScene != nullptr && _pActiveScene->getObjectManager() != nullptr )
            _pActiveScene->getObjectManager()->endPlay();
        _pActiveScene = pScene;
        if ( _bWorldPlaying && pScene != nullptr && pScene->getObjectManager() != nullptr )
            pScene->getObjectManager()->beginPlay();
    }

    void SceneManager::setWorldPlaying( bool bPlaying )
    {
        if ( _bWorldPlaying == bPlaying )
            return;
        _bWorldPlaying = bPlaying;
        // 플레이를 멈추면 영속 표시를 잊는다(유니티는 플레이 모드를 나가면 영속 씬을 비운다). 편집 중 씬을 바꿀 때 옮겨 가면 안 된다.
        if ( bPlaying == false )
            _listPersistentObjectID.clear();
        GameObjectManager* pObjects = ( _pActiveScene != nullptr ) ? _pActiveScene->getObjectManager() : nullptr;
        if ( pObjects == nullptr )
            return;
        if ( bPlaying )
            pObjects->beginPlay();
        else
            pObjects->endPlay();
    }

    /**
     * @brief 지정한 씬을 해제하고 로드된 씬 목록에서 뺍니다.
     */
    void SceneManager::unloadScene( Scene* pScene )
    {
        if ( pScene == nullptr )
            return;

        if ( _pActiveScene == pScene )
            activateScene( nullptr );

        pScene->shutdown();

        _listLoadedScene.erase(
            std::remove_if( _listLoadedScene.begin(), _listLoadedScene.end(),
                            [pScene]( const unique_ptr<Scene>& owned )
        { return owned.get() == pScene; } ),
            _listLoadedScene.end() );
    }
} // namespace sw
