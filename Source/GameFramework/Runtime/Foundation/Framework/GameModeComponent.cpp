#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/IGameAutoplayQuery.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/ReflectedInstance.h"
#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"

namespace sw
{
    namespace
    {
        struct GameModeComponentInternal
        {
            /** @brief 세운 핸들 목록을 덜어 내기 시작하는 크기입니다(그 뒤로는 덜어 낸 크기의 두 배마다). */
            static constexpr uint32 kMinCompactThreshold = 64;
            /** @brief 최고 걸음 시간으로 읽는 백분위입니다. */
            static constexpr uint32 kPeakPercent = 100;

            /**
             * @brief 감시 값 `GameMode.StepMs` · `GameMode.StepPeakMs` 입니다 — 프로파일러 구간 `kStepScopeName` 의 마지막 프레임 시간(씬의 디렉터 걸음 합) · 측정 창의 최고(밀리초).
             * @details 최고는 분포의 p100 이라 칸 아래 끝이다(약 12% 안, `FrameProfiler::getPercentileNanos`). 열린 디렉터가 없거나 계측이 꺼져 있으면 값이 없다(`Scene.ComponentTickMs` 와 같은 길).
             */
            template <bool bPeak>
            [[nodiscard]] static bool readStepMilliseconds( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                FrameProfiler* const pProfiler = game::findSubsystem<FrameProfiler>();
                if ( pProfiler == nullptr || pProfiler->isEnabled() == false )
                    return false;
                bool bStarted = false;
                for ( const GameModeComponent* pGameMode : pManager->getComponentRegistry().getAll<GameModeComponent>() )
                {
                    bStarted = bStarted || pGameMode->isStarted();
                }
                if ( bStarted == false )
                    return false;
                const uint32 slot  = pProfiler->registerScope( GameModeComponent::kStepScopeName );
                const uint64 nanos = bPeak ? pProfiler->getPercentileNanos( slot, kPeakPercent ) : pProfiler->getLastFrameNanos( slot );
                outValue           = static_cast<float64>( nanos ) / static_cast<float64>( constant::time::kNanosecondsPerMillisecond );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GameModeComponent::GameModeComponent()
        : _bAutoPlay{ false }
        , _tickAfter{}
        , _listSpawned{}
        , _listPool{}
        , _listFollowedAsset{}
        , _pendingStateBytes{}
        , _soundQueue{}
        , _compactThreshold{ GameModeComponentInternal::kMinCompactThreshold }
        , _pendingRuleDeltaTime{ 0.0f }
        , _bStarted{ SW_FALSE }
        , _bViewsSpawned{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _bRuleOnSubTick{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    GameModeComponent::~GameModeComponent() = default;

    void GameModeComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 규칙은 앞 그룹 — 뷰 · 컨트롤러 · 카메라 리그(뒤 그룹)가 같은 프레임에 이 결과를 읽는다.
        setTickGroup( TickGroup::PrePhysics );
        if ( openGame() == false )
            return;
        _bStarted = SW_TRUE;
        hookTickAfter();
        if ( _pendingStateBytes.empty() == false )
            applyPendingState();
        scheduleFlush();
        onGameStarted();
    }

    void GameModeComponent::onEndPlay()
    {
        despawnViews();
        _bStarted = SW_FALSE;
        Component::onEndPlay();
    }

    void GameModeComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<GameModeComponent>( this );
    }

    void GameModeComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<GameModeComponent>( this );
        Component::onUnregister( manager );
    }

    bool GameModeComponent::openGame()
    {
        vector<FollowedAsset> listPrevious = std::move( _listFollowedAsset ); // 규칙 객체는 `startGame` 이 다시 묶을 때까지 앞 판을 가리킨다
        _listFollowedAsset.clear();
        resetRuntimeState();
        if ( startGame() )
            return true;
        for ( FollowedAsset& previous : listPrevious )
        {
            _listFollowedAsset.push_back( std::move( previous ) ); // 다시 묶지 못한 규칙 객체가 남았다 — 쥔 채로 둔다
        }
        return false;
    }

