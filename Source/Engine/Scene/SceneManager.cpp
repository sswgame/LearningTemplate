#include "pch.h"

#include "Engine/Scene/SceneManager.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/GameTimer.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Physics/Component/ScenePhysics.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/PreloadSet.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/ScenePreloadList.h"

namespace sw
{
    SW_LOG_CALLER( "SceneManager" );

    SW_GLOBAL_VARIABLE( bool, gv_scenePreload, true, "씬 로드 요청 때 쿠킹이 구운 프리로드 목록(<씬>.preload.json)의 파일을 미리 읽는다 (끄면 씬이 에셋을 잡을 때 읽는다)" );
    SW_GLOBAL_VARIABLE( int32, gv_scenePreloadInFlight, 8, "씬 프리로드가 한 번에 걸어 두는 읽기 요청 수 (프레임에 IO 를 몰지 않는 예산)" );
    SW_GLOBAL_VARIABLE( int32, gv_scenePreloadPinMegabytes, 256, "씬 프리로드가 캐시에 붙드는 에셋 바이트 합의 상한 (MB, 넘으면 오래 안 쓴 것부터 놓는다)" );
    SW_GLOBAL_VARIABLE( int32, gv_scenePreloadHoldFrames, 120, "로드가 끝난 뒤 프리로드를 붙들고 있는 프레임 수 (씬이 에셋을 잡을 시간)" );

    /** @brief 종료 · 취소가 자기가 띄운 씬 로드를 기다리는 최대 시간입니다. 로드 잡은 파싱 · 인스턴스화만 한다 — 넘기면 멈춘 것이므로 오류로 알린다. */
    constexpr uint32 kLoadWaitMs = 30000;

    SceneManager::SceneManager()
        : _listLoadedScene{}
        , _pActiveScene{ nullptr }
        , _listPreviewScene{}
        , _pFocusedScene{ nullptr }
        , _nextPreviewKey{ 1 }
        , _bWorldPlaying{ false }
        , _sceneGeneration{ 0 }
        , _pRHIDevice{ nullptr }
        , _services{ SceneEngineServices::makeFromBoundEngine() }
        , _asyncLoad{ sw::make_shared<AsyncLoadSlot>() }
        , _queuedPath{}
        , _queuedPromise{}
        , _bLoadInFlight{ false }
        , _loadHandle{}
        , _loadTracker{}
        , _saveBlockReason{}
        , _onSceneObjectsInvalidated{}
        , _commandStackClearSubscription{}
        , _netLink{}
        , _bInitialized{ false }
        , _pScenePreload{}
        , _preloadHeldFrames{ 0 }
        , _leakProbe{}
    {
        _asyncLoad->_services = _services;
    }

    SceneManager::~SceneManager()
    {
        shutdown();
    }

    /**
     * @brief 묶인 표의 씬 쪽 묶음을 읽어 매니저를 초기화하고 비동기 로드 슬롯을 준비합니다.
     */
    SubsystemInitResult SceneManager::initializeSubsystem()
    {
        _services = SceneEngineServices::makeFromBoundEngine();
        if ( _asyncLoad == nullptr )
            _asyncLoad = sw::make_shared<AsyncLoadSlot>();
        _asyncLoad->_services = _services;
        _asyncLoad->_bReady.store( false, std::memory_order_release );
        _asyncLoad->_bAccepting.store( true, std::memory_order_release );
        {
            ScopedLock<mutex> lock{ _asyncLoad->_mutex };
            _asyncLoad->_scene.reset();
        }
        _bLoadInFlight = false;
        _queuedPath.clear();
        _bInitialized = true;

        // 되돌리기 기록은 나간 씬의 오브젝트를 가리킨다 — 씬이 바뀌면 비운다. 다시 초기화해도 구독이 겹치지 않는다.
        _commandStackClearSubscription.reset(); // 다시 초기화해도 구독이 겹치지 않는다(대입이 앞 구독을 푼다)
        CommandStack* const pCommandStack = engine::findSubsystem<CommandStack>();
        if ( pCommandStack != nullptr )
            _commandStackClearSubscription = _onSceneObjectsInvalidated.add( SW_DELEGATE_METHOD( Delegate<void()>, &CommandStack::clear, pCommandStack ) );

        SW_LOG_INFO( "Initialized." );
        return SubsystemInitResult::Succeeded;
    }

