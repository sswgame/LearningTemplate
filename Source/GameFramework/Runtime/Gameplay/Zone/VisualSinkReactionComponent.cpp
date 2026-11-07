#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/VisualSinkReactionComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

namespace sw
{
    VisualSinkReactionComponent::VisualSinkReactionComponent()
        : _attribute{ "SinkDepth" }
        , _scale{ 1.0f }
        , _listVisual{}
        , _appliedDepth{ 0.0f }
    {
        setCanEverTick( true );
    }

    void VisualSinkReactionComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 메시 자리 쓰기는 틱 뒤 게임 스레드에서 한다(속성은 이 프레임의 지역 처리 뒤 값이다).
        pManager->executeOrDeferPostTickOnComponent( *this, []( VisualSinkReactionComponent& self, GameObjectManager& manager )
        { self.applyDepth( manager ); } );
    }

    void VisualSinkReactionComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            restoreVisuals( *pManager );
        Component::onEndPlay();
    }

    void VisualSinkReactionComponent::applyDepth( GameObjectManager& manager )
    {
        GameObject* const             pOwner   = getOwner();
        const AbilityComponent* const pAbility = pOwner != nullptr ? pOwner->getComponent<AbilityComponent>() : nullptr;
        const std::optional<float32>  value    = pAbility != nullptr ? pAbility->getAttributeValue( _attribute ) : std::nullopt;
        const float32                 depth    = value.has_value() ? MathUtil::max( 0.0f, *value ) * _scale : 0.0f; // 속성이 없는 개체는 가라앉지 않는다
        if ( depth <= 0.0f )
        {
            restoreVisuals( manager );
            return;
        }
        // 처음 내릴 때(또는 외형이 다시 조립돼 메시 수가 바뀌면) 다른 메시에 매달리지 않은 메시를 모두 고르고 원래 자리를 적는다.
        uint32 rootMeshCount = 0;
        pOwner->forEachComponentOfType<MeshComponent>( [&rootMeshCount]( MeshComponent* pMesh )
        { rootMeshCount += hasMeshAncestor( *pMesh ) ? 0u : 1u; } );
        if ( rootMeshCount != _listVisual.size() )
        {
            restoreVisuals( manager );
            pOwner->forEachComponentOfType<MeshComponent>( [this]( MeshComponent* pMesh )
            {
                if ( hasMeshAncestor( *pMesh ) == false )
                    _listVisual.push_back( SunkVisual{ TComponentHandle<MeshComponent>{ pMesh }, pMesh->getLocalPosition() } );
            } );
        }
        for ( const SunkVisual& visual : _listVisual )
        {
            if ( MeshComponent* pMesh = manager.resolveComponent( visual._mesh ) )
                pMesh->setLocalPosition( visual._localPosition - float3{ 0.0f, depth, 0.0f } );
        }
        _appliedDepth = depth;
    }

    void VisualSinkReactionComponent::restoreVisuals( GameObjectManager& manager )
    {
        for ( const SunkVisual& visual : _listVisual )
        {
            if ( MeshComponent* pMesh = manager.resolveComponent( visual._mesh ) )
                pMesh->setLocalPosition( visual._localPosition );
        }
        _listVisual.clear();
        _appliedDepth = 0.0f;
    }

    bool VisualSinkReactionComponent::hasMeshAncestor( const SceneComponent& component )
    {
        for ( const SceneComponent* pParent = component.getParent(); pParent != nullptr; pParent = pParent->getParent() )
        {
            if ( isA<MeshComponent>( pParent ) )
                return true;
        }
        return false;
    }
} // namespace sw
