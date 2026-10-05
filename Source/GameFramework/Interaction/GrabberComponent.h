/**
 * @file GrabberComponent.h
 * @brief 집기 · 던지기 · 붙이기 — 물리 백엔드를 모르는 창구(`IGrabPhysics`)와 그것을 쓰는 컴포넌트입니다.
 * @details 게임이 `IGrabPhysics` 를 게임 서비스로 걸면 그것을 쓰고, 없으면 강체 백엔드(`RigidBodyGrabPhysics` — 강체를 키네마틱으로 손에 묶고 놓을 때
 *          동적 + 속도)입니다. 강체가 없는 대상은 트랜스폼 폴백(`TransformGrabPhysics` — 들고 있는 쪽 씬 컴포넌트에 월드 자리를 지켜 붙이고, 놓으면 떼며
 *          속도는 대상의 `GravityComponent` 수직 속도로만 준다)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 집기 백엔드입니다. 게임 스레드에서 불립니다(틱 밖 — 컴포넌트가 틱 뒤로 미룬다). */
    class IGrabPhysics
    {
    public:
        IGrabPhysics()          = default;
        virtual ~IGrabPhysics() = default;

        IGrabPhysics( const IGrabPhysics& )            = default;
        IGrabPhysics& operator=( const IGrabPhysics& ) = default;

        /** @brief @p target 을 @p holder 의 @p localOffset 자리에 묶습니다. 묶었으면 true 입니다. */
        [[nodiscard]] virtual bool attach( GameObject& holder, GameObject& target, const float3& localOffset ) = 0;
        /** @brief 묶음을 풀고 @p velocity(월드, m/s)로 놓습니다(0 이면 떨어뜨리기). */
        virtual void release( GameObject& holder, GameObject& target, const float3& velocity ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 물리 없이 트랜스폼으로 묶는 폴백입니다. */
    class SW_GF_API TransformGrabPhysics final : public IGrabPhysics
    {
    public:
        [[nodiscard]] bool attach( GameObject& holder, GameObject& target, const float3& localOffset ) override;
        void               release( GameObject& holder, GameObject& target, const float3& velocity ) override;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RigidBodyGrabPhysics
     * @brief 강체를 키네마틱으로 손에 묶고(트랜스폼을 따른다 — 밀린 것이 속도를 받는다), 놓을 때 동적으로 되돌려 속도를 줍니다(3D · 2D 강체).
     *        강체가 없는 대상은 트랜스폼 폴백(`TransformGrabPhysics`)과 같습니다. 서비스가 없을 때 `GrabberComponent` 의 기본 백엔드입니다.
     */
    class SW_GF_API RigidBodyGrabPhysics final : public IGrabPhysics
    {
    public:
        [[nodiscard]] bool attach( GameObject& holder, GameObject& target, const float3& localOffset ) override;
        void               release( GameObject& holder, GameObject& target, const float3& velocity ) override;

    private:
        TransformGrabPhysics _transform;
    };
} // namespace sw

namespace sw
{
    /**
     * @class GrabberComponent
     * @brief 하나를 집어 들고(`grab`) 던지거나(`throwHeld`) 내려놓습니다(`dropHeld`). 소켓에 붙이기(무기를 등에)는 `grab` 에 그 소켓 자리를 오프셋으로 줍니다.
     * @details 든 것은 핸들로 저장합니다(`_held`). 백엔드는 게임 서비스 `IGrabPhysics`, 없으면 `RigidBodyGrabPhysics`.
     */
    REFLECT( Category = "Interaction", DisplayName = "Grabber", Tooltip = "Grab, carry, throw or attach one object through the grab physics interface" )
    class SW_GF_API GrabberComponent : public Component
    {
    public:
        REFLECT_BODY();

        GrabberComponent();
        virtual ~GrabberComponent() override = default;

        /** @brief 집습니다. 이미 들고 있거나 자기 자신이면 false 입니다. 게임 스레드에서 부릅니다. */
        [[nodiscard]] bool grab( GameObject& target );
        /** @brief 든 것을 @p velocity 로 던집니다. 든 것이 없으면 false 입니다. */
        bool throwHeld( const float3& velocity );
        /** @brief 든 것을 내려놓습니다. */
        bool dropHeld() { return throwHeld( float3{} ); }

        GameObjectHandle getHeld() const { return _held; }
        bool             isHolding() const { return _held.isValid(); }
        void             setHoldOffset( const float3& offset ) { _holdOffset = offset; }

    private:
        IGrabPhysics& getBackend();

    private:
        PROPERTY( Category = "Grab", DisplayName = "Hold Offset", Tooltip = "Where the held object sits, in this object's space", Units = m )
        float3 _holdOffset;
        PROPERTY( Category = "Grab", DisplayName = "Held", Tooltip = "Object held now (runtime)" )
        GameObjectHandle _held;

        RigidBodyGrabPhysics _fallback;
    };
} // namespace sw