    void GameModeComponent::applyReloadedAssets()
    {
        bool bReloaded = false;
        for ( FollowedAsset& followed : _listFollowedAsset )
        {
            shared_ptr<const void> pLatest = followed._takeLatest();
            if ( pLatest == nullptr || pLatest == followed._listHeld.back() )
                continue;
            followed._listHeld.push_back( std::move( pLatest ) );
            bReloaded = true;
        }
        if ( bReloaded == false )
            return;
        Archive state;
        writeState( state );
        state.setReadModeAndResetPosition( true );
        const bool bRebound = readState( state );
        if ( bRebound )
            SW_LOG_INFO_ID( "GameMode.ReloadedAssetApplied", "GameMode '%#': a reloaded asset is applied - the running game is bound to it", getTypeName().c_str() );
        else
            SW_LOG_WARNING_ID( "GameMode.ReloadedAssetRefused", "GameMode '%#': the running game does not fit a reloaded asset (see the messages above)", getTypeName().c_str() );
        onStateRestored( bRebound );
        despawnViews(); // 새 판의 모습으로 다시 세운다(이번 규칙 틱이 플러시를 잡는다)
    }

    void GameModeComponent::resetRuntimeState()
    {
        const TypeInfo* const pType = findCachedTypeInfo();
        if ( pType == nullptr )
            reportFatalError( "GameModeComponent '%#' has no reflected type - its state fields cannot be reset", getTypeName().c_str() );
        ReflectVisitOptions options;
        options._roleMask = PropertyRoleMask::makeRuntimeState();
        const ReflectPropertyVisitor propertyVisitor{ options };
        ReflectedInstance            fresh; // 기본 생성자의 값이 기본값이다 — 상태 칸이 없는 디렉터는 만들지 않는다
        (void)propertyVisitor.visitProperties( *pType, static_cast<void*>( this ), [this, pType, &fresh]( const ReflectVisitField& field )
        {
            if ( fresh.isValid() == false )
                fresh = ReflectedInstance::create( *pType );
            const bool bReset = fresh.isValid() && SerializerUtil::copyPropertyValue( field._property, ObjectAddress{ fresh.get() }, MutableObjectAddress{ this },
                                                                                      SerializeContext::getDefault( SerializePurpose::Snapshot ) );
            if ( bReset == false )
                reportFatalError( "state field '%#::%#' cannot be reset to its default before a game opens", pType->_fullyQualifiedName.c_str(), field._property._name.c_str() );
            return ReflectVisitFlow::Continue;
        } );
    }

    bool GameModeComponent::finishMinigame( const hashed_string& resultName )
    {
        MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
        if ( pFlow == nullptr || pFlow->findActiveDefinition() == nullptr )
        {
            SW_LOG_ERROR_ID( "GameMode.FinishWithoutMinigame", "GameMode '%#' finishes with result '%#' but no minigame is active - only a minigame director ends a minigame",
                             getTypeName().c_str(), resultName.c_str() );
            return false;
        }
        vector<MinigameResultValue> listValue;
        collectMinigameResult( listValue );
        pFlow->requestLeave( resultName, std::move( listValue ) );
        return true;
    }

    void GameModeComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bRuleOnSubTick == SW_FALSE )
            deferRules( context );
    }

    void GameModeComponent::onSubTick( uint32 subTickID, TickContext& context )
    {
        Component::onSubTick( subTickID, context );
        if ( subTickID == kRuleSubTick && _bRuleOnSubTick == SW_TRUE )
            deferRules( context );
    }

    void GameModeComponent::deferRules( TickContext& context )
    {
        // 규칙은 오브젝트를 만들고 지우고 남의 오브젝트를 읽고 쓴다 — 틱(워커)은 시간만 쌓고 규칙은 틱 뒤(게임 스레드, 같은 프레임의 적용 단계)에 돈다.
        // 언리얼 GameMode 가 게임 스레드에서 도는 자리다. 디렉터 사이 순서(`_tickAfter`)는 틱 뒤 큐의 디스패치 순번(뒤 스테이지가 뒤)이 지킨다.
        _pendingRuleDeltaTime += context.getDeltaTime();
        context.deferPostTickOnComponent( *this, []( GameModeComponent& gameMode )
        { gameMode.runPendingRules(); } );
    }

