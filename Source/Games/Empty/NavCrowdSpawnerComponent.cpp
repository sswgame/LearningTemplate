#include "pch.h"

#include "Games/Empty/NavCrowdSpawnerComponent.h"

#include "Core/Math/DeterministicRandom.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

#include "Games/Empty/NavWanderComponent.h"

namespace sw
{
    NavCrowdSpawnerComponent::NavCrowdSpawnerComponent()
        : _agentType{}
        , _agentCount{ 0 }
        , _halfExtent{ 20.0f }
        , _wanderRadius{ 6.0f }
        , _maxSpeed{ 3.5f }
        , _seed{ 1 }
        , _probeInterval{ 4 }
    {
    }

    void NavCrowdSpawnerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObjectManager* pManager = getOwner()->getManager();
        if ( pManager == nullptr || _agentCount == 0 )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( NavCrowdSpawnerComponent& self, GameObjectManager& manager )
        { self.spawnAgents( manager ); } );
    }

    void NavCrowdSpawnerComponent::spawnAgents( GameObjectManager& manager )
    {
        const SceneComponent* pScene  = getOwner()->getPrimarySceneComponent();
        const float3          center  = pScene != nullptr ? pScene->getWorldPosition() : float3{};
        const uint32          columns = static_cast<uint32>( MathUtil::ceil( MathUtil::sqrt( static_cast<float32>( _agentCount ) ) ) );
        const float32         spacing = columns > 0 ? ( 2.0f * _halfExtent ) / static_cast<float32>( columns ) : 0.0f;
        for ( uint32 index = 0; index < _agentCount; ++index )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "NavWanderer" ) );
            if ( pObject == nullptr )
                continue;
            SceneComponent*        pBody   = pObject->addComponent<SceneComponent>();
            NavMeshAgentComponent* pAgent  = pObject->addComponent<NavMeshAgentComponent>();
            NavWanderComponent*    pWander = pObject->addComponent<NavWanderComponent>();
            if ( pBody == nullptr || pAgent == nullptr || pWander == nullptr )
                continue;
            // 칸 가운데에 세운다 — 처음부터 겹치지 않게(군중이 밀어내는 데 프레임을 쓰지 않는다).
            const float32 offsetX = -_halfExtent + spacing * ( static_cast<float32>( index % columns ) + 0.5f );
            const float32 offsetZ = -_halfExtent + spacing * ( static_cast<float32>( index / columns ) + 0.5f );
            pBody->setLocalPosition( float3{ center._x + offsetX, center._y, center._z + offsetZ } );
            pAgent->setAgentType( _agentType );
            pAgent->setMaxSpeed( _maxSpeed );
            pAgent->setDriveMode( NavAgentDriveMode::Transform );
            pWander->setWander( center, _halfExtent, _wanderRadius, DeterministicRandom32::deriveSeed( _seed, index ), _probeInterval );
        }
    }
} // namespace sw