    void SceneManager::shutdownSubsystem()
    {
        shutdown(); // 종료도 알린다(기록을 비운다) — 그 뒤에 구독을 뗀다
        _commandStackClearSubscription.reset();
    }

    /**
     * @brief 진행 중인 비동기 로드가 끝나기를 기다린 뒤 로드된 씬을 모두 해제합니다.
     */
    void SceneManager::shutdown()
    {
        if ( _bInitialized == false )
            return;

        _bInitialized = false;
        _pScenePreload.reset(); // 붙든 에셋을 에셋 매니저가 살아 있는 동안 놓는다
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
        const bool bNeedWait     = _services._pTaskManager != nullptr && ( bLoading || bAsyncReady );

        if ( bNeedWait )
            (void)_loadTracker.waitIdle( kLoadWaitMs, "SceneManager::shutdown" ); // 시간 초과는 추적기가 원인 태스크 이름과 함께 오류로 남긴다

        if ( _asyncLoad != nullptr )
        {
            ScopedLock<mutex> lock{ _asyncLoad->_mutex };
            _asyncLoad->_scene.reset();
            _asyncLoad->_bReady.store( false, std::memory_order_release );
        }
        _bLoadInFlight = false;
        // 미리보기 씬이 먼저다 — 로드된 씬과 서비스를 공유하지 않지만, 내리는 순서를 만든 순서의 역으로 둔다.
        while ( _listPreviewScene.empty() == false )
        {
            (void)destroyPreviewScene( _listPreviewScene.back()._scene.get() );
        }
        _pFocusedScene = nullptr;
        // 활성을 **씬을 내리기 전에** 비운다 — 플레이 중이면 여기서 활성 씬의 플레이가 끝난다(onEndPlay 가 살아 있는 씬을 본다).
        activateScene( nullptr );
        _bWorldPlaying = false;
        // 씬 정리는 소멸자 한 곳이 한다. 자리를 먼저 비운 채 소멸시켜, 소멸 중의 조회가 지우는 중인 목록을 보지 않게 한다.
        for ( unique_ptr<Scene>& scene : _listLoadedScene )
        {
            scene.reset();
        }
        _listLoadedScene.clear();
        broadcastSceneObjectsInvalidated();
        SW_LOG_INFO( "Shut down." );
    }

    Scene* SceneManager::createScene( string_view name )
    {
        SW_MEMORY_SCOPE( Scene );
        return adoptScene( sw::make_unique<Scene>( name, _services ), SceneRole::Loaded );
    }

    Scene* SceneManager::createEmptyActiveScene( string_view name )
    {
        SW_MEMORY_SCOPE( Scene );
        return adoptScene( sw::make_unique<Scene>( name, _services ), SceneRole::Active );
    }

