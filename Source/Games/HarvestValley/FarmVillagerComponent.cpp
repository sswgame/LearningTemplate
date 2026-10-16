#include "pch.h"

#include "Games/HarvestValley/FarmVillagerComponent.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/HarvestValley/FarmVillageDirectorComponent.h"

namespace sw
{
    FarmVillagerComponent::FarmVillagerComponent()
        : _director{}
        , _npcIndex{ -1 }
        , _heightOffset{ 0.5f }
        , _agent{}
        , _listGoal{ float3{} }
        , _listNeighbor{}
        , _bPlaced{ false }
    {
        setCanEverTick( true );
    }

    void FarmVillagerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 마을 디렉터(PrePhysics)가 이 프레임의 일정을 흘린 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void FarmVillagerComponent::assignVillager( GameObjectHandle director, int32 npcIndex )
    {
        _director = director;
        _npcIndex = npcIndex;
        _bPlaced  = false;
    }

    void FarmVillagerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const FarmVillageDirectorComponent* pDirector         = GameDirectorComponent::resolve<FarmVillageDirectorComponent>( *pManager, _director );
        const bool                          bFollowsAVillager = pDirector != nullptr && 0 <= _npcIndex && _npcIndex < pDirector->getVillagerCount();
        if ( bFollowsAVillager == false )
            return;
        const float3 goal     = pDirector->getVillagerPosition( _npcIndex );
        const bool   bTooFar  = float3::getHorizontalDistance( _agent.getPosition(), goal ) > pDirector->getBodySnapDistance();
        const bool   bJumpsTo = _bPlaced == false || bTooFar;
        if ( bJumpsTo )
        {
            _agent.setPosition( goal );
            _bPlaced = true;
        }
        else
        {
            _agent.setSettings( pDirector->makeBodySettings( _npcIndex ) );
            _listGoal[0] = goal;
            _agent.followPath( _listGoal );
            pDirector->collectNeighborPositions( _npcIndex, _listNeighbor );
            _agent.update( pDirector->getNavGrid(), _listNeighbor, deltaTime );
        }
        pMesh->setLocalPosition( _agent.getPosition() + float3{ 0.0f, _heightOffset, 0.0f } );
    }
} // namespace sw
