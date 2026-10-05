#include "pch.h"

#include "GameFramework/Base/Framework/GameDirectorComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Base/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "GameDirector" );

    namespace
    {
        struct GameDirectorComponentInternal
        {
            /** @brief 세운 핸들 목록을 덜어 내기 시작하는 크기입니다(그 뒤로는 덜어 낸 크기의 두 배마다). */
            static constexpr uint32 kMinCompactThreshold = 64;
        };
    } // namespace
} // namespace sw

namespace sw
{
    GameDirectorComponent::GameDirectorComponent()
        : _bAutoPlay{ false }
        , _tickAfter{}
        , _listSpawned{}
        , _pendingStateBytes{}
        , _soundQueue{}
        , _compactThreshold{ GameDirectorComponentInternal::kMinCompactThreshold }
        , _bStarted{ SW_FALSE }
        , _bViewsSpawned{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _bRuleOnSubTick{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    GameDirectorComponent::~GameDirectorComponent() = default;

    void GameDirectorComponent::onBeginPlay()
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

    void GameDirectorComponent::onEndPlay()
    {
        despawnViews();
        _bStarted = SW_FALSE;
        Component::onEndPlay();
    }

    void GameDirectorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bRuleOnSubTick == SW_FALSE )
            runRules( deltaTime );
    }

    void GameDirectorComponent::onSubTick( uint32 subTickId, float32 deltaTime )
    {
        Component::onSubTick( subTickId, deltaTime );
        if ( subTickId == kRuleSubTick && _bRuleOnSubTick == SW_TRUE )
            runRules( deltaTime );
    }

    SubTickHandle GameDirectorComponent::getRuleTickHandle() const
    {
        SubTickHandle handle = getTickHandle();
        // 걸 대상이 있으면 아직 시작 전이어도 서브틱 핸들 — 먼저 시작한 뒤쪽 디렉터가 이것을 걸어도 맞는다.
        if ( _tickAfter.isValid() )
            handle._subTickId = kRuleSubTick;
        return handle;
    }

    void GameDirectorComponent::hookTickAfter()
    {
        if ( _tickAfter.isValid() == false )
            return;
        GameObjectManager*           pManager = getObjectManager();
        const GameObject*            pObject  = pManager != nullptr ? pManager->resolveGameObject( _tickAfter ) : nullptr;
        const GameDirectorComponent* pBefore  = pObject != nullptr ? pObject->getComponent<GameDirectorComponent>() : nullptr;
        if ( pBefore == nullptr || pBefore == this )
        {
            SW_LOG_WARNING( "GameDirector: _tickAfter does not point at another director - the rules stay on the main tick" );
            return;
        }
        (void)registerSubTick( TickGroup::PrePhysics, kRuleSubTick );
        (void)addSubTickPrerequisite( kRuleSubTick, pBefore->getRuleTickHandle() );
        _bRuleOnSubTick = SW_TRUE;
    }

    void GameDirectorComponent::runRules( float32 deltaTime )
    {
        if ( _bStarted == SW_FALSE )
            return;
        if ( _bViewsSpawned == SW_FALSE )
            scheduleFlush(); // 상태 저장 전에 걷었다(또는 처음) — 지금 상태대로 다시 세운다
        tickGame( deltaTime );
        if ( _soundQueue.isEmpty() == false || hasPendingSpawn() )
            scheduleFlush();
    }

    void GameDirectorComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bStarted == SW_TRUE )
            applyPendingState();
    }

    void GameDirectorComponent::despawnViews()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager != nullptr )
        {
            for ( GameObjectHandle& handle : _listSpawned )
                destroySpawned( *pManager, handle );
        }
        _listSpawned.clear();
        _compactThreshold = GameDirectorComponentInternal::kMinCompactThreshold;
        _bViewsSpawned    = SW_FALSE;
        onViewsDespawned();
    }

    bool GameDirectorComponent::isAutoPlayOn() const
    {
        return _bAutoPlay || GameAutoplay::isOn();
    }

    void GameDirectorComponent::applyPendingState()
    {
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        const bool bRestored = readState( archive );
        _pendingStateBytes.clear();
        // 새 판을 열며 쌓은 소리(첫 짓기 · 웨이브)는 되살린 판의 것이 아니다.
        _soundQueue.clear();
        onStateRestored( bRestored );
        despawnViews(); // 지금 상태대로 다시 세운다(다음 틱)
    }

    // ------------------------------------------------------------------------------
    // 틱 뒤 플러시(게임 스레드)
    // ------------------------------------------------------------------------------
    void GameDirectorComponent::scheduleFlush()
    {
        if ( _bFlushScheduled == SW_TRUE )
            return;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        _bFlushScheduled = SW_TRUE;
        // 틱 안이면 틱 뒤로 미뤄진다. 그 사이에 디렉터가 사라질 수 있으니 핸들로 다시 찾는다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            GameDirectorComponent* pDirector = static_cast<GameDirectorComponent*>( pManager->resolveComponent( self ) );
            if ( pDirector != nullptr )
                pDirector->flushPending();
        } );
    }

    void GameDirectorComponent::flushPending()
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

    GameObject* GameDirectorComponent::spawnPrefab( GameObjectManager& manager, const string& prefabPath, const utf8* pName )
    {
        AssetManager* pAssetManager = game::getService<AssetManager>();
        if ( pAssetManager == nullptr || prefabPath.empty() )
            return nullptr;
        GameObject* pObject = pAssetManager->getPrefabCache().spawn( &manager, prefabPath, pName );
        if ( pObject != nullptr )
            trackSpawned( *pObject );
        return pObject;
    }

    void GameDirectorComponent::trackSpawned( const GameObject& object )
    {
        _listSpawned.push_back( object.getHandle() );
    }

    void GameDirectorComponent::destroySpawned( GameObjectManager& manager, GameObjectHandle& inoutHandle )
    {
        GameObject* pObject = manager.resolveGameObject( inoutHandle );
        if ( pObject != nullptr )
            manager.destroyObject( pObject );
        inoutHandle = GameObjectHandle{};
    }

    void GameDirectorComponent::compactSpawned( const GameObjectManager& manager )
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
        _compactThreshold = MathUtil::max( GameDirectorComponentInternal::kMinCompactThreshold, static_cast<uint32>( keptCount * 2 ) );
    }

    GameObjectManager* GameDirectorComponent::getObjectManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw
