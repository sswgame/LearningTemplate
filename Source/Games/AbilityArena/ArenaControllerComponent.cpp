#include "pch.h"

#include "Games/AbilityArena/ArenaControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Ability/CombatAttributeSet.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    ArenaControllerComponent::ArenaControllerComponent()
        : _director{}
        , _facing{ 0.0f, 0.0f, 1.0f }
        , _deathScale{ 1.0f, 1.0f, 1.0f }
        , _deathTimer{ -1.0f }
    {
        setCanEverTick( true );
    }

    void ArenaControllerComponent::assignDirector( GameObjectHandle director, const float3& facing )
    {
        _director = director;
        _facing   = facing;
    }

    float3 ArenaControllerComponent::flattenDirection( const float3& direction, const float3& fallback )
    {
        const float3  flat   = float3{ direction._x, 0.0f, direction._z };
        const float32 length = flat.getLength();
        if ( length < 1.0e-4f )
            return fallback;
        return float3{ flat._x / length, 0.0f, flat._z / length };
    }

    void ArenaControllerComponent::tapInput( AbilitySystemComponent& abilitySystem, int32 inputId )
    {
        abilitySystem.abilityInputPressed( inputId );
        abilitySystem.abilityInputReleased( inputId );
    }

    void ArenaControllerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*             pOwner         = getOwner();
        GameObjectManager*      pManager       = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*          pMesh          = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        AbilitySystemComponent* pAbilitySystem = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr || pAbilitySystem == nullptr || deltaTime <= 0.0f )
            return;
        if ( _deathTimer >= 0.0f || pAbilitySystem->hasMatchingTag( CombatAttributeSet::getDeadTag() ) )
        {
            tickDeath( deltaTime, *pMesh );
            return;
        }
        const ArenaDirectorComponent* pDirector = GameDirectorComponent::resolve<ArenaDirectorComponent>( *pManager, _director );
        if ( pDirector == nullptr )
            return;

        float3 position = pMesh->getLocalPosition();
        tickController( deltaTime, *pDirector, *pAbilitySystem, position );
        const float32 halfSize = pDirector->getArenaHalfSize();
        position._x            = MathUtil::clamp( position._x, -halfSize, halfSize );
        position._z            = MathUtil::clamp( position._z, -halfSize, halfSize );
        pMesh->setLocalPosition( position );
        // 모델의 앞(+Z)을 바라보는 쪽으로 — AI 는 움직이지 않고도 몸을 돌린다.
        pMesh->setLocalRotation( float3{ 0.0f, MathUtil::atan2( _facing._x, _facing._z ), 0.0f } );
    }

    void ArenaControllerComponent::moveTowards( const AbilitySystemComponent& abilitySystem, const float3& direction, float32 deltaTime, float3& inoutPosition )
    {
        const float3 flatDirection = flattenDirection( direction, _facing );
        _facing                    = flatDirection;
        // 이동 속도는 어트리뷰트의 current 다 — 대시(×3) · 둔화 이펙트가 여기에 그대로 반영된다.
        const float32 speed = abilitySystem.getAttributeValue( CombatAttributes::moveSpeed() );
        inoutPosition       = inoutPosition + flatDirection * ( speed * deltaTime );
    }

    void ArenaControllerComponent::tickDeath( float32 deltaTime, MeshComponent& mesh )
    {
        // 쓰러진 유닛은 납작해지다가 디렉터가 걷는다(같은 시간 — `ArenaDirectorComponent::kDeathLinger`).
        if ( _deathTimer < 0.0f )
        {
            _deathTimer = ArenaDirectorComponent::kDeathLinger;
            _deathScale = mesh.getLocalScale();
        }
        _deathTimer          = MathUtil::max( 0.0f, _deathTimer - deltaTime );
        const float32 squash = MathUtil::max( 0.05f, _deathTimer / ArenaDirectorComponent::kDeathLinger );
        mesh.setLocalScale( float3{ _deathScale._x, _deathScale._y * squash, _deathScale._z } );
    }
} // namespace sw
