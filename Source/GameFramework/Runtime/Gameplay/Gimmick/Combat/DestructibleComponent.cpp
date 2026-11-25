#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/DestructibleComponent.h"

#include "Engine/Destruction/Fracture/FractureBaseComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/GimmickDamageUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

namespace sw
{
    DestructibleComponent::DestructibleComponent()
        : _listStageThreshold{ 20.0f, 50.0f }
        , _stageStrain{ 80.0f }
        , _shatterStrain{ 600.0f }
        , _damageTaken{ 0.0f }
        , _stage{ 0 }
    {
    }

    void DestructibleComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = context.getIntervalCount();
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void DestructibleComponent::stepOnce()
    {
        GameObject*             pOwner  = getOwner();
        GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor == nullptr )
            return;
        if ( isDestroyed() )
        {
            pSensor->setSignal( static_cast<float32>( _stage ) ); // 되살린 판의 센서는 신호를 싣지 않는다 — 부서진 단계에서 다시 낸다
            return;
        }
        _damageTaken += pSensor->consumeDamage();
        const int32            stageCount = static_cast<int32>( _listStageThreshold.size() );
        FractureBaseComponent* pFracture  = pOwner->getComponent<FractureBaseComponent>();
        const bool             bFracture  = pFracture != nullptr && pFracture->hasFractureData();
        while ( _stage < stageCount && _damageTaken >= _listStageThreshold[static_cast<size_t>( _stage )] )
        {
            ++_stage;
            const SceneComponent* pScene   = pOwner->getPrimarySceneComponent();
            const float3          position = pScene != nullptr ? pScene->getWorldPosition() : float3{};
            GimmickUtil::sendCue( *pOwner, hashed_string( _stage >= stageCount ? "Destroyed" : "Stage" ), position, static_cast<float32>( _stage ) );
            if ( bFracture == false )
                continue;
            // 단계마다 조각을 깎고, 마지막 단계는 통째로 부순다.
            const float32 strain = _stage >= stageCount ? _shatterStrain : _stageStrain * static_cast<float32>( _stage ) / static_cast<float32>( stageCount );
            GimmickDamageUtil::breakAtCenter( *pFracture, position, strain, strain * 0.5f );
        }
        pSensor->setSignal( static_cast<float32>( _stage ) );
        if ( isDestroyed() && bFracture == false )
            GimmickUtil::setBodyActive( *pOwner, false, this );
    }
} // namespace sw
