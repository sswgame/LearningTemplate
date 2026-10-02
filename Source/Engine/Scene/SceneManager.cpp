#include "pch.h"

#include "Engine/Scene/SceneManager.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/CpuTimer.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/ResourceManager.h"
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
        // 예전에는 씬을 다 지운 뒤에 비웠는데, 그때는 포인터만 지우는 일이라 순서가 드러나지 않았다.
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
            engine::getResourceManager().garbageCollectUnusedAssets();

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

        bool expected{ false };
        if ( _bLoadInFlight.compare_exchange_strong( expected, true ) == false )
        {
            // 대기열은 **한 자리**다. 앞에 있던 요청은 여기서 밀려나므로 그 요청자에게
            // 실패를 알린다. 예전에는 `_queuedPath` 만 덮어써서, 밀려난 쪽이 쥔 future 는
            // 아무도 채우지 않은 채로 남았다(영원히 끝나지 않는다).
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
        _bLoadInFlight.store( true, std::memory_order_release );
        _asyncLoad->_bReady.store( false, std::memory_order_release );
        _asyncLoad->_promise           = std::move( promise );
        _asyncLoad->_factoryHeadSerial = GameObjectManager::getFactoryHeadSerial();
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
        // 잡히지 않는다. 여기 계측이 하나도 없어서 씬 로드가 얼마나 걸리는지 아무도 알 수 없었고,
        // 실제로 로드 시간의 81% 를 먹는 결함이 그동안 보이지 않았다(`ComponentDefaults` 가 없는
        // 파일을 컴포넌트마다 다시 열고 있었다).
        //
        // 재는 것은 `ScopeCpuTimer` 가 한다. 스코프 동안 재고 소멸할 때 남긴다. 로그가
        // 사라지는 빌드에서는 경과 계산도 함께 사라지므로 따로 가려 줄 것이 없다.
        SceneDocument doc{};
        bool          ok = false;
        {
            ScopeCpuTimer parseTimer{ "Scene.load.parse" };
            ok = doc.load( pathStr );
        }

        sw::unique_ptr<Scene> newScene;
        if ( ok )
        {
            ScopeCpuTimer instantiateTimer{ "Scene.load.instantiate" };
            newScene = sw::make_unique<Scene>( doc._name.empty() ? "LoadedScene" : doc._name );
            newScene->setSourcePath( pathStr );
            newScene->instantiate( doc );
        }

        SW_LOG_INFO( "[SceneLoad] '%#' 엔티티 %#개", pathStr, static_cast<uint32>( doc._listEntityNode.size() ) );

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

        string outPath( path );
        if ( outPath.empty() )
            outPath = pScene->getSourcePath();
        if ( outPath.empty() )
            outPath = "Assets/Scenes/DefaultScene.scene";

        SceneDocument doc{};
        pScene->serializeToDocument( doc );
        doc._sourcePath = outPath;

        if ( doc.saveXml( outPath ) == false )
            return false;

        pScene->setSourcePath( outPath );
        return true;
    }

    /**
     * @brief 백그라운드에서 끝난 비동기 로드 결과를 메인 스레드의 안전한 시점에 활성 씬으로 바꿔 넣습니다.
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

        // 짓는 동안 모듈 팩토리가 바뀌었으면(시작할 때 키트 · SWGame 이 올라오는 중에 에디터가 시작 씬을 열었다) 그 씬은 **워커가 만들 때의**
        // 팩토리로 지어져, 새 모듈의 컴포넌트가 조용히 빠졌다(만들 수 없는 컴포넌트는 경고 없이 건너뛴다). 그대로 쓰면 저장할 때 사라진다.
        // 버리고 같은 경로를 다시 띄운다. 대기열이 **다른** 경로면 어차피 이 결과를 버리므로 아래에 맡긴다. 대기열이 **같은** 경로면 아래는
        // 이 결과를 그대로 쓴다 — 예전에는 그 경우를 빠뜨려 낡은 팩토리로 지은 씬이 활성이 됐다. 대기열은 그대로 두어, 다시 지은 결과가 두
        // 요청자를 함께 채운다.
        const bool bQueuedSamePath = _queuedPath.empty() == false && pendingScene != nullptr &&
                                     FileUtil::pathsEqualNormalized( pendingScene->getSourcePath(), _queuedPath );
        if ( pendingScene != nullptr && ( _queuedPath.empty() || bQueuedSamePath ) &&
             _asyncLoad->_factoryHeadSerial != GameObjectManager::getFactoryHeadSerial() )
        {
            const string path = pendingScene->getSourcePath();
            SW_LOG_INFO( "Module factories changed while '%#' was loading — loading it again", path );
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
                // 대기열 요청자의 약속을 **그대로 들고 간다.** 예전에는 `requestLoadFuture` 를
                // 다시 불러 약속을 새로 만들었고, 그래서 대기열에 넣은 쪽이 쥔 future 는 바로
                // 위에서 nullptr 로 닫힌 것이었다. 자기 씬이 활성이 되는데도 실패를 받았다.
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
            engine::getResourceManager().garbageCollectUnusedAssets();
    }

    /**
     * @brief 활성 씬을 매 프레임 갱신합니다.
     */
    void SceneManager::tick( float32 deltaTime )
    {
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
            _listPersistentObjectId.push_back( pRoot->getObjectId() );
    }

    bool SceneManager::isPersistent( const GameObject* pObject ) const
    {
        return pObject != nullptr &&
               std::find( _listPersistentObjectId.begin(), _listPersistentObjectId.end(), pObject->getObjectId() ) != _listPersistentObjectId.end();
    }

    void SceneManager::carryPersistentObjects( Scene* pFrom, Scene* pTo )
    {
        GameObjectManager* pSource = ( pFrom != nullptr ) ? pFrom->getObjectManager() : nullptr;
        GameObjectManager* pTarget = ( pTo != nullptr ) ? pTo->getObjectManager() : nullptr;
        if ( pSource == nullptr || pTarget == nullptr || _listPersistentObjectId.empty() )
            return;

        struct CarriedObject
        {
            GameObject*    _pSource{ nullptr };
            ObjectIdentity _identity{};
            vector<uint8>  _bytes{};
            GameObject*    _pTarget{ nullptr };
        };
        struct CrossAttachment
        {
            uint64 _childComponentId{ 0 };
            uint64 _parentObjectId{ 0 };
            uint64 _parentComponentId{ 0 };
        };

        vector<uint64> listKept;
        for ( const uint64 rootId : _listPersistentObjectId )
        {
            GameObject* pRoot = pSource->findGameObjectById( rootId );
            if ( pRoot == nullptr )
                continue; // 그새 파괴됐다

            // 루트와 자손(부모 먼저). 부착은 다른 오브젝트 쪽만 적어 둔다 — 오브젝트 안의 부착은 상태 로드가 되붙인다.
            vector<CarriedObject>   listCarried;
            vector<CrossAttachment> listCross;
            vector<GameObject*>     listChild;
            listCarried.push_back( CarriedObject{ pRoot } );
            for ( size_t cursor = 0; cursor < listCarried.size(); ++cursor )
            {
                GameObject* pObject = listCarried[cursor]._pSource;
                pObject->getChildren( listChild );
                for ( GameObject* pChild : listChild )
                    listCarried.push_back( CarriedObject{ pChild } );
                for ( const Component* pComp : pObject->getComponents() )
                {
                    const SceneComponent* pScene  = ( pComp != nullptr && pComp->isSceneComponent() ) ? static_cast<const SceneComponent*>( pComp ) : nullptr;
                    const SceneComponent* pParent = ( pScene != nullptr ) ? pScene->getParent() : nullptr;
                    if ( pParent != nullptr && pParent->getOwner() != pObject )
                        listCross.push_back( CrossAttachment{ pScene->getComponentId(), pParent->getOwner()->getObjectId(), pParent->getComponentId() } );
                }
            }

            // 상태를 모두 찍은 뒤에 만든다 — 만드는 동안 원본은 그대로다.
            for ( CarriedObject& carried : listCarried )
            {
                carried._identity = ObjectStateSerializer::captureIdentity( carried._pSource );
                ObjectStateSerializer::saveToBinaryBuffer( carried._pSource, carried._bytes );
            }
            for ( CarriedObject& carried : listCarried )
            {
                carried._pTarget = pTarget->createGameObjectWithId( carried._pSource->getName(), carried._identity._objectId );
                string parentName;
                if ( carried._pTarget == nullptr ||
                     ObjectStateSerializer::loadFromBinaryBuffer( carried._pTarget, carried._bytes.data(), carried._bytes.size(), parentName,
                                                                  &carried._identity ) == 0 )
                    SW_LOG_WARNING( "Persistent object '%#' could not be carried into '%#'", carried._pSource->getName().c_str(), pTo->getName() );
            }
            // 다른 오브젝트로의 부착은 **컴포넌트 id** 로 되붙인다(이름으로 찾지 않는다 — 새 씬에 같은 이름이 있으면 이름이 바뀐다). 소켓도 그대로다.
            for ( const CrossAttachment& attachment : listCross )
            {
                GameObject*     pParentObject = pTarget->findGameObjectById( attachment._parentObjectId );
                Component*      pParentComp   = ( pParentObject != nullptr ) ? pParentObject->findComponentById( attachment._parentComponentId ) : nullptr;
                SceneComponent* pParent       = ( pParentComp != nullptr && pParentComp->isSceneComponent() ) ? static_cast<SceneComponent*>( pParentComp ) : nullptr;
                SceneComponent* pChild        = nullptr;
                for ( const CarriedObject& carried : listCarried )
                {
                    Component* pFound = ( carried._pTarget != nullptr ) ? carried._pTarget->findComponentById( attachment._childComponentId ) : nullptr;
                    if ( pFound != nullptr )
                    {
                        pChild = pFound->isSceneComponent() ? static_cast<SceneComponent*>( pFound ) : nullptr;
                        break;
                    }
                }
                if ( pChild != nullptr && pParent != nullptr )
                    pChild->attachToComponent( pParent );
            }
            if ( listCarried.front()._pTarget != nullptr )
                listKept.push_back( rootId );
        }
        _listPersistentObjectId = std::move( listKept );
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
            _listPersistentObjectId.clear();
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
