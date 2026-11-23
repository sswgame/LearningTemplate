#include "pch.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Client/ArenaDeathSquashComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaUnitComponent.h"

namespace sw
{
    ArenaDeathSquashComponent::ArenaDeathSquashComponent()
        : _minSquashRatio{ 0.05f }
        , _spawnScale{}
    {
        setCanEverTick( true );
    }

    void ArenaDeathSquashComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const GameObject* const         pOwner = getOwner();
        const ArenaUnitComponent* const pUnit  = pOwner != nullptr ? pOwner->getComponent<ArenaUnitComponent>() : nullptr;
        if ( pUnit != nullptr && pUnit->getDeathRemainRatio() < 1.0f )
            squash( pUnit->getDeathRemainRatio() );
    }

    SpawnStateReset ArenaDeathSquashComponent::resetToSpawnState()
    {
        GameObject* const    pOwner = getOwner();
        MeshComponent* const pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( _spawnScale.has_value() && pMesh != nullptr )
            pMesh->setLocalScale( *_spawnScale );
        _spawnScale.reset();
        return SpawnStateReset::Reset;
    }

    void ArenaDeathSquashComponent::squash( float32 remainRatio )
    {
        GameObject* const    pOwner = getOwner();
        MeshComponent* const pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        if ( _spawnScale.has_value() == false )
            _spawnScale = pMesh->getLocalScale();
        pMesh->setLocalScale( float3{ _spawnScale->_x, _spawnScale->_y * MathUtil::max( _minSquashRatio, remainRatio ), _spawnScale->_z } );
    }
} // namespace sw
