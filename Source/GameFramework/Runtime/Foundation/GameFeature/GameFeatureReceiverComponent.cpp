#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureReceiverComponent.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureComponents.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    GameFeatureReceiverComponent::GameFeatureReceiverComponent()
        : _receiver{}
        , _listAdded{}
        , _pComponents{ nullptr }
    {
    }

    void GameFeatureReceiverComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameFeatureComponents* const pComponents = game::findSubsystem<GameFeatureComponents>();
        if ( pComponents != nullptr )
            pComponents->addReceiver( *this );
    }

    void GameFeatureReceiverComponent::onUnregister( GameObjectManager& manager )
    {
        if ( _pComponents != nullptr )
            _pComponents->removeReceiver( *this );
        _pComponents = nullptr;
        _listAdded.clear();
        Component::onUnregister( manager );
    }

    void GameFeatureReceiverComponent::addFeatureComponent( const GameFeatureComponentRule& rule )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager == nullptr || pOwner->findComponentByTypeName( rule._componentType ) != nullptr )
            return;
        SW_ASSERT( pManager->isStructuralMutationFrozen() == false ); // 기능은 게임 프레임 앞에서 켜지고 시작은 틱 전이다 — 미뤄지면 무엇을 붙였는지 알 수 없다
        Component* pAdded = pManager->addComponentByName( pOwner, rule._componentType );
        if ( pAdded == nullptr )
            return; // 까닭은 매니저가 알렸다
        pAdded->setTransient( true );
        AddedEntry& added = _listAdded.emplace_back();
        added._ruleID     = rule._ruleID;
        added._component  = pAdded->getHandle();
    }

    void GameFeatureReceiverComponent::removeFeatureComponent( uint32 ruleID )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        for ( const AddedEntry& added : _listAdded )
        {
            Component* pComponent = added._ruleID == ruleID && pManager != nullptr ? pManager->resolveComponent( added._component ) : nullptr;
            if ( pComponent != nullptr )
                (void)pOwner->removeComponent( pComponent ); // 이 오브젝트의 것이다(핸들로 찾았다)
        }
        (void)VectorUtil::removeIf( _listAdded, [ruleID]( const AddedEntry& added ) // 붙인 적이 없으면 뺄 것도 없다
        { return added._ruleID == ruleID; } );
    }
} // namespace sw
