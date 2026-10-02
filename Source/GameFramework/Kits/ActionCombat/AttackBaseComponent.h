#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class AttackBaseComponent
     * @brief 휘두르는 동안 같은 오브젝트의 콜라이더에 닿은 유닛에 피해를 주는 공격 판정입니다(언리얼 근접 히트박스 — 공격 창 동안의 겹침 + `ApplyDamage`).
     * @details 판정 모양은 같은 오브젝트의 `BoxCollider2DComponent` 입니다 — 칼 끝처럼 따로 둘 판정은 자식 오브젝트에 콜라이더와 이것을 함께 붙입니다.
     *          `beginAttack` 부터 `duration` 동안 판정이 켜지고, 그 사이 닿은 유닛(`UnitStatsComponent`)마다 **한 번** 피해를 줍니다 — 휘두르기 전부터
     *          판정 안에 서 있던 유닛도(시작 때), 휘두르는 동안 들어온 유닛도(들어올 때). 공격자 계층(이 오브젝트의 맨 위 조상과 그 아래 모두)은
     *          맞지 않고, 피해 이벤트의 `_instigator` 는 그 맨 위 조상입니다. 피해는 투사체와 같은 `UnitStatsComponent::takeDamage` 를 지납니다.
     */
    REFLECT()
    class SW_GF_API AttackBaseComponent : public Component
    {
    public:
        REFLECT_BODY();
        AttackBaseComponent();
        virtual ~AttackBaseComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;
        /** @brief 겹친 것을 기억하고, 휘두르는 중이면 그 유닛에 피해를 줍니다. */
        void onOverlapBegin( GameObject* pOther ) override;
        /** @brief 떨어진 것을 잊습니다. 상대가 사라졌으면(nullptr) 풀리지 않는 것을 모두 덜어 냅니다. */
        void onOverlapEnd( GameObject* pOther ) override;

        bool  isAttackActive() const { return _bIsAttacking; }
        int32 getDamage() const { return _damage; }
        /**
         * @brief 휘두르기를 시작합니다 — @p duration 초 동안 판정이 켜집니다(0 이하면 다시 부를 때까지 켜져 있습니다).
         * @details 지금 겹쳐 있는 유닛은 이 자리에서 맞습니다. 틱 안에서 불러도 됩니다 — 피해는 틱 직후로 미뤄집니다(`takeDamage`).
         */
        void beginAttack( int32 damage, float32 duration );

    private:
        /** @brief 공격자 계층의 맨 위 오브젝트입니다(소유자가 없으면 nullptr). */
        GameObject* findAttackerRoot() const;
        /** @brief @p pTarget 이 공격자 계층 밖의 유닛이고 이번 휘두르기에 아직 안 맞았으면 피해를 줍니다. */
        void deliverHit( GameObject* pTarget );

        /** @brief 이번 휘두르기에 이미 맞은 유닛입니다. `beginAttack` 이 비웁니다. */
        PROPERTY( Alias = "hitTarget" )
        vector<GameObjectHandle> _listHitTarget;
        /**
         * @brief 지금 판정 콜라이더와 겹친 오브젝트입니다(겹침 이벤트가 채운다).
         * @details 저장하지 않습니다 — 물리가 내는 값이라, 오브젝트가 다시 만들어지면(되돌리기 · 핫 리로드) 바디가 새로 들며 시작 이벤트가 다시 옵니다.
         */
        vector<GameObjectHandle> _listOverlapping;
        PROPERTY( Alias = "damage" )
        int32 _damage;
        PROPERTY( Alias = "duration" )
        float32 _duration;
        PROPERTY( Alias = "currentDuration" )
        float32 _currentDuration;
        PROPERTY( Alias = "bIsAttacking" )
        bool _bIsAttacking;
    };
} // namespace sw
