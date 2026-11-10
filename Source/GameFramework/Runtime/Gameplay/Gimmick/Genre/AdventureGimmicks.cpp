#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/AdventureGimmicks.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/ElementRuleCatalog.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/GenreGimmickUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"

namespace sw
{
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
        GameObject*        pOwner      = getOwner();
        GameObjectManager* pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SceneComponent*    pScene      = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool         bCannotMove = pManager == nullptr || pScene == nullptr || isMoving();
        if ( bCannotMove )
            return false;
        // 가장 가까운 축 하나로 맞춘다. 3D 는 X/Z, 2D 는 X/Y 축을 쓴다.
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
        if ( WorldQuery::raycast( *pManager, from, from + cellStep, pOwner->getObjectID(), hit ) )
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
        // 상호작용(Push)을 끝낸 쪽이 있으면 그 쪽에서 블록 방향으로 민다.
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
        , _material{}
        , _signalStatus{}
        , _startStimulus{}
        , _grid{}
        , _pTable{}
        , _tableReload{}
    {
    }

    void ElementStatusComponent::rebuild()
    {
        _tableReload.markSeen();
        if ( _tablePath.empty() )
        {
            SW_LOG_ERROR_ID( "AdventureGimmicks.ElementStatusNoRule", "Element status '%#' has no rule table path - set Rule Table", _material.c_str() );
            return;
        }
        // 새 테이블을 받기 전에 격자를 옛 테이블과 함께 쥔 채 둔다 — 받지 못하면 옛 상태 그대로다.
        shared_ptr<const ElementRuleCatalog> pTable = SharedAssetCache<ElementRuleCatalog>::acquire( _tablePath );
        if ( pTable == nullptr )
        {
            SW_LOG_ERROR_ID( "AdventureGimmicks.ElementRuleTableCould", "Element rule table %# could not be read", _tablePath.c_str() );
            return;
        }
        if ( _grid.initialize( 1, 1, pTable.get() ) == false )
            return; // 이유는 ElementGrid 가 남겼다
        _pTable                        = std::move( pTable );
        const optional<int32> material = _pTable->findMaterial( _material );
        if ( material.has_value() == false )
        {
            SW_LOG_ERROR_ID( "AdventureGimmicks.ElementMaterialNotRule", "Element material '%#' is not in the rule table", _material.c_str() );
            return; // 0 번 재질로 대신 서지 않는다
        }
        _grid.setMaterial( int2{ 0, 0 }, *material );
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
            (void)applyStimulus( _startStimulus ); // 바뀐 것이 없거나 알 수 없는 자극이면 false 이고, 이때는 시작 상태 그대로 둔다
        publishSignal();
    }

    void ElementStatusComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _tableReload.isStale() )
            rebuild(); // 규칙 테이블 파일이 바뀌었으므로 새 테이블로 다시 만든다. 상태는 처음부터 시작한다
        if ( _grid.update( deltaTime ) > 0 )
            publishSignal();
    }

    bool ElementStatusComponent::applyStimulus( const hashed_string& stimulus )
    {
        const ElementRuleCatalog* pTable = _grid.getTable();
        if ( pTable == nullptr )
            return false;
        const optional<int32> found    = pTable->findStimulus( stimulus );
        const bool            bChanged = found.has_value() && _grid.applyStimulus( int2{ 0, 0 }, *found ) > 0;
        publishSignal();
        return bChanged;
    }

    bool ElementStatusComponent::hasStatus( const hashed_string& status ) const
    {
        const ElementRuleCatalog* pTable = _grid.getTable();
        if ( pTable == nullptr )
            return false;
        const optional<int32> found = pTable->findStatus( status );
        return found.has_value() && _grid.hasStatus( int2{ 0, 0 }, *found );
    }

    void ElementStatusComponent::setMaterial( const hashed_string& material )
    {
        _material = material;
        rebuild();
        publishSignal();
    }

    void ElementStatusComponent::setTablePath( string_view path )
    {
        _tablePath = path;
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
