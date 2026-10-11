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
     * @class MeleeHitboxComponent
     * @brief 휘두르는 동안 같은 오브젝트의 콜라이더에 닿은 유닛에게 피해를 주는 공격 판정입니다(언리얼의 근접 히트박스처럼 공격 창 동안의 겹침을 보고 `ApplyDamage` 를 부릅니다).
     * @details 판정 모양은 같은 오브젝트의 `BoxCollider2DComponent` 로 정한다. 칼 끝처럼 따로 둘 판정은 자식 오브젝트에 콜라이더와 이 컴포넌트를 함께 붙인다.
     *          `beginAttack` 부터 `duration` 동안 판정이 켜지고, 그 사이에 닿은 유닛(`UnitStatsComponent`)마다 **한 번** 피해를 준다. 휘두르기 전부터 판정 안에 서 있던 유닛은 시작할 때, 휘두르는 동안 들어온 유닛은 들어올 때 맞는다.
     *          공격자 계층(이 오브젝트의 맨 위 조상과 그 아래 모든 오브젝트)은 맞지 않고, 피해 이벤트의 `_instigator` 는 그 맨 위 조상이다. 피해는 투사체와 같은 `UnitStatsComponent::takeDamage` 를 지난다.
     *          상대의 트리거(감지 범위)와 겹친 것은 치지 않는다. 판정은 상대의 막는 콜라이더(몸)에 닿아야 맞는다.
     *          이쪽 판정 콜라이더는 트리거여도 된다. 언리얼 히트박스가 Overlap 반응인 것과 같고, 그러면 총알도 이 판정에 막히지 않는다.
     */
    REFLECT()
    class SW_GF_API MeleeHitboxComponent : public Component
    {
    public:
        REFLECT_BODY();
        MeleeHitboxComponent();
        virtual ~MeleeHitboxComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;
        /** @brief 겹친 것을 기억하고, 휘두르는 중이면 그 유닛에게 피해를 줍니다. 상대의 트리거는 치지 않습니다. */
        void onOverlapBegin( const OverlapInfo& overlap ) override;
        /** @brief 떨어진 것을 잊습니다. 상대가 사라졌으면(nullptr) 풀리지 않는 접촉을 모두 제거합니다. */
        void onOverlapEnd( const OverlapInfo& overlap ) override;

        bool  isAttackActive() const { return _bIsAttacking; }
        int32 getDamage() const { return _damage; }
        /**
         * @brief 지금 판정과 겹친 접촉 수입니다. 콜라이더 쌍 하나가 접촉 하나이므로, 상대 하나가 콜라이더 둘로 겹치면 2입니다.
         * @details 사라진 상대는 끝 이벤트(상대 없음)에서 제거한다. 남겨 두면 휘두를 때마다 확인하고 버리는 일이 반복되어 목록이 줄지 않는다.
         */
        uint32 getOverlapCount() const { return static_cast<uint32>( _listOverlapping.size() ); }
        /**
         * @brief 휘두르기를 시작합니다. @p duration 초 동안 판정이 켜지고, 0 이하면 다시 부를 때까지 켜져 있습니다.
         * @details 지금 겹쳐 있는 유닛은 이 호출 안에서 바로 맞는다. 틱 안에서 불러도 되며, 그 경우 피해는 틱 직후로 미뤄진다(`takeDamage`).
         */
        void beginAttack( int32 damage, float32 duration );

    private:
        /** @brief 공격자 계층의 맨 위 오브젝트입니다. 소유자가 없으면 nullptr 입니다. */
        GameObject* findAttackerRoot() const;
        /** @brief @p pTarget 이 공격자 계층 밖의 유닛이고 이번 휘두르기에 아직 맞지 않았으면 피해를 줍니다. */
        void deliverHit( GameObject* pTarget );

        /** @brief 이번 휘두르기에 이미 맞은 유닛입니다. `beginAttack` 이 비웁니다. */
        PROPERTY()
        vector<GameObjectHandle> _listHitTarget;
        /**
         * @brief 지금 판정 콜라이더와 겹친 오브젝트입니다(겹침 이벤트가 채웁니다). 접촉마다 하나씩이라서, 같은 상대가 콜라이더 둘로 겹치면 두 번 들어 있습니다.
         * @details 저장하지 않는다. 이 값은 물리가 만드는 값이라서, 오브젝트를 다시 만들면(되돌리기, 핫 리로드) 바디가 새로 생기면서 시작 이벤트가 다시 온다.
         */
        vector<GameObjectHandle> _listOverlapping;
        PROPERTY()
        int32 _damage;
        PROPERTY()
        float32 _duration;
        PROPERTY()
        float32 _currentDuration;
        PROPERTY()
        bool _bIsAttacking;
    };
} // namespace sw
