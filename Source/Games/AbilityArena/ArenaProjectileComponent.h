/**
 * @file ArenaProjectileComponent.h
 * @brief 날아가는 투사체 하나 — 자기 오브젝트를 옮기고, 디렉터의 유닛 모습에서 처음 닿은 적대 유닛에 이펙트를 겁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Gameplay/Ability/GameplayEffect.h"

namespace sw
{
    /**
     * @class ArenaProjectileComponent
     * @brief 투사체 프리팹의 컴포넌트입니다. 디렉터가 틱 뒤에 세우고 `launch` 로 쏩니다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 자기 오브젝트만 옮깁니다. 맞은 판정은 디렉터가 `PrePhysics` 에서 적은 유닛 모습을 읽고, 이펙트는
     *          틱 뒤(게임 스레드)에 대상 어빌리티 시스템에 겁니다 — 대상은 다른 워커가 틱하고 있을 수 있다. 다 날았거나 맞으면 자기 오브젝트를 지웁니다.
     *          스펙은 런타임 상태라 저장하지 않습니다(상태 저장 전에 디렉터가 투사체를 걷는다).
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Projectile", Tooltip = "Moves itself and applies its effect specs to the first hostile unit it touches" )
    class ArenaProjectileComponent : public Component
    {
    public:
        REFLECT_BODY();

        ArenaProjectileComponent();
        virtual ~ArenaProjectileComponent() override = default;

        void onTick( float32 deltaTime ) override;

        /** @brief 쏩니다. @p position 은 발 높이(맞음 판정)이고 모습은 `_visualLift` 만큼 위에 그린다. */
        void launch( GameObjectHandle director, const float3& position, const float3& velocity, float32 range, const GameplayEffectSpec& spec,
                     const GameplayEffectSpec& extraSpec, bool bFromPlayer );

    private:
        /** @brief 닿은 유닛에 이펙트를 틱 뒤로 미뤄 겁니다. */
        void applyHit( GameObjectHandle target ) const;
        void finish();

    private:
        PROPERTY( Category = "Projectile", DisplayName = "Director", Tooltip = "Object with the ArenaDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Projectile", DisplayName = "Radius", Tooltip = "Hit radius", Min = 0.0, Units = m )
        float32 _radius;
        PROPERTY( Category = "Projectile", DisplayName = "Unit Radius", Tooltip = "Body radius of the units it can hit", Min = 0.0, Units = m )
        float32 _unitRadius;
        PROPERTY( Category = "Projectile", DisplayName = "Visual Lift", Tooltip = "Drawn this high above the hit point (chest height)", Units = m )
        float32 _visualLift;

        GameplayEffectSpec _spec;
        GameplayEffectSpec _extraSpec;
        float3             _position;
        float3             _velocity;
        float32            _remainingRange;
        uint8              _bFromPlayer : 1;
        uint8              _bLaunched   : 1;
        uint8              _bFinished   : 1;
        uint8              _reserved    : 5;
    };
} // namespace sw
