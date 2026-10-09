/**
 * @file PhysicsComponent.h
 * @brief 강체 물리 컴포넌트(바디 · 관절 · 캐릭터, 2D · 3D)의 기반입니다 — 씬의 물리(`ScenePhysics`)에 등록되고, 프레임마다 정해진 순서로 불립니다.
 * @details 컴포넌트는 틱하지 않습니다(병렬 틱에서 물리 씬을 만지면 같은 그룹의 다른 오브젝트가 스케줄에 따라 다른 자리를 본다). 매니저가 틱 ·
 *          트랜스폼 적용이 끝난 뒤 게임 스레드에서 `ScenePhysics::step` 을 부르고, 그것이 등록된 컴포넌트를 단계(바디 → 관절 → 캐릭터) 순으로 돕니다:
 *
 *          1. `beginPhysicsFrame` — 시작 전이면 아무것도 하지 않고, 시작했으면 바디를 만들거나(처음) 트랜스폼의 바깥 변화(코드가 옮김 ·
 *             `teleportTo`)를 바디로 옮깁니다.
 *          2. 고정 스텝마다 `prePhysicsStep`(키네마틱 목표 · 캐릭터 이동) → 씬 step → `postPhysicsStep`(바디 자세 기록).
 *          3. `endPhysicsFrame( alpha )` — 마지막 두 스텝 사이를 보간한 자세를 트랜스폼에 씁니다(유니티 Rigidbody interpolation).
 *
 *          바디는 플레이 중에만 있습니다(편집 중에는 시뮬레이션하지 않는다). 컴포넌트를 끄거나 플레이가 끝나면 백엔드 객체를 놓습니다(`releasePhysics`).
 *          핸들은 런타임 값이라 저장하지 않습니다 — 핫 리로드 · 되돌리기로 다시 만든 컴포넌트는 새 바디를 만듭니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;
    class ScenePhysics;

    /** @brief 물리 컴포넌트가 도는 단계입니다. 관절은 바디가 있어야 만들 수 있어 그 뒤입니다. */
    enum class PhysicsComponentPhase : uint8
    {
        Body = 0,
        Joint,
        Character,
        Count
    };

    REFLECT( Abstract, Category = "Physics", DisplayName = "Physics Component", Tooltip = "Base of rigid body, joint and character components" )
    class SW_API PhysicsComponent : public SceneComponent
    {
        friend class ScenePhysics; ///< 등록 자리(`_physicsIndex`)를 적고 단계 콜백을 부른다

    public:
        REFLECT_BODY();
        /** @brief 등록되지 않았다는 표시입니다. */
        static constexpr uint32 kNotRegistered = invalid_index::kUint32;

        explicit PhysicsComponent( PhysicsComponentPhase phase );
        ~PhysicsComponent() override = default;

        /** @brief 씬의 물리에 등록합니다(바디는 시작한 뒤 첫 물리 프레임에 생긴다). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 백엔드 객체를 놓고 등록을 풉니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 플레이가 끝나면 백엔드 객체를 놓습니다. */
        void onEndPlay() override;
        /** @brief 다음 물리 프레임에 바디를 그 자리로 순간이동시키고 보간을 끊습니다. */
        void onTeleported() override;
        /** @brief 값이 바뀌면(에디터 · 코드) 다음 물리 프레임에 백엔드 객체를 다시 만듭니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 등록된 씬의 물리입니다(등록 전이면 nullptr). */
        ScenePhysics* getScenePhysics() const { return _pScenePhysics; }
        /** @brief 이 컴포넌트의 단계입니다. */
        PhysicsComponentPhase getPhysicsPhase() const { return _phase; }

    protected:
        /** @brief 시뮬레이션에 들 상태인지입니다 — 시작했고, 켜졌고, 지워지는 중이 아니다. */
        bool isSimulated() const;
        /** @brief 순간이동 표시를 읽고 지웁니다. */
        bool consumeTeleport() { return _bTeleportPending.exchange( false, std::memory_order_acq_rel ); }
        /** @brief 다음 물리 프레임에 백엔드 객체를 다시 만들게 합니다(게임 스레드 — 세터 · 속성 변경). */
        void requestRebuild() { _bRebuildPending = true; }
        /** @brief 다시 만들라는 표시를 읽고 지웁니다. */
        bool consumeRebuild()
        {
            const bool bRebuild = _bRebuildPending;
            _bRebuildPending    = false;
            return bRebuild;
        }

        /** @brief 물리 프레임 시작 — 백엔드 객체를 만들거나 트랜스폼의 바깥 변화를 옮깁니다. */
        virtual void beginPhysicsFrame( ScenePhysics& physics ) { (void)physics; }
        /** @brief 고정 스텝 직전 — @p stepIndex 는 이 프레임의 몇 번째 스텝인지(0 부터), @p stepCount 는 이 프레임의 스텝 수입니다. */
        virtual void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
        {
            (void)physics;
            (void)fixedDeltaTime;
            (void)stepIndex;
            (void)stepCount;
        }
        /** @brief 고정 스텝 직후 — 자세를 기록합니다. */
        virtual void postPhysicsStep( ScenePhysics& physics ) { (void)physics; }
        /** @brief 물리 프레임 끝 — 보간 비 @p alpha 로 자세를 트랜스폼에 씁니다. */
        virtual void endPhysicsFrame( ScenePhysics& physics, float32 alpha )
        {
            (void)physics;
            (void)alpha;
        }
        /** @brief 백엔드 객체(바디 · 관절 · 캐릭터)를 놓습니다. 몇 번 불려도 됩니다. */
        virtual void releasePhysics( ScenePhysics& physics ) { (void)physics; }

    private:
        ScenePhysics*         _pScenePhysics;
        uint32                _physicsIndex; ///< `ScenePhysics` 의 단계 목록 자리. 없으면 `kNotRegistered`
        PhysicsComponentPhase _phase;
        bool                  _bRebuildPending;  ///< 게임 스레드에서만 읽고 쓴다(속성 변경 → 다음 물리 프레임)
        atomic<bool>          _bTeleportPending; ///< 틱 중 다른 워커가 세울 수 있어 원자값이다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 컴포넌트가 다음 물리 프레임에 바디에 줄 명령입니다. 틱 중 어느 워커에서든 쌓고(컴포넌트의 잠금 아래), 물리 프레임이 게임 스레드에서 씁니다.
     * @details 물리 씬은 한 스레드에서만 만집니다(Box2D 는 동시 호출을 막는다) — 병렬 틱의 `addForce` 가 씬을 바로 부르지 않는 이유입니다.
     *          힘 · 토크는 그 프레임의 모든 스텝에, 충격량은 첫 스텝에 한 번 줍니다.
     */
    template <typename TDimension>
    struct PhysicsBodyCommand
    {
        using Vector  = typename TDimension::Vector;
        using Angular = typename TDimension::Angular;

        Vector  _force{};
        Vector  _impulse{};
        Angular _torque{};
        Vector  _linearVelocity{};
        Angular _angularVelocity{};
        bool    _bSetLinearVelocity{ false };
        bool    _bSetAngularVelocity{ false };
        bool    _bWake{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 물리 컴포넌트가 함께 쓰는 자세 · 셰이프 도우미입니다. */
    struct SW_API PhysicsComponentUtil
    {
        /** @brief 월드 행렬을 자리 · 회전 · 배율로 풉니다. */
        static void readWorldPose( const SceneComponent& component, float3& outPosition, quaternion& outRotation, float3& outScale );
        /** @brief 자리 · 회전(배율은 지금 것)을 월드 트랜스폼으로 쓰고, 쓴 뒤의 값을 다시 읽어 돌려줍니다(오일러 왕복 오차까지 같은 값으로 비교하려고). */
        static void writeWorldPose( SceneComponent& component, const float3& position, const quaternion& rotation, float3& outWrittenPosition,
                                    quaternion& outWrittenRotation );
        /** @brief 두 자세가 다른지입니다(1 mm · 0.06 도 넘게). */
        static bool hasMoved( const float3& position, const quaternion& rotation, const float3& otherPosition, const quaternion& otherRotation );
        /** @brief Z 축 둘레 각입니다(2D 물리의 회전). */
        static float32 getAngle2D( const quaternion& rotation );
        /** @brief Z 축 둘레 각의 회전입니다. */
        static quaternion makeRotation2D( float32 angle );
        /** @brief 셰이프를 월드 배율로 키웁니다(상자 · 자리 · 점은 축마다, 반지름은 가로 축 중 큰 쪽, 캡슐 높이는 Y). */
        static PhysicsShapeDesc3D makeScaledShape( const PhysicsShapeDesc3D& shape, const float3& scale );
        /** @brief 2D 셰이프를 월드 배율(XY)로 키웁니다. */
        static PhysicsShapeDesc2D makeScaledShape( const PhysicsShapeDesc2D& shape, const float3& scale );
    };
} // namespace sw
