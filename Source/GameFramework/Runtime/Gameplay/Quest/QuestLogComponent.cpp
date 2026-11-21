#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"

namespace sw
{
    namespace
    {
        /** @brief 퀘스트 일지를 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다 — 형제 `PlayerStateComponent` 와 같다). */
        const StatefulComponentRegistrar<QuestLogComponent> s_questLogStateRegistration{ "QuestLogComponent" };
    } // namespace

    QuestLogComponent::QuestLogComponent()
        : _questLog{}
        , _restore{}
        , _bOpenAtBeginPlay{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    QuestLogComponent::~QuestLogComponent() = default;

    GameStateInitResult QuestLogComponent::initialize( const QuestCatalog* pCatalog )
    {
        if ( _restore.isOpen() )
            return GameStateInitResult::AlreadyInitialized; // 앞 디렉터가 연 일지를 다시 비우지 않는다
        _questLog.initialize( pCatalog );
        return _restore.open( *this );
    }

    void QuestLogComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<QuestLogComponent>( this );
    }

    void QuestLogComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<QuestLogComponent>( this );
        Component::onUnregister( manager );
    }

    void QuestLogComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _bOpenAtBeginPlay == SW_TRUE )
            (void)initialize( nullptr ); // 카탈로그가 없어도 거절하지 않는다 — 결과는 새 판 · 복원 · 이미 열림뿐이다
    }

    void QuestLogComponent::serializeState( StateIO& io )
    {
        _restore.serialize( io, kStateTag, kStateVersion, _questLog, "the quest log" );
    }

    void QuestLogComponent::restoreState( vector<uint8>&& bytes )
    {
        _restore.receive( *this, std::move( bytes ) );
    }
} // namespace sw
