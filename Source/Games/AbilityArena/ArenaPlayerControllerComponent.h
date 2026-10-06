/**
 * @file ArenaPlayerControllerComponent.h
 * @brief 플레이어 유닛의 입력 — 키를 어빌리티 입력 번호로 넘기고 WASD 로 움직입니다. 자동 전투면 AI 가 대신 누릅니다.
 */
#pragma once
#include "Games/AbilityArena/ArenaControllerComponent.h"

namespace sw
{
    class InputManager;

    /**
     * @class ArenaPlayerControllerComponent
     * @brief 플레이어 프리팹의 컨트롤러입니다. 디렉터의 자동 전투(`isAutoPlayOn`)가 켜져 있거나 입력이 없으면 AI 로 돕니다.
     * @details 조작: WASD · 방향키 이동, J/Space 근접, K/2 화염구, L/3 회복, LeftShift/4 대시. 대시 중(`State.Dashing`)에는 입력과 상관없이
     *          바라보는 쪽으로 미끄러집니다(이동 속도 ×3 은 이펙트가 건다).
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Player Controller", Tooltip = "Keyboard (or auto play) control of the arena player" )
    class ArenaPlayerControllerComponent : public ArenaControllerComponent
    {
    public:
        REFLECT_BODY();

        ArenaPlayerControllerComponent();
        virtual ~ArenaPlayerControllerComponent() override = default;

    protected:
        void tickController( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem, float3& inoutPosition ) override;

    private:
        void tickInput( float32 deltaTime, const InputManager& input, AbilitySystemComponent& abilitySystem, float3& inoutPosition );
        /** @brief 반쯤 깎이면 회복, 가까우면 근접, 멀면 다가가며 화염구, 둘러싸이면 대시로 빠진다 — 입력 번호만 누른다. */
        void tickAutoPlay( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem, float3& inoutPosition );

    private:
        PROPERTY( Category = "Auto Play", DisplayName = "Crowd Radius", Tooltip = "Auto play counts enemies inside this distance as surrounding it", Min = 0.0, Units = m )
        float32 _crowdRadius;
        PROPERTY( Category = "Auto Play", DisplayName = "Crowd Count", Tooltip = "Auto play dashes away when this many enemies surround it", Min = 1 )
        uint32 _crowdCount;
        PROPERTY( Category = "Auto Play", DisplayName = "Melee Range", Tooltip = "Auto play switches to melee inside this distance", Min = 0.0, Units = m )
        float32 _meleeRange;
    };
} // namespace sw
