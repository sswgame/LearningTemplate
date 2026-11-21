#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/ExplosiveBarrelComponent.h"

#include "Core/Container/vector.h"

#include "Engine/Destruction/Fracture/FractureBaseComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/GimmickDamageUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

namespace sw
{
    ExplosiveBarrelComponent::ExplosiveBarrelComponent()
        : _health{ 30.0f }
        , _radius{ 4.0f }
        , _damage{ 80.0f }
        , _fuseTime{ 0.0f }
        , _fractureStrain{ 300.0f }
        , _blastImpulse{ 400.0f }
        , _damageTaken{ 0.0f }
        , _bExploded{ false }
        , _fuseStepsLeft{ -1 }
        , _clock{}
        , _blastCenter{}
    {
    }

    void ExplosiveBarrelComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = _clock.consume( context.getDeltaTime() );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void ExplosiveBarrelComponent::stepOnce()
    {
        GameObject*             pOwner  = getOwner();
        GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pOwner == nullptr || _bExploded )
            return;
        if ( _fuseTime > 0.0f )
        {
            if ( _fuseStepsLeft < 0 )
                _fuseStepsLeft = _clock.secondsToSteps( _fuseTime, 1 );
            if ( --_fuseStepsLeft <= 0 )
            {
                explode( *pOwner );
                return;
            }
        }
        if ( pSensor == nullptr )
            return;
        _damageTaken += pSensor->consumeDamage();
        if ( _damageTaken >= _health )
            explode( *pOwner );
    }

    void ExplosiveBarrelComponent::explode( GameObject& owner )
    {
        _bExploded                        = true;
        const GameObjectManager* pManager = owner.getManager(); // 틱에서 부른다 — 반경 피해는 명령 버퍼로
        const SceneComponent*    pScene   = owner.getPrimarySceneComponent();
        const float3             center   = pScene != nullptr ? pScene->getWorldPosition() : float3{};
        GimmickUtil::sendCue( owner, hashed_string( "Explosion" ), center, _damage );
        // 파괴 컴포넌트가 있으면 몸을 끄지 않는다. 조각으로 부서져 날아가며, 조각의 그래픽과 바디가 그 몸이다.
        FractureBaseComponent* pSelfFracture = owner.getComponent<FractureBaseComponent>();
        if ( pSelfFracture != nullptr && pSelfFracture->hasFractureData() )
            GimmickDamageUtil::breakAtCenter( *pSelfFracture, center, _fractureStrain * 2.0f, _blastImpulse );
        else
            GimmickUtil::setBodyActive( owner, false, this );
        if ( pManager == nullptr || _radius <= 0.0f )
            return;
        _blastCenter = center;
        pManager->executeOrDeferPostTickOnComponent( *this, []( ExplosiveBarrelComponent& self, GameObjectManager& manager )
        { self.applyRadialDamage( manager ); } );
    }

    void ExplosiveBarrelComponent::applyRadialDamage( GameObjectManager& manager ) const
    {
        const float3& center = _blastCenter;
        // 반경 안의 것에 거리 감쇠 피해를 준다. 다른 드럼통은 자기 스텝에 터지며 연쇄가 이어진다. 파괴 오브젝트는 경계가 반경에 닿으면 위치 기반 폭발로 깬다.
        // 피해를 받는 쪽은 센서(`GimmickSensorComponent` — 피해를 쌓는다)와 파괴 컴포넌트뿐이라 그 둘의 등록 목록(`ComponentRegistry`)만 본다 — 씬 전체를 돌지 않는다
        // (2D · 3D 몸 없는 센서도 든다). 조각 깨기는 새 컴포넌트를 등록하므로 고르기를 먼저 끝내고 쓰기(조각 깨기 · 피해 이벤트)는 그 뒤에 한다.
        struct BlastTarget
        {
            GameObject*            _pObject;
            FractureBaseComponent* _pFracture; ///< 경계가 반경에 닿은 파괴 컴포넌트(없으면 nullptr)
            float32                _distance;
        };
        GameObject* const   pSource = getOwner();
        const uint64        selfID  = pSource != nullptr ? pSource->getObjectID() : 0;
        const float32       radius  = _radius;
        vector<BlastTarget> listTarget;
        const auto          consider = [selfID, radius, &center, &listTarget]( GameObject* pObject, FractureBaseComponent* pFracture )
        {
            const SceneComponent* pOther = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pOther == nullptr || pObject->getObjectID() == selfID )
                return;
            for ( const BlastTarget& existing : listTarget )
            {
                if ( existing._pObject == pObject )
                    return; // 센서와 파괴 컴포넌트를 함께 가진 오브젝트는 한 번만
            }
            const bool    bReached = pFracture != nullptr && pFracture->isReachedBy( center, radius );
            const float32 distance = float3::getDistance( pOther->getWorldPosition(), center );
            if ( bReached || distance <= radius )
                listTarget.push_back( BlastTarget{ pObject, bReached ? pFracture : nullptr, distance } );
        };
        const ComponentRegistry& registry = manager.getComponentRegistry();
        for ( FractureBaseComponent* pFracture : registry.getAll<FractureBaseComponent>() )
            consider( pFracture->getOwner(), pFracture );
        for ( GimmickSensorComponent* pSensor : registry.getAll<GimmickSensorComponent>() )
        {
            GameObject* const pObject = pSensor->getOwner();
            consider( pObject, pObject != nullptr ? pObject->getComponent<FractureBaseComponent>() : nullptr );
        }
        for ( const BlastTarget& target : listTarget )
        {
            if ( target._pFracture != nullptr )
                target._pFracture->applyRadialDamageAtWorld( center, radius, _fractureStrain, _blastImpulse );
            if ( target._distance <= radius )
                GimmickDamageUtil::applyDamage( *target._pObject, pSource, hashed_string( "Explosion" ), _damage * ( 1.0f - target._distance / radius ) );
        }
    }

    void ExplosiveBarrelComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<ExplosiveBarrelComponent>( this );
    }

    void ExplosiveBarrelComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<ExplosiveBarrelComponent>( this );
        Component::onUnregister( manager );
    }
} // namespace sw
