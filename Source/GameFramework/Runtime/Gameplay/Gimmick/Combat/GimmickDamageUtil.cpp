#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/GimmickDamageUtil.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Destruction/Fracture/FractureBaseComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

namespace sw
{
    void GimmickDamageUtil::applyDamage( const GameObject& target, const GameObject* pSource, const hashed_string& kind, float32 amount )
    {
        if ( amount <= 0.0f )
            return;
        const GameObjectManager* pManager = target.getManager(); // 틱에서도 부른다 — 피해는 명령 버퍼로 틱 뒤에 넣는다
        if ( pManager == nullptr )                               // 씬 밖 — 맞음 구독(`onRegister`)이 없으니 센서만 받는다
        {
            GimmickSensorComponent* pSensor = target.getComponent<GimmickSensorComponent>();
            if ( pSensor != nullptr )
                pSensor->applyDamage( amount );
            return;
        }
        const GameObjectHandle targetHandle = target.getHandle();
        const GameObjectHandle sourceHandle = pSource != nullptr ? pSource->getHandle() : GameObjectHandle{};
        pManager->executeOrDeferPostTickOnScene( [targetHandle, sourceHandle, kind, amount]( GameObjectManager& sceneManager )
        {
            GameObject* pTarget = sceneManager.resolveGameObject( targetHandle );
            if ( pTarget == nullptr )
                return;
            GimmickSensorComponent* pSensor = pTarget->getComponent<GimmickSensorComponent>();
            if ( pSensor != nullptr )
                pSensor->applyDamage( amount );
            // 체력은 무기 · 근접과 같은 맞음 길로 깎는다(언리얼 ApplyDamage → TakeDamage) — 받는 체력 원천(`UnitStatsComponent::receiveHit` 등)이 피해를 정한다.
            // 받는 쪽이 없으면(체력도 센서도 없는 소품) 조용히 지나간다: 범위 · 점유자 피해는 대상을 영역이 고르고, 무기 광선이 벽을 맞힌 것과 같다.
            HitInfo hit;
            hit._pInstigator = sceneManager.resolveGameObject( sourceHandle );
            hit._kind        = kind;
            hit._damage      = amount;
            CharacterRayHit::deliverHit( *pTarget, hit );
        } );
    }

    void GimmickDamageUtil::breakAtCenter( FractureBaseComponent& fracture, const float3& center, float32 strain, float32 impulse )
    {
        const FractureAsset* pAsset = fracture.findAsset();
        const float32        radius = pAsset != nullptr ? float3::getDistance( pAsset->_boundsMin, pAsset->_boundsMax ) : 2.0f;
        fracture.applyRadialDamageAtWorld( center, radius, strain, impulse );
    }
} // namespace sw
