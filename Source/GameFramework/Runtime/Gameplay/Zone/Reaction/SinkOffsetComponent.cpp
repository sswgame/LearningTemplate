#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/Reaction/SinkOffsetComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Reflection/ReflectionCast.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"

namespace sw
{
    namespace
    {
        struct SinkOffsetInternal
        {
            /** @brief 자기 오브젝트의 메시를 핸들로 찾습니다. 내리는 메시는 늘 자기 것이라 매니저(남의 오브젝트 찾기, 틱에서는 읽기 전용)를 거치지 않는다. */
            static MeshComponent* findOwnMesh( const GameObject& self, TComponentHandle<MeshComponent> mesh )
            {
                return castTo<MeshComponent>( self.findComponentByID( mesh.componentID() ) );
            }
        };
    } // namespace

    SinkOffsetComponent::SinkOffsetComponent()
        : _attribute{}
        , _scale{ 1.0f }
        , _capsuleStep{ 0.1f }
        , _listVisual{}
        , _appliedDepth{ 0.0f }
    {
        setCanEverTick( true );
    }

    void SinkOffsetComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        // 자기 오브젝트의 속성을 읽고 자기 메시만 옮긴다 — 틱 안에서 해도 된다(예전에는 틱 뒤 큐로 미뤄 지역 처리 뒤에 오기를 바랐지만, 같은 큐 안 순서는 워커 순서였다).
        applySinkDepth();
    }

    void SinkOffsetComponent::onEndPlay()
    {
        restoreVisuals();
        Component::onEndPlay();
    }

    void SinkOffsetComponent::applySinkDepth()
    {
        GameObject* const                   pOwner   = getOwner();
        const AbilitySystemComponent* const pAbility = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        const optional<float32>             value    = pAbility != nullptr ? pAbility->getAttributeValue( _attribute ) : nullopt;
        const float32                       depth    = value.has_value() ? MathUtil::max( 0.0f, *value ) * _scale : 0.0f; // 속성이 없는 개체는 가라앉지 않는다
        if ( depth <= 0.0f || pOwner == nullptr )
        {
            restoreVisuals();
            return;
        }
        // 처음 내릴 때(또는 외형이 다시 조립돼 메시 수가 바뀌면) 다른 메시에 매달리지 않은 메시를 모두 고르고 원래 자리를 적는다.
        uint32 rootMeshCount = 0;
        pOwner->forEachComponentOfType<MeshComponent>( [&rootMeshCount]( MeshComponent* pMesh )
        { rootMeshCount += hasMeshAncestor( *pMesh ) ? 0u : 1u; } );
        if ( rootMeshCount != _listVisual.size() )
        {
            restoreVisuals();
            pOwner->forEachComponentOfType<MeshComponent>( [this]( MeshComponent* pMesh )
            {
                if ( hasMeshAncestor( *pMesh ) == false )
                    _listVisual.push_back( SunkVisual{ TComponentHandle<MeshComponent>{ pMesh }, pMesh->getZeroOriginPosition() } );
            } );
        }
        for ( const SunkVisual& visual : _listVisual )
        {
            if ( MeshComponent* pMesh = SinkOffsetInternal::findOwnMesh( *pOwner, visual._mesh ) )
                pMesh->setZeroOriginPosition( visual._restPosition - float3{ 0.0f, depth, 0.0f } );
        }
        // 판정 캡슐도 깊이를 따른다 — 단계로 내려 가라앉는 동안 프레임마다 캡슐을 다시 짓지 않는다.
        if ( CharacterControllerComponent* pController = pOwner->getComponent<CharacterControllerComponent>() )
            pController->setHeightReduction( MathUtil::floor( depth / _capsuleStep ) * _capsuleStep );
        _appliedDepth = depth;
    }

    void SinkOffsetComponent::restoreVisuals()
    {
        GameObject* const pOwner = getOwner();
        for ( const SunkVisual& visual : _listVisual )
        {
            MeshComponent* const pMesh = pOwner != nullptr ? SinkOffsetInternal::findOwnMesh( *pOwner, visual._mesh ) : nullptr;
            if ( pMesh != nullptr )
                pMesh->setZeroOriginPosition( visual._restPosition );
        }
        _listVisual.clear();
        if ( _appliedDepth > 0.0f && pOwner != nullptr )
        {
            if ( CharacterControllerComponent* pController = pOwner->getComponent<CharacterControllerComponent>() )
                pController->setHeightReduction( 0.0f );
        }
        _appliedDepth = 0.0f;
    }

    bool SinkOffsetComponent::hasMeshAncestor( const SceneComponent& component )
    {
        for ( const SceneComponent* pParent = component.getParent(); pParent != nullptr; pParent = pParent->getParent() )
        {
            if ( isA<MeshComponent>( pParent ) )
                return true;
        }
        return false;
    }
} // namespace sw
