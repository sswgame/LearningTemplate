#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Progression/ReputationComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

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
        : _reputation{}
        , _restore{}
        , _bOpenAtBeginPlay{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ReputationComponent::~ReputationComponent() = default;

    GameStateInitResult ReputationComponent::initialize( const ReputationCatalog* pCatalog )
    {
        if ( _restore.isOpen() )
            return GameStateInitResult::AlreadyInitialized; // 앞 디렉터가 연 평판을 다시 비우지 않는다
        _reputation.initialize( pCatalog );
        return _restore.open( *this );
    }

    void ReputationComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<ReputationComponent>( this );
    }

    void ReputationComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<ReputationComponent>( this );
        Component::onUnregister( manager );
    }

    void ReputationComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _bOpenAtBeginPlay == SW_TRUE )
            (void)initialize( nullptr ); // 카탈로그가 없어도 거절하지 않는다 — 결과는 새 판 · 복원 · 이미 열림뿐이다
    }

    void ReputationComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _restore.isOpen() == false )
            return;
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
