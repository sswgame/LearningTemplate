#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/IGameAutoplayQuery.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct GameModeComponentInternal
        {
            /** @brief 세운 핸들 목록을 덜어 내기 시작하는 크기입니다(그 뒤로는 덜어 낸 크기의 두 배마다). */
            static constexpr uint32 kMinCompactThreshold = 64;
        };
    } // namespace
} // namespace sw

namespace sw
{
    GameModeComponent::GameModeComponent()
        : _bAutoPlay{ false }
        , _tickAfter{}
        , _listSpawned{}
        , _pendingStateBytes{}
        , _soundQueue{}
        , _compactThreshold{ GameModeComponentInternal::kMinCompactThreshold }
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
        if ( startGame() == false )
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

    void GameModeComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bRuleOnSubTick == SW_FALSE )
            runRules( deltaTime );
    }

    void GameModeComponent::onSubTick( uint32 subTickID, float32 deltaTime )
    {
        Component::onSubTick( subTickID, deltaTime );
        if ( subTickID == kRuleSubTick && _bRuleOnSubTick == SW_TRUE )
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
        (void)registerSubTick( TickGroup::PrePhysics, kRuleSubTick ); // 핸들은 쓰지 않는다 — id 로 다룬다
        (void)addSubTickPrerequisite( kRuleSubTick, pBefore->getRuleTickHandle() );
        _bRuleOnSubTick = SW_TRUE;
    }

    void GameModeComponent::runRules( float32 deltaTime )
    {
        if ( _bStarted == SW_FALSE )
            return;
        if ( _bViewsSpawned == SW_FALSE )
            scheduleFlush(); // 상태 저장 전에 걷었다(또는 처음) — 지금 상태대로 다시 세운다
        updateGame( deltaTime );
        if ( _soundQueue.isEmpty() == false || hasPendingSpawn() )
            scheduleFlush();
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
        }
        _listSpawned.clear();
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
        GameObjectManager* pManager = getObjectManager();
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

    GameObject* GameModeComponent::findGameObject( GameObjectHandle handle ) const
    {
        GameObjectManager* pManager = getObjectManager();
        return pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
    }

    void GameModeComponent::reportMissingAsset( string_view path )
    {
        SW_LOG_ERROR_ID( "GameMode.RequiredAssetMissing", "GameMode: the required asset '%#' could not be loaded - the game cannot start", path );
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

    GameObjectManager* GameModeComponent::getObjectManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw
