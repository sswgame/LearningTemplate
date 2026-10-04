#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ProjectileComponent
     * @brief 월드 속도로 날다가 닿은 것에 피해를 주고 사라지는 투사체입니다(언리얼 `ProjectileMovement` + `OnComponentHit` + `ApplyDamage`).
     * @details 맞음은 **같은 오브젝트의 `BoxCollider2DComponent` 겹침**(`onOverlapBegin`)으로 압니다. 시작할 때 그 콜라이더를 연속 충돌로
     *          켜므로(`BoxCollider2DComponent::setContinuous`), 한 프레임에 얇은 적을 건너뛸 만큼 빨라도 지나간 길에서 맞습니다. 콜라이더가 없으면
     *          날기만 하므로 시작할 때 경고합니다(프리팹을 고쳐야 한다).
     *
     *          닿은 것마다(먼저 닿은 것부터 — `OverlapInfo::_time`):
     *          - 상대 콜라이더가 트리거면 지나칩니다(감지 범위 · 구역 볼륨이 총알을 먹지 않는다 — 유니티 `isTrigger` · 언리얼 Overlap 반응).
     *          - 쏜 쪽(`setInstigator`)과 거기 붙은 오브젝트는 지나칩니다 — 총구에서 나온 총알이 쏜 몸에 맞지 않습니다.
     *          - 다른 투사체는 **요격탄만** 맞힙니다(`setInterceptor` — 언리얼의 채널별 충돌 반응). 같은 쪽이 쏜 것끼리는 요격탄이어도 지나칩니다
     *            (한 자리에서 퍼지는 산탄). 요격한 투사체는 사라집니다.
     *          - `UnitStatsComponent` 가 있으면 `takeDamage( 피해, 쏜 쪽 )` 을 한 번 부릅니다(무적 · 방어력 · 이벤트는 거기서).
     *          - 그다음 사라집니다. 관통 수(`setPierceCount`)가 남았으면 유닛 · 요격한 투사체를 맞혀도 하나 줄이고 계속 납니다.
     *          스탯이 없는 것(벽)도 레이어가 부딪히게 두었으면 투사체를 멈춥니다 — 무엇에 막힐지는 레이어 행렬(`CollisionLayers`)이 정합니다.
     */
    REFLECT()
    class SW_GF_API ProjectileComponent : public Component
    {
    public:
        REFLECT_BODY();
        ProjectileComponent();
        virtual ~ProjectileComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;
        /** @brief 맞음 처리입니다(클래스 설명의 규칙). 물리 step 뒤 게임 스레드에서 불리고, 사라짐은 같은 틱 끝에 놓입니다. */
        void onOverlapBegin( const OverlapInfo& overlap ) override;

        void setVelocity( const float2& velocity );
        /** @brief 맞은 유닛에 줄 피해입니다(방어력을 빼기 전). */
        int32 getDamage() const { return _damage; }
        void  setDamage( int32 damage );
        void  setLifeTime( float32 lifeTime );
        /**
         * @brief 쏜 쪽입니다. 이 오브젝트와 거기 붙은 것은 맞지 않고, 피해 이벤트의 `_instigator` 로 실립니다.
         * @details 핸들로 듭니다 — 쏜 쪽이 먼저 사라져도 날아가는 총알이 매달린 포인터를 쥐지 않습니다.
         */
        GameObjectHandle getInstigator() const { return _instigator; }
        void             setInstigator( GameObjectHandle instigator );
        /** @brief 사라지지 않고 더 꿰뚫을 수 있는 수입니다. 유닛 · 요격한 투사체를 맞힐 때마다 하나 줄고, 0 이면 다음 맞음에 사라집니다. */
        int32 getPierceCount() const { return _pierceCount; }
        void  setPierceCount( int32 pierceCount );
        /**
         * @brief 다른 쪽이 쏜 투사체를 맞혀 지우는 요격탄인지입니다(기본 아님 — 언리얼의 투사체 채널 반응을 Block 으로 둔 것).
         * @details 같은 쪽이 쏜 것끼리는 이 값과 무관하게 지나칩니다.
         */
        bool isInterceptor() const { return _bInterceptor; }
        void setInterceptor( bool bInterceptor );

    private:
        /** @brief @p other 를 요격하는지 — 요격탄이고, 다른 쪽이 쏜 것일 때입니다(같은 instigator 끼리는 산탄이다). */
        bool canIntercept( const ProjectileComponent& other ) const;

        PROPERTY()
        float2 _velocity;
        PROPERTY()
        GameObjectHandle _instigator;
        PROPERTY()
        int32 _damage;
        PROPERTY()
        int32 _pierceCount;
        PROPERTY()
        float32 _lifeTime;
        PROPERTY()
        float32 _currentLife;
        PROPERTY()
        bool _bInterceptor;
    };
} // namespace sw
