#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/Math.h"

#include "Engine/Character/Hit/MeleeHitWindow.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/SceneTeleportCursor.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class MeleeHitboxComponent
     * @brief 코드가 여는 근접 공격 판정입니다. 휘두르는 동안 오브젝트 기준 칼날(두 점과 두께)을 프레임마다 쓸어 걸린 오브젝트를 맞힙니다(2D 물리 씬).
     * @details 판정은 엔진의 근접 판정 한 길(`MeleeHit::sweep`, 애니메이션 알림 `HitWindow` 와 같다)이고, 이 컴포넌트는 칼날을 오브젝트 변환으로
     *          만들고 창을 열고 닫기만 한다. 칼날은 `setBlade` 의 두 점(이 오브젝트 기준)과 두께다. 칼 끝처럼 따로 움직일 판정은 자식 오브젝트에 붙인다.
     *          `beginAttack` 부터 `duration` 동안 창이 열리고, 그 사이 칼날에 걸린 오브젝트는 창마다 한 번 맞는다. 휘두르기 전부터 칼날 안에 있던 오브젝트는
     *          시작할 때, 휘두르는 동안 들어온 오브젝트는 그 프레임에 맞는다. 공격자 계층(이 오브젝트의 맨 위 조상과 그 아래 모두)은 맞지 않고, 맞음의 때린 쪽은
     *          그 맨 위 조상이다. 맞음은 맞음 사건(`GameObjectEvents::_hitReceived`)으로 가고 유닛은 `AbilitySystemComponent::receiveHit` 에서 피해를 받는다(투사체 · 장치와 같은 피해 이펙트).
     *          트리거(감지 범위)는 걸리지 않는다. 클라이언트 프로세스는 맞음을 전달하지 않는다(서버 권위, `MeleeHit::getProcessAuthority`).
     *          스윕은 틱 밖에서 돈다 — 틱(물리 뒤 단계)이 틱 직후로 미룬다(물리 질의와 맞음 사건은 틱 밖의 일이다).
     */
    REFLECT()
    class MeleeHitboxComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostPhysics; ///< 물리가 이번 프레임 위치로 옮긴 뒤에 쓴다.
        MeleeHitboxComponent();
        virtual ~MeleeHitboxComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 지난 칼날 자리를 옮깁니다 — 옮기지 않으면 다음 쓸기가 옛 원점에서 지금 자리까지 월드를 가로질러 맞힌다. */
        void onWorldOriginShifted( const float3& delta ) override;

        bool  isAttackActive() const { return _bIsAttacking; }
        int32 getDamage() const { return _damage; }
        /** @brief 칼날을 정합니다. 두 점은 이 오브젝트 기준이고 @p radius 는 두께(0 이면 선)입니다. 두 점이 같으면 칼날이 없어 `beginAttack` 이 오류를 냅니다. */
        void setBlade( const float3& bladeStart, const float3& bladeEnd, float32 radius );
        /**
         * @brief 휘두르기를 시작합니다. @p duration 초 동안 판정이 켜지고, 0 이하면 다시 부를 때까지 켜져 있습니다.
         * @details 지금 칼날에 걸린 오브젝트는 이 호출 안에서 바로 맞는다. 틱 안에서 부르면 그 스윕은 틱 직후로 미뤄진다.
         */
        FUNCTION( Category = "Combat", Exec )
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
        SW_TRANSIENT vector<float3> _listPreviousPoint;
        SW_TRANSIENT vector<float3> _listBladeScratch; ///< 이번 스윕의 표본점을 모으는 버퍼 — 끝나면 지난 표본점과 맞바꾼다(틱마다 새 벡터를 만들지 않는다)
        /** @brief 순간이동하면 지난 칼날을 잊는다 — 지나가지 않은 길을 쓸지 않는다. */
        SW_TRANSIENT SceneTeleportCursor _teleport;
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

    namespace
    {
        /** @brief 칼날 표본점 수입니다. 두 끝과 가운데 — 칼날이 돌며 지나간 자리를 끝점 둘만으로는 놓친다. */
        constexpr uint32 kMeleeHitboxBladeSampleCount = 3;
    } // namespace
} // namespace sw

