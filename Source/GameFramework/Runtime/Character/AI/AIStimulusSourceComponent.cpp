#include "pch.h"

#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"

#include "GameFramework/Runtime/Character/AI/AIPerception.h"

namespace sw
{
    namespace
    {
        // 층은 팀 × 종류마다 하나다. 주소가 층의 열쇠라 정적 표로 둔다. 사용자 수 0 — 원천이 셀을 걸면 바로 맞추고, 읽는 쪽은 질의만 한다.
        const SpatialGridLayerDesc kArrStimulusLayer[AIStimulusSourceComponent::kTeamCount][static_cast<size_t>( AIStimulusKind::Count )] = {
            {{ "AIStimulusSight0", SpatialGridPlane::XZ, 0 }, { "AIStimulusHearing0", SpatialGridPlane::XZ, 0 }},
            {{ "AIStimulusSight1", SpatialGridPlane::XZ, 0 }, { "AIStimulusHearing1", SpatialGridPlane::XZ, 0 }},
            {{ "AIStimulusSight2", SpatialGridPlane::XZ, 0 }, { "AIStimulusHearing2", SpatialGridPlane::XZ, 0 }},
            {{ "AIStimulusSight3", SpatialGridPlane::XZ, 0 }, { "AIStimulusHearing3", SpatialGridPlane::XZ, 0 }},
        };
    } // namespace

    AIStimulusSourceComponent::AIStimulusSourceComponent()
        : _team{ 0 }
        , _bVisible{ true }
        , _noiseRadius{ 0.0f }
        , _arrEntry{}
    {
        setCanEverTick( false ); // 자리는 색인이 따라간다
    }

    const SpatialGridLayerDesc& AIStimulusSourceComponent::getLayer( uint8 team, AIStimulusKind kind )
    {
        SW_ASSERT( team < kTeamCount && kind < AIStimulusKind::Count );
        return kArrStimulusLayer[team < kTeamCount ? team : 0][static_cast<size_t>( kind )];
    }

    void AIStimulusSourceComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        // 위치가 없는 원천(씬 컴포넌트 없음)은 색인에 오르지 않는다.
        const SceneComponent* pScene = getOwner() != nullptr ? getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return;
        if ( _team >= kTeamCount )
        {
            SW_LOG_ERROR_ID( "AIStimulusSourceComponent.TeamOutOfRange", "AI stimulus source '%#' has team %# but the index has only %# teams - it is not registered", getOwner()->getName().c_str(),
                             static_cast<int32>( _team ), static_cast<int32>( kTeamCount ) );
            return;
        }
        SceneSpatialGrid& grid = manager.getSubsystem<SceneSpatialGrid>();
        if ( _bVisible )
        {
            const SpatialGridLayerDesc& sightLayer = getLayer( _team, AIStimulusKind::Sight );
            grid.requireCellSize( sightLayer, kCellSize );
            _arrEntry[static_cast<size_t>( AIStimulusKind::Sight )] = grid.add( sightLayer, *this, *pScene, 0.0f );
        }
        const SpatialGridLayerDesc& hearingLayer = getLayer( _team, AIStimulusKind::Hearing );
        grid.requireCellSize( hearingLayer, kCellSize );
        _arrEntry[static_cast<size_t>( AIStimulusKind::Hearing )] = grid.add( hearingLayer, *this, *pScene, _noiseRadius );
    }

    void AIStimulusSourceComponent::onUnregister( GameObjectManager& manager )
    {
        SceneSpatialGrid& grid = manager.getSubsystem<SceneSpatialGrid>();
        for ( SlotHandle& entry : _arrEntry )
        {
            grid.remove( entry );
        }
        Component::onUnregister( manager );
    }

    void AIStimulusSourceComponent::reportNoise( float32 radius )
    {
        _noiseRadius                      = MathUtil::max( radius, 0.0f );
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _arrEntry[static_cast<size_t>( AIStimulusKind::Hearing )].isValid() == false )
            return;
        // 틱(워커)에서도 불린다 — 색인은 게임 스레드에서만 고친다(`InteractableComponent::syncSpatialExtent` 와 같다).
        pManager->executeOrDeferPostTickOnComponent( *this, []( AIStimulusSourceComponent& component )
        {
            GameObjectManager* pTarget = component.getOwner() != nullptr ? component.getOwner()->getMutableManager() : nullptr;
            const SlotHandle   entry   = component._arrEntry[static_cast<size_t>( AIStimulusKind::Hearing )];
            if ( pTarget != nullptr && entry.isValid() )
                pTarget->getSubsystem<SceneSpatialGrid>().setExtent( entry, component._noiseRadius );
        } );
    }

    void AIStimulusSourceComponent::gatherStimuli( const GameObjectManager& manager, uint8 ownTeam, const float3& eye, float32 range, vector<AIStimulus>& outListStimulus, vector<Component*>& inoutListKey )
    {
        outListStimulus.clear();
        const SceneSpatialGrid& grid = manager.getSubsystem<SceneSpatialGrid>();
        for ( uint8 team = 0; team < kTeamCount; ++team )
        {
            if ( team == ownTeam )
                continue;
            for ( size_t kindIndex = 0; kindIndex < static_cast<size_t>( AIStimulusKind::Count ); ++kindIndex )
            {
                const SpatialGridLayerDesc& layer = kArrStimulusLayer[team][kindIndex];
                if ( grid.isActive( layer ) == false )
                    continue;
                float32 planeX{ 0.0f };
                float32 planeY{ 0.0f };
                SceneSpatialGrid::projectToPlane( layer, eye, planeX, planeY );
                // 소리 층은 항목 반 크기가 소리 반경이라 같은 상자로 "상자 + 반경 안" 이 걸린다.
                // shortcut: 귀 배율이 1 + range / 반경을 넘으면 그 밖 소리를 놓친다, 귀 배율을 키우는 게임이 생기면 질의 상자에 배율을 싣는다.
                grid.query( layer, planeX - range, planeY - range, planeX + range, planeY + range, inoutListKey );
                for ( Component* pKey : inoutListKey )
                {
                    // 이 층들의 열쇠는 원천 자신이다(`onRegister`). 해제가 항목을 빼므로 살아 있다.
                    const AIStimulusSourceComponent& source  = *static_cast<const AIStimulusSourceComponent*>( pKey );
                    const GameObject*                pObject = source.getOwner();
                    const SceneComponent*            pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
                    if ( pScene == nullptr )
                        continue;
                    const float3 position = pScene->getWorldPosition();
                    if ( kindIndex == static_cast<size_t>( AIStimulusKind::Sight ) )
                        outListStimulus.push_back( AIStimulus::makeSight( position, pObject->getObjectID() ) );
                    else if ( source._noiseRadius > 0.0f )
                        outListStimulus.push_back( AIStimulus::makeHearing( position, pObject->getObjectID(), source._noiseRadius ) ); // 조용한 원천은 소리 자극이 아니다
                }
            }
        }
    }
} // namespace sw