    void GameModeComponent::runPendingRules()
    {
        const float32 deltaTime = _pendingRuleDeltaTime;
        _pendingRuleDeltaTime   = 0.0f;
        if ( _bStarted == SW_TRUE )
            applyReloadedAssets();
        runRules( deltaTime );
    }

    SubTickHandle GameModeComponent::getRuleTickHandle() const
    {
        SubTickHandle handle = getTickHandle();
        // 걸 대상이 있으면 아직 시작 전이어도 서브틱 핸들 — 먼저 시작한 뒤쪽 디렉터가 이것을 걸어도 맞는다.
        if ( _tickAfter.isValid() )
            handle._subTickID = kRuleSubTick;
        return handle;
    }

    void GameModeComponent::hookTickAfter()
    {
        if ( _tickAfter.isValid() == false )
            return;
        GameObjectManager*       pManager = getObjectManager();
        const GameObject*        pObject  = pManager != nullptr ? pManager->resolveGameObject( _tickAfter ) : nullptr;
        const GameModeComponent* pBefore  = pObject != nullptr ? pObject->getComponent<GameModeComponent>() : nullptr;
        if ( pBefore == nullptr || pBefore == this )
        {
            SW_LOG_WARNING_ID( "GameMode.GameDirectorTickAfterDoesNot", "GameMode: _tickAfter does not point at another game mode - the rules stay on the main tick" );
            return;
        }
        (void)registerSubTick( TickGroup::PrePhysics, kRuleSubTick );                                                // 핸들은 쓰지 않는다 — id 로 다룬다
        (void)addSubTickPrerequisite( kRuleSubTick, pBefore->getRuleTickHandle(), TickPrerequisiteKind::OrderOnly ); // 앞 디렉터의 규칙 뒤에만 돌면 된다 — 그 자리를 읽지 않는다
        _bRuleOnSubTick = SW_TRUE;
    }

    void GameModeComponent::runRules( float32 deltaTime )
    {
        if ( _bStarted == SW_FALSE )
            return;
        if ( _bViewsSpawned == SW_FALSE )
            scheduleFlush(); // 상태 저장 전에 걷었다(또는 처음) — 지금 상태대로 다시 세운다
        // 디렉터 계약 ① 의 재는 자리 — 측정 문턱(RTS 유닛 2000 에서 2 ms 등)을 판정할 값을 모든 디렉터가 줄 추가 없이 낸다.
        {
            SW_PROFILE_SCOPE( kStepScopeName );
            updateGame( deltaTime );
        }
        if ( _soundQueue.isEmpty() == false || hasPendingSpawn() )
            scheduleFlush();
    }

    void GameModeComponent::serializeState( StateIO& io )
    {
        const TypeInfo* const pType = findCachedTypeInfo();
        if ( pType == nullptr )
            reportFatalError( "GameModeComponent '%#' has no reflected type - its state fields cannot be carried", getTypeName().c_str() );
        if ( io.isWriting() )
        {
            io.reflectedAs( *pType, *this );
            serializeGameState( io );
            return;
        }
        Archive backup;
        {
            StateIO backupIO( backup );
            backupIO.reflectedAs( *pType, *this );
        }
        io.reflectedAs( *pType, *this );
        if ( io.isOk() )
            serializeGameState( io );
        if ( io.isOk() )
            return;
        // 칸이나 손 본문이 거절했다 — 덮은 상태 칸을 읽기 전 값으로 되돌린다(손 본문은 끝까지 맞을 때만 대입하므로 그대로다).
        backup.setReadModeAndResetPosition( true );
        StateIO restoreIO( backup );
        restoreIO.reflectedAs( *pType, *this );
        SW_ASSERT( restoreIO.isOk() );
    }

