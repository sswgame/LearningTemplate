/**
 * @file CharacterCrowdSpawnerComponent.h
 * @brief 캐릭터 이동 부하 시나리오(`load/charactermove`)의 무대 — 플레이를 시작하면 캐릭터 컨트롤러(`CharacterControllerComponent`) N 개를 격자로 세워 가운데로 걷게 합니다.
 * @details 고정 스텝마다 물리 PreStep 끝의 캐릭터 일괄 이동(`IPhysicsCharacterScene3D::moveCharacters`, 감사 S-8)이 N 명을 옮긴다. 가운데로 모여 서로 밀어
 *          캐릭터 대 캐릭터 충돌 질의가 가장 무거운 꼴이 된다(유니티 DOTS Character Controller 의 군중 샘플과 같은 꼴). 세운 뒤에는 할 일이 없다(요청 속도는 남는다).
 *          새 게임을 시작할 때 `NavCrowdSpawnerComponent` 와 함께 지우는 파일이다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
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
} // namespace sw
