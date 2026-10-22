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
     * @brief 코드가 여는 근접 공격 판정입니다. 휘두르는 동안 오브젝트 기준 칼날(두 점과 두께)을 프레임마다 쓸어 걸린 오브젝트를 맞힙니다(2D 물리 씬).
     * @details 판정은 엔진의 근접 판정 한 길(`MeleeHitWindowUtil::sweep`, 애니메이션 알림 `HitWindow` 와 같다)이고, 이 컴포넌트는 칼날을 오브젝트 변환으로
     *          만들고 창을 열고 닫기만 한다. 칼날은 `setBlade` 의 두 점(이 오브젝트 기준)과 두께다. 칼 끝처럼 따로 움직일 판정은 자식 오브젝트에 붙인다.
     *          `beginAttack` 부터 `duration` 동안 창이 열리고, 그 사이 칼날에 걸린 오브젝트는 창마다 **한 번** 맞는다. 휘두르기 전부터 칼날 안에 있던 오브젝트는
     *          시작할 때, 휘두르는 동안 들어온 오브젝트는 그 프레임에 맞는다. 공격자 계층(이 오브젝트의 맨 위 조상과 그 아래 모두)은 맞지 않고, 맞음의 때린 쪽은
     *          그 맨 위 조상이다. 맞음은 `onHitReceived` 로 가고 유닛은 `UnitStatsComponent::onHitReceived` 에서 피해를 받는다(투사체와 같은 `takeDamage`).
     *          트리거(감지 범위)는 걸리지 않는다. 클라이언트 프로세스는 맞음을 전달하지 않는다(서버 권위, `MeleeHitWindowUtil::getProcessAuthority`).
     *          스윕은 틱 밖에서 돈다 — 틱(물리 뒤 단계)이 틱 직후로 미룬다(물리 질의와 `onHitReceived` 는 틱 밖의 일이다).
     */
    REFLECT()
    class SW_KIT_API MeleeHitboxComponent : public Component
    {
    public:
        REFLECT_BODY();
        MeleeHitboxComponent();
        virtual ~MeleeHitboxComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        bool  isAttackActive() const { return _bIsAttacking; }
        int32 getDamage() const { return _damage; }
        /** @brief 칼날을 정합니다. 두 점은 이 오브젝트 기준이고 @p radius 는 두께(0 이면 선)입니다. 두 점이 같으면 칼날이 없어 `beginAttack` 이 오류를 냅니다. */
        void setBlade( const float3& bladeStart, const float3& bladeEnd, float32 radius );
        /**
         * @brief 휘두르기를 시작합니다. @p duration 초 동안 판정이 켜지고, 0 이하면 다시 부를 때까지 켜져 있습니다.
         * @details 지금 칼날에 걸린 오브젝트는 이 호출 안에서 바로 맞는다. 틱 안에서 부르면 그 스윕은 틱 직후로 미뤄진다.
         */
        void beginAttack( int32 damage, float32 duration );

    private:
        /** @brief 공격자 계층의 맨 위 오브젝트입니다. 소유자가 없으면 nullptr 입니다. */
        GameObject* findAttackerRoot() const;
        /** @brief 지금 칼날의 표본점(월드)입니다. */
        void collectBlade( vector<float3>& outListPoint ) const;
        /** @brief 틱 밖이면 바로, 틱 안이면 틱 직후에 칼날을 씁니다. */
        void requestSweep();
        /** @brief 지난 칼날에서 지금 칼날까지 쓸어 맞히고, 지금 칼날을 다음 기준으로 넘깁니다. */
        void sweepBlade();

        /** @brief 이번 휘두르기에 이미 맞은 오브젝트입니다. `beginAttack` 이 비웁니다. */
        PROPERTY()
        vector<GameObjectHandle> _listHitTarget;
        /**
         * @brief 지난 스윕의 칼날 표본점(월드)입니다. 비었으면 지금 칼날만 씁니다.
         * @details 저장하지 않는다. 다시 연 판정은 지금 칼날부터 다시 쓸고, 이미 맞은 목록이 같은 오브젝트를 다시 맞히지 않게 한다.
         */
        vector<float3> _listPreviousPoint;
        PROPERTY()
        float3 _bladeStart;
        PROPERTY()
        float3 _bladeEnd;
        PROPERTY()
        float32 _bladeRadius;
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
