#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Progression/ReputationComponent.h"

#include "Core/Foundation/BuildInfo.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 평판을 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다 — 형제 `PlayerStateComponent` 와 같다). */
        const StatefulComponentRegistrar<ReputationComponent> s_reputationStateRegistration{ "ReputationComponent" };
    } // namespace

    ReputationComponent::ReputationComponent()
        : _catalog{}
        , _pBoundCatalog{}
        , _reputation{}
        , _restore{}
        , _bOpenAtBeginPlay{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ReputationComponent::~ReputationComponent() = default;

    GameStateInitResult ReputationComponent::initialize( const ReputationCatalog& catalog )
    {
        if ( _restore.isOpen() )
            return GameStateInitResult::AlreadyInitialized; // 앞 디렉터가 연 평판을 다시 비우지 않는다
        _catalog       = SharedAssetRef<ReputationCatalog>::shareLive( &catalog );
        _pBoundCatalog = _catalog.snapshot();
        _reputation.initialize( catalog );
        return _restore.open( *this );
    }

    void ReputationComponent::rebindCatalog()
    {
        shared_ptr<const ReputationCatalog> pLatest = _catalog.snapshot();
        if ( pLatest == nullptr || pLatest == _pBoundCatalog )
            return;
        const shared_ptr<const ReputationCatalog> pPrevious = std::move( _pBoundCatalog ); // 다시 묶을 때까지 앞 판을 쥔다 — 평판이 그 정의를 가리킨다
        _pBoundCatalog                                      = std::move( pLatest );
        _restore.rebind( *this, [this]()
        { _reputation.initialize( *_pBoundCatalog ); } );
    }

    void ReputationComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _bOpenAtBeginPlay == SW_TRUE && _restore.isOpen() == false )
            (void)_restore.open( *this ); // 카탈로그를 빌리지 않은 평판이다 — 결과는 새 판 · 복원뿐이다
    }

    void ReputationComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _restore.isOpen() == false )
            return;
        if ( _catalog.get() != _pBoundCatalog.get() )
            context.deferPostTickOnComponent( *this, []( ReputationComponent& reputation )
            { reputation.rebindCatalog(); } );
        const GameObject* const         pOwner = getOwner();
        const GameStateComponent* const pWorld = pOwner != nullptr ? GameStateComponent::findOpen( pOwner->getManager() ) : nullptr;
        if ( pWorld == nullptr )
            return; // 월드 시계가 없는 씬은 날이 넘어가지 않는다
        for ( const WorldClockEvent& clockEvent : pWorld->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                _reputation.advanceDay();
        }
    }

    void ReputationComponent::serializeState( StateIO& io )
    {
        _restore.serialize( io, kStateTag, kStateVersion, _reputation, "the reputation" );
    }

    void ReputationComponent::restoreState( vector<uint8>&& bytes )
    {
        _restore.receive( *this, std::move( bytes ) );
    }
} // namespace sw
