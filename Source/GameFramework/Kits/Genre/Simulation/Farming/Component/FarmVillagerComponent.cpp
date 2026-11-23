#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmVillagerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmVillageGameModeComponent.h"

namespace sw
{
    FarmVillagerComponent::FarmVillagerComponent()
        : _npcIndex{ -1 }
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

    void FarmVillagerComponent::assignVillager( GameObjectHandle gameMode, int32 npcIndex )
    {
        bindGameMode( gameMode );
        _npcIndex = npcIndex;
        _bPlaced  = false;
    }

    void FarmVillagerComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        const FarmVillageGameModeComponent* pGameMode         = findGameMode<FarmVillageGameModeComponent>();
        const bool                          bFollowsAVillager = pGameMode != nullptr && 0 <= _npcIndex && _npcIndex < pGameMode->getVillagerCount();
        if ( bFollowsAVillager == false )
            return;
        const float3 goal     = pGameMode->getVillagerPosition( _npcIndex );
        const bool   bTooFar  = float3::getHorizontalDistance( _agent.getPosition(), goal ) > pGameMode->getBodySnapDistance();
        const bool   bJumpsTo = _bPlaced == false || bTooFar;
        if ( bJumpsTo )
        {
            _agent.setPosition( goal );
            _bPlaced = true;
        }
        else
        {
            _agent.setSettings( pGameMode->makeBodySettings( _npcIndex ) );
            _listGoal[0] = goal;
            _agent.followPath( _listGoal );
            pGameMode->collectNeighborPositions( _npcIndex, _listNeighbor );
            _agent.update( pGameMode->getNavGrid(), _listNeighbor, context.getDeltaTime() );
        }
        pMesh->setLocalPosition( context.getManager().getWorldOrigin().toLocal( _agent.getPosition() + float3{ 0.0f, _heightOffset, 0.0f } ) );
    }
} // namespace sw