    Scene* SceneManager::adoptScene( unique_ptr<Scene> scene, SceneRole role )
    {
        SW_MEMORY_SCOPE( Scene );
        Scene* const pScene = scene.get();
        // 기본 머티리얼 · 기본 카메라 · 메시 기본값을 건다(디바이스가 없는 테스트는 건너뛴다). 머티리얼 없는 메시가 그려지려면 필요하다.
        if ( _pRHIDevice != nullptr && pScene->initialize( _pRHIDevice ) == false )
            SW_LOG_WARNING( "Scene '%#' could not acquire its default material", pScene->getName() );
        // 넷 링크는 플레이가 시작되기 전(활성화 · `beginPlay` 앞)에 올린다. 미리보기 씬은 만드는 쪽(가상 서버 하니스)이 자기 링크를 올린다.
        GameObjectManager* const pNewManager = pScene->getObjectManager();
        if ( role != SceneRole::Preview && _netLink.isComplete() && pNewManager != nullptr )
            (void)pNewManager->getSubsystem<SceneNetLink>().bind( _netLink );

        if ( role == SceneRole::Preview )
        {
            _listPreviewScene.push_back( PreviewSceneEntry{ _nextPreviewKey++, std::move( scene ), ScenePreviewPolicy{} } );
            return pScene;
        }

        Scene* const pPrevious = _pActiveScene;
        _listLoadedScene.push_back( std::move( scene ) );
        if ( role == SceneRole::Loaded && pPrevious != nullptr )
            return pScene;

        activateScene( pScene );
        ++_sceneGeneration;
        if ( role == SceneRole::Loaded )
            return pScene;

        broadcastSceneObjectsInvalidated();
        if ( pPrevious != nullptr )
            unloadScene( pPrevious );
        garbageCollectUnusedAssets();
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
        // 타입 표를 받지 못한 매니저도 같은 이유로 거절한다 — 무엇으로 지을지 모른다.
        if ( _services._pTypeRegistry == nullptr || _services._pTypeRegistry->areAllModuleTypesRegistered() == false )
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
        // 로드를 띄울 곳(태스크)과 지을 표(타입)가 없으면 띄우지 않는다. 기다리는 쪽에는 실패로 알린다.
        if ( _services._pTaskManager == nullptr || _services._pTypeRegistry == nullptr )
        {
            SW_LOG_ERROR( "Scene '%#' cannot be loaded - the scene manager has no task manager or type registry", path );
            _bLoadInFlight.store( false, std::memory_order_release );
            promise.setValue( nullptr );
            return false;
        }
        _bLoadInFlight.store( true, std::memory_order_release );
        _asyncLoad->_bReady.store( false, std::memory_order_release );
        _asyncLoad->_promise             = std::move( promise );
        _asyncLoad->_typeTableGeneration = _services._pTypeRegistry->getGeneration();
        SW_LOG_TRACE( "dispatchLoad: %#", path );

        shared_ptr<AsyncLoadSlot> slot = _asyncLoad;
        _loadHandle                    = _services._pTaskManager->emplaceTask(
            "SceneLoadAsync",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, SceneManager::loadSceneAsyncJob ),
            MakeTaskArgs( slot, string( path ) ) );