namespace sw
{
    MeleeHitboxComponent::MeleeHitboxComponent()
        : _listHitTarget{}
        , _listPreviousPoint{}
        , _listBladeScratch{}
        , _teleport{}
        , _bladeStart{}
        , _bladeEnd{}
        , _bladeRadius{ 0.0f }
        , _damage{ 0 }
        , _duration{ 0.0f }
        , _currentDuration{ 0.0f }
        , _bIsAttacking{ false }
    {
    }

    void MeleeHitboxComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        _listPreviousPoint.clear();
    }

    void MeleeHitboxComponent::onTick( TickContext& context )
    {
        SceneComponent::onTick( context );

        if ( _bIsAttacking == false )
            return;

        requestSweep();
        _currentDuration += context.getDeltaTime().count();
        if ( _duration > 0.0f && _currentDuration >= _duration )
        {
            _bIsAttacking    = false;
            _currentDuration = 0.0f;
        }
    }

    void MeleeHitboxComponent::setBlade( const float3& bladeStart, const float3& bladeEnd, float32 radius )
    {
        _bladeStart  = bladeStart;
        _bladeEnd    = bladeEnd;
        _bladeRadius = radius;
    }

    void MeleeHitboxComponent::beginAttack( int32 damage, float32 duration )
    {
        if ( ( _bladeEnd - _bladeStart ).getLengthSquared() <= 1.0e-12f )
        {
            SW_LOG_ERROR_ID( "MeleeHitbox.MeleeHitboxComponentBladeNoLength", "MeleeHitboxComponent: blade has no length - call setBlade() with two different points before beginAttack()" );
            return;
        }
        _damage          = damage;
        _duration        = duration;
        _currentDuration = 0.0f;
        _bIsAttacking    = true;
        _listHitTarget.clear();
        _listPreviousPoint.clear();
        // 휘두르기 전부터 칼날 안에 있던 오브젝트도 맞는다.
        requestSweep();
    }

    void MeleeHitboxComponent::onWorldOriginShifted( const float3& delta )
    {
        for ( float3& point : _listPreviousPoint )
        {
            point += delta;
        }
    }

    GameObject* MeleeHitboxComponent::findAttackerRoot() const
    {
        GameObject* pRoot = getOwner();
        while ( pRoot != nullptr && pRoot->getParent() != nullptr )
        {
            pRoot = pRoot->getParent();
        }
        return pRoot;
    }

    void MeleeHitboxComponent::collectBlade( vector<float3>& outListPoint ) const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? this : nullptr;
        const float4x4        world  = pScene != nullptr ? pScene->getWorldMatrix() : float4x4::Identity;
        MeleeHit::makeBlade( float3::transform( _bladeStart, world ), float3::transform( _bladeEnd, world ), kMeleeHitboxBladeSampleCount, outListPoint );
    }

    void MeleeHitboxComponent::requestSweep()
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( MeleeHitboxComponent& self, GameObjectManager& manager )
        {
            (void)manager;
            self.sweepBlade();
        } );
    }

    void MeleeHitboxComponent::sweepBlade()
    {
        GameObject* pAttacker = findAttackerRoot();
        if ( pAttacker == nullptr )
            return;
        if ( _teleport.consume( *this ).has_value() )
            _listPreviousPoint.clear();
        vector<float3>& listCurrent = _listBladeScratch;
        listCurrent.clear();
        collectBlade( listCurrent );
        MeleeHitParams params;
        params._radius = _bladeRadius;
        params._damage = static_cast<float32>( _damage );
        params._bIs2D  = true;
        vector<MeleeHit> listHit;
        (void)MeleeHit::sweep( *pAttacker, _listPreviousPoint, listCurrent, params, MeleeHit::getProcessAuthority(), _listHitTarget, listHit );
        _listPreviousPoint.swap( listCurrent ); // 지난 버퍼는 다음 스윕이 다시 쓴다
    }
} // namespace sw

#include "MeleeHitboxComponent.cpp.gen.inl"
