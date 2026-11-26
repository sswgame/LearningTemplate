#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class CharacterCrowdSpawnerComponent
     * @brief 캐릭터 N 개를 이 오브젝트 둘레의 상자에 세워 가운데로 걷게 합니다. 파일 머리말 참고.
     */
    REFLECT()
    class CharacterCrowdSpawnerComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( CharacterCrowdSpawnerComponent );

        CharacterCrowdSpawnerComponent();
        ~CharacterCrowdSpawnerComponent() override = default;

        void onBeginPlay() override;

        /** @brief 세운 캐릭터 수입니다(부하 시나리오가 측정 경로를 탔는지 본다). */
        uint32 getSpawnedCount() const { return _spawnedCount; }

    private:
        /** @brief 캐릭터를 격자로 세웁니다(구조 변경 — 틱 밖에서 돈다). */
        void spawnCharacters( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Crowd", Min = 0, Tooltip = "How many character controllers to spawn" )
        uint32 _characterCount;
        PROPERTY( Category = "Crowd", Min = 0.0, Tooltip = "Half size of the box the characters spawn in, around this object", Units = m )
        float32 _halfExtent;
        PROPERTY( Category = "Crowd", Min = 0.0, Tooltip = "Walking speed toward the center", Units = "m/s" )
        float32 _walkSpeed;

        SW_TRANSIENT uint32 _spawnedCount;
    };

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

#include "CharacterCrowdSpawnerComponent.cpp.gen.inl"