        _loadTracker.track( _loadHandle );
        _loadHandle.submit();
        if ( _loadHandle.isValid() == false )
        {
            _bLoadInFlight.store( false, std::memory_order_release );
            _asyncLoad->_promise.setValue( nullptr );
            return false;
        }
        beginScenePreload( path );
        return true;
    }

    void SceneManager::beginScenePreload( string_view path )
    {
        _pScenePreload.reset();
        _preloadHeldFrames = 0;
        if ( gv_scenePreload == false )
            return;
        // 목록은 쿠킹이 굽는다 — 쿠킹 전 Dev 실행은 목록이 없고, 그 씬은 예전처럼 에셋을 잡을 때 읽는다.
        const string listPath = ScenePreloadList::makeListPath( path );
        if ( listPath.empty() || ResourceIO::hasResource( ResourceID{ listPath } ) == false )
            return;
        vector<string> listAsset;
        if ( ScenePreloadList::read( ResourceID{ listPath }, listAsset ) == false || listAsset.empty() )
            return; // 읽지 못한 이유는 읽기가 남겼다. 로드는 그대로 간다
        _pScenePreload = make_unique<PreloadSet>( std::move( listAsset ), StreamingPriority::Normal, static_cast<uint32>( MathUtil::max( static_cast<int32>( gv_scenePreloadInFlight ), 1 ) ) );
        if ( _services._pAssetManager != nullptr )
            _pScenePreload->enablePinning( *_services._pAssetManager, static_cast<uint64>( MathUtil::max( static_cast<int32>( gv_scenePreloadPinMegabytes ), 0 ) ) * constant::kBytesPerMegabyte );
    }

    void SceneManager::updateScenePreload()
    {
        if ( _pScenePreload == nullptr )
            return;
        AssetStreamingQueue* pQueue = engine::findSubsystem<AssetStreamingQueue>();
        if ( pQueue != nullptr )
            _pScenePreload->update( *pQueue );
        if ( _bLoadInFlight.load( std::memory_order_acquire ) )
            return;
        ++_preloadHeldFrames;
        if ( _preloadHeldFrames >= static_cast<uint32>( MathUtil::max( static_cast<int32>( gv_scenePreloadHoldFrames ), 0 ) ) )
            _pScenePreload.reset();
    }

    bool SceneManager::tryGetLoadProgress( float32& outProgress ) const
    {
        if ( _pScenePreload == nullptr )
            return false;
        outProgress = _pScenePreload->getProgress();
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
        // 잡히지 않으므로 여기서 따로 측정한다.
        //
        // 측정하는 것은 `ScopedTimer` 가 한다. 스코프 동안 재고 소멸할 때 남긴다. 로그가
        // 사라지는 빌드에서는 경과 계산도 함께 사라지므로 따로 가려 줄 것이 없다.
        SceneDocument doc{};
        bool          ok = false;
        {
            ScopedTimer parseTimer{ "Scene.load.parse" };
            ok = doc.load( ResourceID{ pathStr } );
        }

        if ( slot->_bAccepting.load( std::memory_order_acquire ) == false )
            return; // 종료가 시작됐다 — 인스턴스화(긴 일)를 하지 않고 돌아간다

        sw::unique_ptr<Scene> newScene;
        if ( ok )
        {
            ScopedTimer instantiateTimer{ "Scene.load.instantiate" };
            newScene = sw::make_unique<Scene>( doc._name.empty() ? "LoadedScene" : doc._name, slot->_services );
            newScene->setSourcePath( pathStr );
            const bool bInstantiated = newScene->instantiate( doc, &slot->_bStop );
            if ( bInstantiated == false && slot->_bStop.load( std::memory_order_acquire ) == false ) // 종료 신호로 멈춘 것은 실패가 아니다
                SW_LOG_WARNING( "[SceneLoad] '%#' could not be instantiated", pathStr );
        }

        SW_LOG_INFO( "[SceneLoad] '%#' %# entities", pathStr, static_cast<uint32>( doc._listSceneObjectNode.size() ) );

        if ( slot->_bAccepting.load( std::memory_order_acquire ) == false )
            return; // 지은 씬(중간에 멈춘 부분 씬 포함)은 여기서 소멸하며 정리된다

        const bool bSuccess = ( newScene != nullptr );
        {
            ScopedLock<mutex> lock{ slot->_mutex };
            slot->_scene = std::move( newScene );
        }
        slot->_bReady.store( true, std::memory_order_release );
        if ( bSuccess )
            SW_LOG_TRACE( "Async load task completed for '%#'", pathStr );
        else
            SW_LOG_ERROR( "Async load task failed for '%#'", pathStr );
    }

    void SceneManager::waitForPendingLoad() const
    {
        // 로드 잡은 워커에서 파싱 · 인스턴스화만 하고 게임 스레드 일을 기다리지 않는다. 그래도 멈춤이 매달림으로 숨지 않게 시간으로 끊고 알린다.
        constexpr int64 kWaitLimitSeconds = 30;
        const Deadline  deadline          = Deadline::afterMilliseconds( kWaitLimitSeconds * constant::kMillisecondsPerSecond );
        while ( _loadHandle.isValid() && _loadHandle.isCompleted() == false )
        {
            if ( deadline.isExpired() )
            {
                SW_LOG_ERROR( "Waited %# s for the in-flight scene load before changing reflected types - continuing without it", kWaitLimitSeconds );
                return;
            }
            std::this_thread::yield();
        }
    }

    void SceneManager::requestAsyncLoadStop()
    {
        if ( _asyncLoad != nullptr )
        {
            _asyncLoad->_bStop.store( true, std::memory_order_release );
            _asyncLoad->_bAccepting.store( false, std::memory_order_release );
        }
    }

    void SceneManager::cancelPendingAsyncLoads()
    {
        if ( _asyncLoad != nullptr )
            _asyncLoad->_bAccepting.store( false, std::memory_order_release );

        // 이 매니저가 띄운 로드(`dispatchLoad`)만 기다린다. 엔진 전체를 기다리면 영구 태스크(오디오 등)가 취소를 막는다. 로드 잡은 `_bAccepting` 이 꺼진 것을
        // 보면 씬을 스스로 내리고 돌아와 약속을 건드리지 않는다 — 약속은 아래에서 한 번만 채운다(이중 해제 없음).
        if ( _services._pTaskManager != nullptr )
            (void)_loadTracker.waitIdle( kLoadWaitMs, "SceneManager::cancelPendingAsyncLoads" );

        _bLoadInFlight.store( false, std::memory_order_release );
        if ( _queuedPath.empty() == false )
        {
            _queuedPath.clear();
            _queuedPromise.setValue( nullptr );
        }

        if ( _asyncLoad != nullptr )
        {
            ScopedLock<mutex> lock{ _asyncLoad->_mutex };
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
        const string outPath = AssetCookPath::toSourcePath( requestedPath, SceneDocument::kAssetType );
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

    bool SceneManager::saveActiveSceneCopy( string_view path )
    {
        if ( _saveBlockReason.empty() == false )
        {
            SW_LOG_WARNING( "Scene copy refused — %#", _saveBlockReason );
            return false;
        }
        Scene*     pScene      = getActiveScene();
        const bool bCannotSave = pScene == nullptr || pScene->getObjectManager() == nullptr || path.empty();
        if ( bCannotSave )
            return false;
        SceneDocument doc{};
        if ( pScene->serializeToDocument( doc ) == false )
            return false;
        doc._sourcePath = string( path );
        return doc.saveXML( path );
    }

    /**
     * @brief 백그라운드에서 끝난 비동기 로드 결과를 메인 스레드의 안전한 시점에 활성 씬으로 바꿔 넣습니다.
     */
    void SceneManager::updateTransitions()
    {
        SW_MEMORY_SCOPE( Scene );
        updateScenePreload();
        if ( _asyncLoad == nullptr || _asyncLoad->_bReady.load( std::memory_order_acquire ) == false )
            return;

        unique_ptr<Scene> pendingScene;
        {
            ScopedLock<mutex> lock{ _asyncLoad->_mutex };
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
        const bool bTypeTableChanged    = _services._pTypeRegistry != nullptr && _asyncLoad->_typeTableGeneration != _services._pTypeRegistry->getGeneration();
        const bool bReloadForTypeChange = pendingScene != nullptr && ( _queuedPath.empty() || bQueuedSamePath ) && bTypeTableChanged;
        if ( bReloadForTypeChange )
        {
            const string path = pendingScene->getSourcePath();
            SW_LOG_INFO( "Reflected types changed while '%#' was loading — loading it again", path );
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
                pendingScene.reset();
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
            _pRHIDevice->waitIdle( RHIWaitIdleReason::SceneActivation ); // 이전 씬의 GPU 일이 끝난 뒤에 새 씬의 기본 머티리얼을 잡는다

        // 활성 교체 · 이전 씬 언로드 · 에셋 정리는 `adoptScene` 이 한다. 약속은 교체가 끝난 뒤에 채운다.
        Scene* const pAdopted = adoptScene( std::move( pendingScene ), SceneRole::Active );
        SW_LOG_INFO( "Active scene swapped to '%#'", pAdopted->getName() );
        if ( _asyncLoad != nullptr )
            _asyncLoad->_promise.setValue( pAdopted );
        // 대기열이 같은 경로를 가리키고 있었으면 그 요청자도 이 씬이 답이다.
        if ( bQueuedSatisfiedByThisLoad )
            _queuedPromise.setValue( pAdopted );
    }

    /**
     * @brief 활성 씬을 매 프레임 갱신합니다.
     */
    void SceneManager::update( float32 deltaTime )
    {
        SW_MEMORY_SCOPE( Scene );
        if ( _leakProbe.isRunning() )
            _leakProbe.update( *this ); // 활성 씬이 없는 프레임에도 진행한다(로드 사이)
        if ( _pActiveScene == nullptr )
            return;

        _pActiveScene->update( deltaTime );
    }

    namespace
    {
        /** @brief 정책을 씬에 적용합니다(오디오 · 물리). 틱 · 시간 배율은 `tickPreviewScenes` 가 읽는다. */
        void applyPreviewPolicy( Scene& scene, const ScenePreviewPolicy& policy )
        {
            scene.setAudioUpdateEnabled( policy._bAudio );
            if ( scene.getObjectManager() != nullptr )
            {
                scene.getObjectManager()->setPhysicsStepEnabled( policy._bPhysics );
                scene.getObjectManager()->getSubsystem<ScenePhysics>().setFixedStepOverride( policy._fixedStepOverride );
            }
        }

        /** @brief 정책의 플레이 설정에 맞춰 씬의 플레이 상태를 맞춥니다. */
        void syncPreviewPlay( Scene& scene, bool bPlay )
        {
            GameObjectManager* pObjects = scene.getObjectManager();
            if ( pObjects == nullptr || pObjects->hasBegunPlay() == bPlay )
                return;
            if ( bPlay )
                pObjects->beginPlay();
            else
                pObjects->endPlay();
        }
    } // namespace

    Scene* SceneManager::createPreviewScene( string_view name, const ScenePreviewPolicy& policy )
    {
        SW_MEMORY_SCOPE( Scene );
        Scene* const pScene = adoptScene( sw::make_unique<Scene>( name, _services ), SceneRole::Preview );
        (void)setPreviewPolicy( pScene, policy ); // 방금 넣은 미리보기 씬이라 늘 찾는다
        return pScene;
    }

    bool SceneManager::destroyPreviewScene( Scene* pScene )
    {
        const auto found = std::find_if( _listPreviewScene.begin(), _listPreviewScene.end(),
                                         [pScene]( const PreviewSceneEntry& entry )
        { return entry._scene.get() == pScene; } );
        if ( pScene == nullptr || found == _listPreviewScene.end() )
            return false;
        if ( _pFocusedScene == pScene )
            _pFocusedScene = nullptr;
        syncPreviewPlay( *pScene, false ); // 씬이 살아 있는 동안 끝낸다
        found->_scene.reset();             // 정리는 소멸자 한 곳이 한다. 목록을 옮기기 전에 소멸시킨다
        _listPreviewScene.erase( found );
        return true;
    }

    bool SceneManager::isPreviewScene( const Scene* pScene ) const
    {
        return getPreviewSceneKey( pScene ) != kInvalidSceneKey;
    }

    uint32 SceneManager::getPreviewSceneKey( const Scene* pScene ) const
    {
        if ( pScene == nullptr )
            return kInvalidSceneKey;
        for ( const PreviewSceneEntry& entry : _listPreviewScene )
        {
            if ( entry._scene.get() == pScene )
                return entry._key;
        }
        return kInvalidSceneKey;
    }

    uint32 SceneManager::getPreviewSceneKeyOf( const GameObjectManager* pManager ) const
    {
        if ( pManager == nullptr )
            return kInvalidSceneKey;
        for ( const PreviewSceneEntry& entry : _listPreviewScene )
        {
            if ( entry._scene->getObjectManager() == pManager )
                return entry._key;
        }
        return kInvalidSceneKey;
    }

    void SceneManager::collectAllScenes( vector<const Scene*>& outListScene ) const
    {
        outListScene.clear();
        for ( const unique_ptr<Scene>& scene : _listLoadedScene )
        {
            if ( scene != nullptr )
                outListScene.push_back( scene.get() );
        }
        for ( const PreviewSceneEntry& entry : _listPreviewScene )
        {
            if ( entry._scene != nullptr )
                outListScene.push_back( entry._scene.get() );
        }
    }

    Scene* SceneManager::findPreviewScene( uint32 key ) const
    {
        if ( key == kInvalidSceneKey )
            return nullptr;
        for ( const PreviewSceneEntry& entry : _listPreviewScene )
        {
            if ( entry._key == key )
                return entry._scene.get();
        }
        return nullptr;
    }

    bool SceneManager::setPreviewPolicy( Scene* pScene, const ScenePreviewPolicy& policy )
    {
        for ( PreviewSceneEntry& entry : _listPreviewScene )
        {
            if ( entry._scene.get() != pScene )
                continue;
            entry._policy = policy;
            applyPreviewPolicy( *pScene, policy );
            syncPreviewPlay( *pScene, policy._bPlay );
            return true;
        }
        return false;
    }

    const ScenePreviewPolicy* SceneManager::findPreviewPolicy( const Scene* pScene ) const
    {
        for ( const PreviewSceneEntry& entry : _listPreviewScene )
        {
            if ( entry._scene.get() == pScene )
                return &entry._policy;
        }
        return nullptr;
    }

    void SceneManager::updatePreviewScenes( float32 deltaTime, vector_reference<const uint32> listDrawnSceneKey )
    {
        if ( _listPreviewScene.empty() )
            return;
        SW_MEMORY_SCOPE( Scene );
        for ( PreviewSceneEntry& entry : _listPreviewScene )
        {
            const bool bManagerTicks = entry._policy._tick == ScenePreviewTick::WhenDrawn;
            if ( bManagerTicks == false || VectorUtil::contains( listDrawnSceneKey, entry._key ) == false )
                continue;
            entry._scene->update( deltaTime * entry._policy._timeScale );
            ++entry._tickCount;
        }
    }

    GameObjectManager* SceneManager::findActiveObjectManager() const
    {
        return _pActiveScene != nullptr ? _pActiveScene->getObjectManager() : nullptr;
    }

    GameObject* SceneManager::findActiveObject( const hashed_string& name ) const
    {
        const GameObjectManager* pManager = findActiveObjectManager();
        return pManager != nullptr ? pManager->findGameObjectByName( name ) : nullptr;
    }

    bool SceneManager::focusScene( Scene* pScene )
    {
        if ( pScene == nullptr )
            return false;
        bool bKnown = isPreviewScene( pScene );
        for ( const unique_ptr<Scene>& loaded : _listLoadedScene )
        {
            bKnown = bKnown || loaded.get() == pScene;
        }
        if ( bKnown == false )
            return false;
        _pFocusedScene = pScene;
        return true;
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
               VectorUtil::contains( _listPersistentObjectID, pObject->getObjectID() );
    }

    void SceneManager::carryPersistentObjects( Scene* pFrom, Scene* pTo )
    {
        GameObjectManager* pSource         = ( pFrom != nullptr ) ? pFrom->getObjectManager() : nullptr;
        GameObjectManager* pTarget         = ( pTo != nullptr ) ? pTo->getObjectManager() : nullptr;
        const bool         bNothingToCarry = pSource == nullptr || pTarget == nullptr || _listPersistentObjectID.empty();
        if ( bNothingToCarry )
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
                if ( ObjectStateSerializer::saveToBinaryBuffer( carried._pSource, carried._bytes, ObjectSaveOptions{ SerializePurpose::Snapshot } ) == false )
                    carried._bytes.clear(); // 아래 로드가 실패로 알린다
            }
            // 옮겨 심은 것끼리의 부착(부모 · 소켓 · 오브젝트 안)은 묶음이 **원래 id** 로 잇는다. 이름으로 찾지 않는다 — 들어오는 씬에 같은
            // 이름이 있으면 옮긴 오브젝트의 이름이 바뀌어(`MusicPlayer_2`), 이름으로 찾으면 들어오는 씬의 것에 붙는다.
            ObjectStateBatch batch( ObjectIDSpace::Live );
            for ( CarriedObject& carried : listCarried )
            {
                carried._pTarget = pTarget->createGameObjectWithID( carried._pSource->getName(), carried._identity._objectID );
                ObjectLoadContext context{ SerializePurpose::Snapshot };
                context._pIdentity = &carried._identity;
                context._pBatch    = &batch;
                if ( carried._pTarget == nullptr ||
                     ObjectStateSerializer::loadFromBinaryBuffer( carried._pTarget, carried._bytes.data(), carried._bytes.size(), context ) == 0 )
                    SW_LOG_WARNING( "Persistent object '%#' could not be carried into '%#'", carried._pSource->getName().c_str(), pTo->getName() );
            }
            // `Live` 배치는 런타임 id 가 맞아 옮기지 못한 핸들을 실패로 치지 않는다. 늘 true 다.
            [[maybe_unused]] const bool bLinked = batch.finish();
            SW_ASSERT( bLinked );
            // 중첩 프리팹 부품 표시도 같은 id 로 옮긴다 — 없으면 들어온 씬을 저장할 때 부품이 따로 엔티티로 적혀 다음 로드에서 두 벌이 된다.
            const PrefabInstanceRegistry& sourceParts = pSource->getSubsystem<PrefabInstanceRegistry>();
            PrefabInstanceRegistry&       targetParts = pTarget->getSubsystem<PrefabInstanceRegistry>();
            for ( const CarriedObject& carried : listCarried )
            {
                const PrefabNestedPart* pPart = ( carried._pTarget != nullptr ) ? sourceParts.findNestedPart( carried._identity._objectID ) : nullptr;
                if ( pPart != nullptr )
                    targetParts.registerNestedPart( carried._pTarget->getObjectID(), pPart->_ownerObjectID, pPart->_prefabPath, pPart->_partKey );
            }
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
        const bool bCarryPersistent = _bWorldPlaying && _pActiveScene != nullptr && pScene != nullptr;
        if ( bCarryPersistent )
            carryPersistentObjects( _pActiveScene, pScene );
        // 플레이 중이면 나가는 씬을 끝내고 들어오는 씬을 시작한다(씬을 내리기 전 — 언로드는 활성을 먼저 비운다).
        const bool bEndOutgoingPlay = _bWorldPlaying && _pActiveScene != nullptr && _pActiveScene->getObjectManager() != nullptr;
        if ( bEndOutgoingPlay )
            _pActiveScene->getObjectManager()->endPlay();
        _pActiveScene                 = pScene;
        const bool bBeginIncomingPlay = _bWorldPlaying && pScene != nullptr && pScene->getObjectManager() != nullptr;
        if ( bBeginIncomingPlay )
            pScene->getObjectManager()->beginPlay();
    }

    void SceneManager::setNetLink( const NetLinkValue& value )
    {
        _netLink                         = value;
        GameObjectManager* const pActive = findActiveObjectManager();
        if ( pActive == nullptr )
            return;
        SceneNetLink& link = pActive->getSubsystem<SceneNetLink>();
        if ( value.isComplete() )
            (void)link.bind( value );
        else
            link.unbind();
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
        if ( _pFocusedScene == pScene )
            _pFocusedScene = nullptr;

        // 자리를 비운 채 소멸시킨 뒤(정리는 소멸자 한 곳) 빈 자리를 지운다 — 소멸 중의 조회가 지우는 중인 목록을 보지 않게.
        for ( unique_ptr<Scene>& owned : _listLoadedScene )
        {
            if ( owned.get() == pScene )
                owned.reset();
        }
        VectorUtil::removeIf( _listLoadedScene, []( const unique_ptr<Scene>& owned )
        { return owned == nullptr; } );
    }

    void SceneManager::broadcastSceneObjectsInvalidated()
    {
        _onSceneObjectsInvalidated.broadcast();
    }

    void SceneManager::garbageCollectUnusedAssets()
    {
        if ( _services._pAssetManager != nullptr )
            _services._pAssetManager->garbageCollectUnusedAssets();
    }
} // namespace sw

namespace sw
{
    span<const utf8* const> SceneManager::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { IAudioService::kSubsystemName, AssetStreamingQueue::kSubsystemName, AsyncFileIO::kSubsystemName, InputManager::kSubsystemName,
                                                          PhysicsSystem::kSubsystemName, AssetManager::kSubsystemName, TaskManager::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

namespace sw
{
    namespace engine
    {
        GameObjectManager* findActiveObjectManager()
        {
            const SceneManager* pSceneManager = findSubsystem<SceneManager>();
            return pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
        }

        GameObject* findActiveObject( const hashed_string& name )
        {
            const SceneManager* pSceneManager = findSubsystem<SceneManager>();
            return pSceneManager != nullptr ? pSceneManager->findActiveObject( name ) : nullptr;
        }
    } // namespace engine
} // namespace sw
