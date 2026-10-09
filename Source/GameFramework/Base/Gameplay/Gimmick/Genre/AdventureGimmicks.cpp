#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/AdventureGimmicks.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Gimmick/ElementRuleTable.h"
#include "GameFramework/Base/Gameplay/Gimmick/Genre/GenreGimmickUtil.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Base/World/Query/WorldQuery.h"

namespace sw
{
    SW_LOG_CALLER( "AdventureGimmicks" );
} // namespace sw

namespace sw
{
    PushBlockComponent::PushBlockComponent()
        : _cellSize{ 1.0f }
        , _moveTime{ 0.3f }
        , _bPlanar2D{ false }
        , _from{}
        , _to{}
        , _stepsLeft{ 0 }
        , _moveSteps{ 0 }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    bool PushBlockComponent::push( const float3& direction )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SceneComponent*    pScene   = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pScene == nullptr || isMoving() )
            return false;
        // 가장 가까운 축 하나로 맞춘다(3D 는 X/Z, 2D 는 X/Y).
        const float32 along  = direction._x;
        const float32 across = _bPlanar2D ? direction._y : direction._z;
        float3        cellStep{};
        if ( MathUtil::abs( along ) >= MathUtil::abs( across ) && along != 0.0f )
            cellStep._x = along > 0.0f ? _cellSize : -_cellSize;
        else if ( across != 0.0f && _bPlanar2D )
            cellStep._y = across > 0.0f ? _cellSize : -_cellSize;
        else if ( across != 0.0f )
            cellStep._z = across > 0.0f ? _cellSize : -_cellSize;
        else
            return false;
        const float3 from = pScene->getWorldPosition();
        WorldRayHit  hit;
        if ( WorldQuery::raycast( *pManager, from, from + cellStep, pOwner->getObjectId(), hit ) )
            return false;
        _from      = from;
        _to        = from + cellStep;
        _moveSteps = GenreGimmickUtil::toSteps( _moveTime, 1 );
        _stepsLeft = _moveSteps;
        return true;
    }

    void PushBlockComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void PushBlockComponent::stepOnce()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SceneComponent*    pScene   = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pScene == nullptr )
            return;
        // 상호작용(Push)을 끝낸 이가 있으면 그 사람에게서 블록 쪽으로 민다.
        GimmickSensorComponent* pSensor = pOwner->getComponent<GimmickSensorComponent>();
        if ( pSensor != nullptr && pSensor->consumeUses() > 0.0f )
        {
            const InteractableComponent* pInteractable = pOwner->getComponent<InteractableComponent>();
            const GameObject*            pPusher       = pInteractable != nullptr ? pManager->resolveGameObject( pInteractable->getLastInteractor() ) : nullptr;
            if ( pPusher != nullptr && pPusher->getPrimarySceneComponent() != nullptr )
                (void)push( pScene->getWorldPosition() - pPusher->getPrimarySceneComponent()->getWorldPosition() );
        }
        if ( _stepsLeft <= 0 )
            return;
        --_stepsLeft;
        const float32 alpha = 1.0f - static_cast<float32>( _stepsLeft ) / static_cast<float32>( MathUtil::max( 1, _moveSteps ) );
        pScene->setWorldPosition( float3::lerp( _from, _to, alpha ) );
    }

    ElementStatusComponent::ElementStatusComponent()
        : _tablePath{}
        , _material{ "Torch" }
        , _signalStatus{ "Burning" }
        , _startStimulus{}
        , _grid{}
        , _seenTableReloadCount{ 0 }
    {
    }

    void ElementStatusComponent::rebuild()
    {
        _seenTableReloadCount          = ElementRuleTable::getSharedReloadCount();
        const ElementRuleTable* pTable = ElementRuleTable::findShared( _tablePath.empty() ? string_view( ElementRuleTable::kDefaultPath ) : string_view( _tablePath ) );
        if ( pTable == nullptr )
        {
            SW_LOG_ERROR( "Element rule table %# could not be read", _tablePath.empty() ? ElementRuleTable::kDefaultPath : _tablePath.c_str() );
            return;
        }
        _grid.initialize( 1, 1, pTable );
        const int32 material = pTable->findMaterial( _material );
        if ( material < 0 )
            SW_LOG_ERROR( "Element material '%#' is not in the rule table", _material.c_str() );
        _grid.setMaterial( int2{ 0, 0 }, MathUtil::max( 0, material ) );
    }

    void ElementStatusComponent::onPostLoad()
    {
        Component::onPostLoad();
        rebuild();
    }

    void ElementStatusComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _grid.getTable() == nullptr )
            rebuild();
        if ( _startStimulus.empty() == false )
            (void)applyStimulus( _startStimulus ); // 바뀐 것이 없거나 모르는 자극이면 false — 시작 상태 그대로 둔다
        publishSignal();
    }

    void ElementStatusComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _seenTableReloadCount != ElementRuleTable::getSharedReloadCount() )
            rebuild(); // 규칙 표 파일을 고쳤다 — 새 표로 다시 짓는다(상태는 처음부터)
        if ( _grid.update( deltaTime ) > 0 )
            publishSignal();
    }

    bool ElementStatusComponent::applyStimulus( const hashed_string& stimulus )
    {
        const ElementRuleTable* pTable = _grid.getTable();
        if ( pTable == nullptr )
            return false;
        const bool bChanged = _grid.applyStimulus( int2{ 0, 0 }, pTable->findStimulus( stimulus ) ) > 0;
        publishSignal();
        return bChanged;
    }

    bool ElementStatusComponent::hasStatus( const hashed_string& status ) const
    {
        const ElementRuleTable* pTable = _grid.getTable();
        return pTable != nullptr && _grid.hasStatus( int2{ 0, 0 }, pTable->findStatus( status ) );
    }

    void ElementStatusComponent::setMaterial( const hashed_string& material )
    {
        _material = material;
        rebuild();
        publishSignal();
    }

    void ElementStatusComponent::publishSignal()
    {
        GameObject*             pOwner  = getOwner();
        GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor != nullptr )
            pSensor->setSignal( hasStatus( _signalStatus ) ? 1.0f : 0.0f );
    }
} // namespace sw
