/**
 * @file ExplosiveBarrelComponent.h
 * @brief 피해가 쌓이면 터져 반경 안에 피해를 주는 폭발 드럼통(연쇄 폭발)입니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ExplosiveBarrelComponent
     * @brief 피해가 `_health` 를 넘으면 터집니다. `_radius` 안의 오브젝트에 거리 감쇠 피해를 주고(가까울수록 큼, 다른 드럼통도 터짐), "Explosion" 연출을 내고, 몸을 끕니다.
     * @details 터짐은 스텝(60 Hz) 끝에 한 번 일어난다. 연쇄 폭발은 다음 스텝으로 이어지므로 한 프레임에 무한히 번지지 않는다. `_fuseTime` 이 있으면 플레이 시작부터 그만큼
     *          지난 뒤 스스로 터진다(시한 폭탄, 쇼케이스). 반경에 닿은 파괴 오브젝트(자기 포함)에는
     *          중심 변형 `_fractureStrain` 과 충격량 `_blastImpulse` 의 폭발 피해를 주고, 자기에게 파괴 컴포넌트가 있으면 몸을 끄지 않고 조각으로 날아간다.
     *          반경 안 다른 오브젝트에 쓰는 일(`applyRadialDamage`)은 틱 뒤 게임 스레드에서 한다 — 병렬 틱에서 남의 오브젝트(파괴 조각)에 쓰지 않는다. 같은 프레임의 물리 앞이다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Explosive Barrel", Tooltip = "Explodes after enough damage, damaging everything in a radius (chains)" )
    class SW_GF_API ExplosiveBarrelComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( ExplosiveBarrelComponent );

        static constexpr float32 kTickInterval = constant::time::kDefaultFixedStepSeconds; ///< 고정 걸음마다 한 번 — 지연 · 간격을 걸음 수로 센다(프레임 시간과 무관하게 같은 걸음)

        ExplosiveBarrelComponent();
        virtual ~ExplosiveBarrelComponent() override = default;

        void    onTick( TickContext& context ) override;
        void    stepOnce();
        bool    hasExploded() const { return _bExploded; }
        float32 getDamageTaken() const { return _damageTaken; }

    private:
        void explode( GameObject& owner );
        /** @brief 터진 자리(`_blastCenter`)에서 반경 안 다른 오브젝트에 거리 감쇠 피해 · 파괴 오브젝트에 폭발 피해를 줍니다(게임 스레드, 틱 밖). */
        void applyRadialDamage( GameObjectManager& manager ) const;

    private:
        PROPERTY( Category = "Barrel", DisplayName = "Health", Min = 0.0 )
        float32 _health;
        PROPERTY( Category = "Barrel", DisplayName = "Radius", Min = 0.0, Units = m )
        float32 _radius;
        PROPERTY( Category = "Barrel", DisplayName = "Damage", Min = 0.0, Tooltip = "Damage at the centre; falls off linearly to the radius" )
        float32 _damage;
        PROPERTY( Category = "Barrel", DisplayName = "Fuse Time", Min = 0.0, Tooltip = "Explodes on its own this long after play starts (0 = only from damage)", Units = s )
        float32 _fuseTime;
        PROPERTY( Category = "Barrel", DisplayName = "Fracture Strain", Min = 0.0, Tooltip = "Strain at the centre for destructible (fractured) objects in the radius" )
        float32 _fractureStrain;
        PROPERTY( Category = "Barrel", DisplayName = "Blast Impulse", Min = 0.0, Tooltip = "Impulse given to pieces broken off by the blast", Units = "N*s" )
        float32 _blastImpulse;
        PROPERTY( RuntimeState, Category = "Barrel", DisplayName = "Damage Taken", Tooltip = "Runtime" )
        float32 _damageTaken;
        PROPERTY( RuntimeState, Category = "Barrel", DisplayName = "Exploded", Tooltip = "Runtime" )
        bool _bExploded;

        PROPERTY( RuntimeState, Category = "Barrel", DisplayName = "Fuse Steps Left", Tooltip = "Runtime: steps until the fuse burns down (-1: not counting yet)" )
        int32               _fuseStepsLeft;
        SW_TRANSIENT float3 _blastCenter; ///< 터진 자리 — 틱 뒤 `applyRadialDamage` 가 읽는다(틱 뒤 일은 캡처 없이 핸들로 다시 찾는다)
    };
} // namespace sw
