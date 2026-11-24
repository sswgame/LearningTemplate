#include "pch.h"

#include "Games/Playground/CharacterCrowdSpawnerComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

namespace sw
{
    namespace
    {
        struct CharacterCrowdSpawnerInternal
        {
            /** @brief 발을 바닥 위로 띄워 세우는 높이(m)입니다 — 처음 스텝에 바닥에 묻히지 않게. */
            static constexpr float32 kFootClearance = 0.05f;

            /** @brief 씬의 무대마다 세운 캐릭터 수를 더합니다. 씬이 없으면 false 입니다. */
            [[nodiscard]] static bool sumSpawned( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint64 total = 0;
                for ( const CharacterCrowdSpawnerComponent* pSpawner : pManager->getComponentRegistry().getAll<CharacterCrowdSpawnerComponent>() )
                {
                    total += pSpawner->getSpawnedCount();
                }
                outValue = static_cast<float64>( total );
                return true;
            }
        };
    } // namespace

    CharacterCrowdSpawnerComponent::CharacterCrowdSpawnerComponent()
        : _characterCount{ 0 }
        , _halfExtent{ 20.0f }
        , _walkSpeed{ 1.5f }
        , _spawnedCount{ 0 }
    {
    }

    void CharacterCrowdSpawnerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObjectManager* pManager = getOwner()->getManager();
        if ( pManager == nullptr || _characterCount == 0 )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( CharacterCrowdSpawnerComponent& self, GameObjectManager& manager )
        { self.spawnCharacters( manager ); } );
    }

    void CharacterCrowdSpawnerComponent::spawnCharacters( GameObjectManager& manager )
    {
        const SceneComponent* pScene  = getOwner()->getPrimarySceneComponent();
        const float3          center  = pScene != nullptr ? pScene->getWorldPosition() : float3{};
        const uint32          columns = static_cast<uint32>( MathUtil::ceil( MathUtil::sqrt( static_cast<float32>( _characterCount ) ) ) );
        const float32         spacing = columns > 0 ? ( 2.0f * _halfExtent ) / static_cast<float32>( columns ) : 0.0f;
        for ( uint32 index = 0; index < _characterCount; ++index )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "CrowdCharacter" ) );
            if ( pObject == nullptr )
                continue;
            CharacterControllerComponent* pCharacter = pObject->addComponent<CharacterControllerComponent>();
            if ( pCharacter == nullptr )
                continue;
            // 칸 가운데에 발을 바닥 조금 위로 세운다 — 처음부터 겹치지 않게.
            const float32 offsetX = -_halfExtent + spacing * ( static_cast<float32>( index % columns ) + 0.5f );
            const float32 offsetZ = -_halfExtent + spacing * ( static_cast<float32>( index / columns ) + 0.5f );
            pCharacter->setLocalPosition( float3{ center._x + offsetX, center._y + CharacterCrowdSpawnerInternal::kFootClearance, center._z + offsetZ } );
            const float32 distance = MathUtil::sqrt( offsetX * offsetX + offsetZ * offsetZ );
            const float32 scale    = distance > 0.0f ? _walkSpeed / distance : 0.0f;
            pCharacter->setMoveVelocity( float3{ -offsetX * scale, 0.0f, -offsetZ * scale } );
            ++_spawnedCount;
        }
    }

    SW_AUTOMATION_WATCH( playgroundCrowdCharacters, "PlaygroundLoad.CrowdCharacters", "Character controllers the crowd spawners placed (all spawners)",
                         &CharacterCrowdSpawnerInternal::sumSpawned );
} // namespace sw
