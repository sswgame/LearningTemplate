#include "pch.h"

#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class LaunchVelocityComponent
     * @brief 첫 틱에 오브젝트의 월드 회전으로 돌린 `_localDirection` 쪽으로 `_speed` 의 선속도를 같은 오브젝트의 `RigidBodyComponent`(동적)에 겁니다(언리얼
     *        ProjectileMovement 의 InitialSpeed 를 물리 바디로 한 것).
     * @details 스폰한 쪽(알림 `SpawnPrefab`)은 스폰한 뒤에 월드 자세를 정하므로, 방향은 시작(`onBeginPlay`)이 아니라 첫 틱에 읽는다. 강체가 없거나 동적이 아니면
     *          오류로 알리고 날리지 않는다(조용히 제자리에 두지 않는다). 날아간 뒤의 자세는 물리가 정하되 `_alignGain` 이 0 보다 크면 날아가는 동안
     *          `_localDirection` 이 속도 쪽을 보도록 각속도를 건다(언리얼 ProjectileMovement 의 `bRotationFollowsVelocity` — 화살 머리가 처음 자세 그대로 날지 않게 한다).
     *          다른 파일이 이 형식을 쓰지 않아 헤더 없이 여기 둔다. 리플렉션 등록은 파일 끝의 `LaunchVelocityComponent.cpp.gen.inl` 이 한다.
     */
    REFLECT( Category = "Physics", DisplayName = "Launch Velocity", Tooltip = "Gives this object's dynamic rigid body an initial velocity along a local direction on its first tick (projectiles)" )
    class LaunchVelocityComponent : public Component
    {
    public:
        REFLECT_BODY();

        LaunchVelocityComponent();
        ~LaunchVelocityComponent() override = default;

        void onBeginPlay() override;
        /** @brief 첫 틱에 한 번 선속도를 겁니다. 그 뒤로는 할 일이 없습니다. */
        void onTick( TickContext& context ) override;

        /** @brief 이미 날렸으면 true 입니다(테스트와 진단). */
        bool hasLaunched() const { return _bLaunched; }

    private:
        /** @brief 날린 뒤 `_alignGain` 이 0 보다 크면 앞 방향(`_localDirection`)을 속도 쪽으로 돌리는 각속도를 건다. */
        void steerTowardVelocity();

        PROPERTY( Category = "Launch", DisplayName = "Speed", Tooltip = "Initial speed along the direction", Min = 0.0, Units = "m/s" )
        float32 _speed;
        PROPERTY( Category = "Launch", DisplayName = "Local Direction", Tooltip = "Launch direction in the object's local space (normalized when launching)", Meta = "Shape=Arrow" )
        float3 _localDirection;
        PROPERTY( Category = "Launch", DisplayName = "Align Gain", Tooltip = "After launch, steers the body's local direction toward its velocity (angular velocity = axis x gain; arrows point along their flight). 0 leaves the rotation to the physics", Min = 0.0, Meta = "Units=1/s" )
        float32 _alignGain;
        PROPERTY( RuntimeState, Category = "Launch", DisplayName = "Launched", Tooltip = "Runtime: the initial velocity was already given" )
        bool               _bLaunched; ///< 불러온 화살을 다시 날리지 않는다(속도는 강체의 상태가 싣는다)
        SW_TRANSIENT uint8 _bPending;  ///< 플레이가 시작돼 다음 틱에 날린다
    };

    LaunchVelocityComponent::LaunchVelocityComponent()
        : _speed{ 0.0f }
        , _localDirection{ 0.0f, 0.0f, 1.0f }
        , _alignGain{ 0.0f }
        , _bLaunched{ false }
        , _bPending{ SW_FALSE }
    {
    }

    void LaunchVelocityComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bPending = _bLaunched ? SW_FALSE : SW_TRUE;
    }

    void LaunchVelocityComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bPending == SW_FALSE )
        {
            steerTowardVelocity();
            return;
        }
        _bPending                    = SW_FALSE;
        GameObject*           pOwner = getOwner();
        RigidBodyComponent*   pBody  = pOwner != nullptr ? pOwner->getComponent<RigidBodyComponent>() : nullptr;
        const SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool            bReady = pBody != nullptr && pRoot != nullptr && pBody->getBodyType() == PhysicsBodyType::Dynamic;
        if ( bReady == false )
        {
            SW_LOG_ERROR_ID( "LaunchVelocity.LaunchVelocityComponentNeedsDynamicRigidBodyComponent", "'%#': LaunchVelocityComponent needs a dynamic RigidBodyComponent on the same object - nothing was launched",
                             pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
            return;
        }
        const float32 length = _localDirection.getLength();
        if ( length <= MathUtil::kEpsilon )
        {
            SW_LOG_ERROR_ID( "LaunchVelocity.LaunchVelocityComponentLocalDirectionZeroNothing", "'%#': LaunchVelocityComponent _localDirection is zero - nothing was launched", pOwner->getName().c_str() );
            return;
        }
        const float3 worldDirection = float3::transformVector( _localDirection / length, pRoot->getWorldMatrix() );
        const float3 velocity       = worldDirection * ( _speed / MathUtil::max( worldDirection.getLength(), MathUtil::kEpsilon ) );
        pBody->setLinearVelocity( velocity );
        _bLaunched = true;
    }

    void LaunchVelocityComponent::steerTowardVelocity()
    {
        // 날린 뒤에만, 정렬을 켠 것만 한다. 속도가 거의 없으면(박히거나 멈춘 화살) 방향이 정해지지 않으므로 건드리지 않는다.
        constexpr float32 kMinAlignSpeed = 0.5f;
        if ( _bLaunched == false || _alignGain <= 0.0f )
            return;
        GameObject*           pOwner        = getOwner();
        RigidBodyComponent*   pBody         = pOwner != nullptr ? pOwner->getComponent<RigidBodyComponent>() : nullptr;
        const SceneComponent* pRoot         = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool            bHasAwakeBody = pBody != nullptr && pRoot != nullptr && pBody->isSleeping() == false;
        if ( bHasAwakeBody == false )
            return;
        const float3  velocity = pBody->getLinearVelocity();
        const float32 speed    = velocity.getLength();
        const float32 length   = _localDirection.getLength();
        if ( speed < kMinAlignSpeed || length <= MathUtil::kEpsilon )
            return;
        const float3 forward = float3::transformVector( _localDirection / length, pRoot->getWorldMatrix() ).normalize();
        // 앞 방향에서 속도 방향으로 돌리는 축 × 이득. 어긋난 각이 클수록 빠르게 돌고 맞으면 0 이라 진동하지 않는다(물리가 각속도를 이어 받는다).
        pBody->setAngularVelocity( forward.cross( velocity / speed ) * _alignGain );
    }
} // namespace sw

#include "LaunchVelocityComponent.cpp.gen.inl"