    void GameModeComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bStarted == SW_TRUE )
            applyPendingState();
    }

    void GameModeComponent::despawnViews()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager != nullptr )
        {
            for ( GameObjectHandle& handle : _listSpawned )
            {
                destroySpawned( *pManager, handle );
            }
            for ( PrefabPool& pool : _listPool )
            {
                pool.destroyAll( *pManager );
            }
        }
        _listSpawned.clear();
        _listPool.clear();
        _compactThreshold = GameModeComponentInternal::kMinCompactThreshold;
        _bViewsSpawned    = SW_FALSE;
        onViewsDespawned();
    }

    bool GameModeComponent::isAutoPlayOn() const
    {
        if ( _bAutoPlay )
            return true;
        // 개발 스위치는 개발 구성에만 있다(Dev/ 의 서브시스템이 칸을 채운다) — 배포본은 칸이 비어 씬 데이터만 본다.
        const IGameAutoplayQuery* pDevSwitch = game::findSubsystem<IGameAutoplayQuery>();
        return pDevSwitch != nullptr && pDevSwitch->isGameAutoplayOn();
    }

    void GameModeComponent::applyPendingState()
    {
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        const bool bRestored = readState( archive );
        _pendingStateBytes.clear();
        // 새 판을 열며 쌓은 소리(첫 짓기 · 웨이브)는 복원한 판의 것이 아니다.
        _soundQueue.clear();
        onStateRestored( bRestored );
        despawnViews(); // 지금 상태대로 다시 세운다(다음 틱)
    }

    // ------------------------------------------------------------------------------
    // 틱 뒤 플러시(게임 스레드)
    // ------------------------------------------------------------------------------
    void GameModeComponent::scheduleFlush()
    {
        if ( _bFlushScheduled == SW_TRUE )
            return;
        const GameObjectManager* pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr; // 틱(규칙)에서도 부른다 — 명령 버퍼만 쓴다
        if ( pManager == nullptr )
            return;
        _bFlushScheduled = SW_TRUE;
        // 틱 안이면 틱 뒤로 미뤄진다. 그 사이에 디렉터가 사라질 수 있으니 핸들로 다시 찾는다.
        pManager->executeOrDeferPostTickOnComponent( *this, []( GameModeComponent& gameMode )
        { gameMode.flushPending(); } );
    }

    void GameModeComponent::flushPending()
    {
        _bFlushScheduled            = SW_FALSE;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr || _bStarted == SW_FALSE )
            return;
        const bool bRespawnViews = _bViewsSpawned == SW_FALSE;
        _bViewsSpawned           = SW_TRUE;
        onFlush( *pManager, bRespawnViews );
        _soundQueue.playAll();
        compactSpawned( *pManager );
    }

    GameObject* GameModeComponent::spawnPrefab( GameObjectManager& manager, string_view prefabPath, const utf8* pName )
    {
        AssetManager* pAssetManager = game::findSubsystem<AssetManager>();
        if ( pAssetManager == nullptr || prefabPath.empty() )
            return nullptr;
        GameObject* pObject = pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, prefabPath, pName );
        if ( pObject != nullptr )
            trackSpawned( *pObject );
        return pObject;
    }

    GameObject* GameModeComponent::spawnPrefab( GameObjectManager& manager, string_view prefabPath, const utf8* pName, const PrefabPoolPolicy& pool )
    {
        if ( pool._maxIdleCount == 0 )
            return spawnPrefab( manager, prefabPath, pName );
        PrefabPool* pPool = nullptr;
        for ( PrefabPool& existing : _listPool )
        {
            if ( string_view( existing.getPrefabPath() ) == prefabPath )
                pPool = &existing;
        }
        if ( pPool == nullptr )
            pPool = &_listPool.emplace_back( string( prefabPath ), pool._maxIdleCount );
        return pPool->acquire( manager, pName );
    }

    void GameModeComponent::releaseSpawned( GameObjectManager& manager, string_view prefabPath, GameObjectHandle& inoutHandle )
    {
        for ( PrefabPool& pool : _listPool )
        {
            if ( string_view( pool.getPrefabPath() ) != prefabPath )
                continue;
            pool.release( manager, inoutHandle );
            inoutHandle = GameObjectHandle{};
            return;
        }
        destroySpawned( manager, inoutHandle ); // 풀 정책 0 — 매번 스폰한 것이다
    }

    void GameModeComponent::trackSpawned( const GameObject& object )
    {
        _listSpawned.push_back( object.getHandle() );
    }

    void GameModeComponent::destroySpawned( GameObjectManager& manager, GameObjectHandle& inoutHandle )
    {
        GameObject* pObject = manager.resolveGameObject( inoutHandle );
        if ( pObject != nullptr )
            manager.destroyGameObject( pObject );
        inoutHandle = GameObjectHandle{};
    }

    void GameModeComponent::destroySpawned( GameObjectManager& manager, vector<GameObjectHandle>& inoutListHandle )
    {
        for ( GameObjectHandle& handle : inoutListHandle )
        {
            destroySpawned( manager, handle );
        }
        inoutListHandle.clear();
    }

    const GameObject* GameModeComponent::findGameObject( GameObjectHandle handle ) const
    {
        const GameObjectManager* pManager = getObjectManager();
        return pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
    }

    GameObject* GameModeComponent::findGameObject( GameObjectHandle handle )
    {
        GameObjectManager* pManager = getObjectManager();
        return pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
    }

    void GameModeComponent::reportMissingAsset( string_view path )
    {
        SW_LOG_ERROR_ID( "GameMode.RequiredAssetMissing", "GameMode: the required asset '%#' could not be loaded - the game cannot start", path );
    }

    void GameModeComponent::reportMissingCatalog( string_view kind )
    {
        SW_LOG_ERROR_ID( "GameMode.BoundCatalogMissing",
                         "GameMode: catalog '%#' is not bound in the game scope - the minigame (features, catalogs) or gamesettings gameFeatures must load it; the game cannot start", kind );
    }

    bool GameModeComponent::isActionDown( const hashed_string& action ) const
    {
        const InputManager* pInput = game::findSubsystem<InputManager>();
        return pInput != nullptr && GameInputView( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() ).isActionDown( action );
    }

    bool GameModeComponent::wasActionTriggered( const hashed_string& action ) const
    {
        const InputManager* pInput = game::findSubsystem<InputManager>();
        return pInput != nullptr && GameInputView( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() ).wasActionTriggered( action );
    }

    float2 GameModeComponent::getActionVector2D( const hashed_string& action ) const
    {
        const InputManager* pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return float2{ 0.0f, 0.0f };
        return GameInputView( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() ).getVector2D( action );
    }

    void GameModeComponent::compactSpawned( const GameObjectManager& manager )
    {
        if ( _listSpawned.size() < _compactThreshold )
            return;
        // 지운 것(파괴 대기 포함)은 풀리지 않는다 — 스스로 사라지는 투사체 · 갈아 세운 건물의 핸들이 쌓이지 않게 덜어 낸다.
        size_t keptCount = 0;
        for ( const GameObjectHandle& handle : _listSpawned )
        {
            if ( manager.resolveGameObject( handle ) != nullptr )
                _listSpawned.data()[keptCount++] = handle;
        }
        _listSpawned.resize( keptCount );
        _compactThreshold = MathUtil::max( GameModeComponentInternal::kMinCompactThreshold, static_cast<uint32>( keptCount * 2 ) );
    }

    const GameObjectManager* GameModeComponent::getObjectManager() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }

    GameObjectManager* GameModeComponent::getObjectManager()
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
    }

    SW_AUTOMATION_WATCH( gameModeStepMs, "GameMode.StepMs", "Rule step (updateGame) time of the last profiled frame in milliseconds, summed over the active scene's game modes (no value while the profiler is off)",
                         &GameModeComponentInternal::readStepMilliseconds<false> );
    SW_AUTOMATION_WATCH( gameModeStepPeakMs, "GameMode.StepPeakMs", "Longest profiled frame of rule steps (updateGame) in milliseconds since the profiler window opened (bucket lower bound; profiler on)",
                         &GameModeComponentInternal::readStepMilliseconds<true> );
} // namespace sw
