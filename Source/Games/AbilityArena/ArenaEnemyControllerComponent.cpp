#include "pch.h"

#include "Games/AbilityArena/ArenaEnemyControllerComponent.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Ability/AbilitySystemComponent.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    ArenaEnemyControllerComponent::ArenaEnemyControllerComponent()
        : _kind{ ArenaUnitKind::Grunt }
        , _meleeReach{ 1.4f }
        , _preferredMin{ 5.0f }
        , _preferredMax{ 9.0f }
        , _fireRange{ 12.0f }
        , _unitRadius{ 0.5f }
        , _targetTick{}
    {
    }

    void ArenaEnemyControllerComponent::onBeginPlay()
    {
        ArenaControllerComponent::onBeginPlay();
        // 플레이어 컨트롤러(주 틱) 뒤에 돌 수 있게 AI 는 서브틱에서 돈다. 선행 조건은 대상을 처음 볼 때 건다(`resolveTargetPosition`).
        setCanEverTick( false );
        _targetTick = SubTickHandle{};
        (void)registerSubTick( TickGroup::DuringPhysics, kChaseSubTick );
    }

    void ArenaEnemyControllerComponent::onSubTick( uint32 subTickId, float32 deltaTime )
    {
        ArenaControllerComponent::onSubTick( subTickId, deltaTime );
        if ( subTickId == kChaseSubTick )
            ArenaControllerComponent::onTick( deltaTime );
    }

    float3 ArenaEnemyControllerComponent::resolveTargetPosition( const ArenaUnitView& target )
    {
        GameObject*                     pOwner            = getOwner();
        GameObjectManager*              pManager          = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*                     pTargetObject     = pManager != nullptr ? pManager->resolveGameObject( target._object ) : nullptr;
        const ArenaControllerComponent* pTargetController = pTargetObject != nullptr ? pTargetObject->getComponent<ArenaControllerComponent>() : nullptr;
        const MeshComponent*            pTargetMesh       = pTargetObject != nullptr ? pTargetObject->getComponent<MeshComponent>() : nullptr;
        if ( pTargetController == nullptr || pTargetMesh == nullptr )
            return target._position;

        const SubTickHandle targetTick = pTargetController->getTickHandle();
        if ( targetTick != _targetTick )
        {
            // 틱 중이라 갈아 거는 것은 틱 뒤에 적용된다 — 이번 프레임은 아직 대상과 나란히 돌 수 있으니 디렉터 모습(틱 전 자리)을 쓴다.
            if ( _targetTick.isValid() )
                (void)removeSubTickPrerequisite( kChaseSubTick, _targetTick );
            (void)addSubTickPrerequisite( kChaseSubTick, targetTick );
            _targetTick = targetTick;
            return target._position;
        }
        // 대상 컨트롤러가 이번 프레임에 옮긴 자리는 이 스테이지 앞에서 적용됐다.
        return pTargetMesh->getWorldPosition();
    }

    void ArenaEnemyControllerComponent::tickController( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem,
                                                        float3& inoutPosition )
    {
        const ArenaUnitView* pTarget = director.findNearestHostileView( getOwner()->getHandle(), 100.0f );
        if ( pTarget != nullptr )
        {
            const float3  targetPosition = resolveTargetPosition( *pTarget );
            const float3  toTarget       = targetPosition - inoutPosition;
            const float32 distance       = toTarget.getLength();
            setFacing( flattenDirection( toTarget, getFacing() ) );
            if ( _kind == ArenaUnitKind::Caster )
            {
                // 원거리 — 적당한 거리를 지키며 쏜다. 물러나도 대상을 본다.
                if ( distance > _preferredMax )
                    moveTowards( abilitySystem, toTarget, deltaTime, inoutPosition );
                else if ( distance < _preferredMin )
                    moveTowards( abilitySystem, float3{ 0.0f, 0.0f, 0.0f } - toTarget, deltaTime, inoutPosition );
                setFacing( flattenDirection( toTarget, getFacing() ) );
                if ( distance <= _fireRange )
                    tapInput( abilitySystem, ArenaDirectorComponent::kInputFireball );
            }
            else if ( distance > _meleeReach )
            {
                moveTowards( abilitySystem, toTarget, deltaTime, inoutPosition );
            }
            else
            {
                tapInput( abilitySystem, ArenaDirectorComponent::kInputMelee );
            }
        }
        separateFromEnemies( director, inoutPosition );
    }

    void ArenaEnemyControllerComponent::separateFromEnemies( const ArenaDirectorComponent& director, float3& inoutPosition ) const
    {
        const GameObjectHandle       self     = getOwner()->getHandle();
        const vector<ArenaUnitView>& listView = director.getUnitViews();
        const ArenaUnitView*         pView    = listView.data();
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const ArenaUnitView& other = pView[viewIndex];
            if ( other._object == self || other._kind == ArenaUnitKind::Player )
                continue;
            const float3  apart    = inoutPosition - other._position;
            const float32 distance = apart.getLength();
            const float32 overlap  = _unitRadius * 2.0f - distance;
            if ( overlap <= 0.0f || distance < 1.0e-4f )
                continue;
            inoutPosition = inoutPosition + apart * ( 0.5f * overlap / distance );
        }
    }
} // namespace sw
