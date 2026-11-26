#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Reflection/ReflectionMacros.h"

#include "Games/Playground/Dev/ScenarioTest/NavWanderComponent.h"

namespace sw
{
    /**
     * @class NavCrowdSpawnerComponent
     * @brief 떠돌이 에이전트 N 개를 이 오브젝트 둘레의 집 상자에 세웁니다. 파일 머리말 참고.
     */
    REFLECT()
    class NavCrowdSpawnerComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavCrowdSpawnerComponent();
        ~NavCrowdSpawnerComponent() override = default;

        void onBeginPlay() override;

    private:
        /** @brief 에이전트를 격자로 세웁니다(구조 변경 — 틱 밖에서 돈다). */
        void spawnAgents( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Crowd", DisplayName = "Agent Type", Tooltip = "Navmesh agent kind of the spawned agents" )
        hashed_string _agentType;
        PROPERTY( Category = "Crowd", Min = 0, Tooltip = "How many wandering agents to spawn" )
        uint32 _agentCount;
        PROPERTY( Category = "Crowd", Min = 0.0, Tooltip = "Half size of the box the agents spawn and wander in, around this object", Units = m )
        float32 _halfExtent;
        PROPERTY( Category = "Crowd", Min = 0.0, Tooltip = "Farthest a new destination lies from an agent", Units = m )
        float32 _wanderRadius;
        PROPERTY( Category = "Crowd", Min = 0.0, Tooltip = "Top speed of each agent", Units = "m/s" )
        float32 _maxSpeed;
        PROPERTY( Category = "Crowd", Tooltip = "Seed of the destination sequences (agent index is mixed in)" )
        uint32 _seed;
        PROPERTY( Category = "Crowd", Tooltip = "Every Nth new destination of an agent also asks an async path query (0 = never)" )
        uint32 _probeInterval;
    };

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
        const GameObjectManager* pManager = getOwner()->getManager();
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

#include "NavCrowdSpawnerComponent.cpp.gen.inl"
