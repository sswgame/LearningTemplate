/**
 * @file PhysicsTypes.h
 * @brief 2D · 3D 물리가 함께 쓰는 낱말 — 핸들(세대 포함) · 바디 종류 · 관절 종류 · 모터 · 접촉 단계입니다.
 * @details 백엔드(Jolt · Box2D)와 무관한 값만 둡니다. 차원마다 다른 것은 벡터 · 회전 타입뿐이고(`PhysicsDimension3D` · `PhysicsDimension2D`),
 *          그 위의 서술자 · 결과 타입은 차원 묶음을 받는 템플릿 하나씩입니다(`PhysicsDesc.h` · `PhysicsQuery.h` · `PhysicsContact.h`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 물리 객체(바디 · 관절 · 캐릭터 · 셰이프)를 가리키는 핸들입니다. 슬롯 인덱스와 세대를 담습니다(`SlotHandle`).
     * @details 객체를 지운 뒤 슬롯을 다시 써도 옛 핸들은 세대가 달라 무효입니다 — 씬 함수는 무효 핸들을 받으면 아무것도 하지 않고 false · 0 을
     *          돌려줍니다. 태그 타입으로 종류를 나눠 바디 핸들을 관절 함수에 넘기는 실수를 컴파일 오류로 만듭니다.
     */
    template <typename TTag>
    class PhysicsHandle
    {
    public:
        constexpr PhysicsHandle() noexcept = default;

        /** @brief 슬롯 핸들에서 만듭니다(백엔드 씬이 자기 슬롯 표로 냅니다). */
        static constexpr PhysicsHandle fromSlot( SlotHandle slot ) noexcept
        {
            PhysicsHandle handle;
            handle._slot = slot;
            return handle;
        }
        /** @brief 묶은 값(`packed`)에서 되돌립니다. */
        static constexpr PhysicsHandle fromPacked( uint64 packed ) noexcept { return fromSlot( SlotHandle::fromPacked( packed ) ); }

        /** @brief 슬롯 핸들입니다. */
        [[nodiscard]] constexpr SlotHandle getSlot() const noexcept { return _slot; }
        /** @brief (세대 << 32) | 인덱스 입니다. 0 이면 무효입니다. */
        [[nodiscard]] constexpr uint64 packed() const noexcept { return _slot.packed(); }
        /** @brief 세대가 0 이 아니면 true 입니다. 씬에 아직 살아 있는지는 씬에 묻습니다(`isBodyValid` 등). */
        [[nodiscard]] constexpr bool isValid() const noexcept { return _slot.isValid(); }

        friend constexpr bool operator==( PhysicsHandle lhs, PhysicsHandle rhs ) noexcept { return lhs._slot == rhs._slot; }
        friend constexpr bool operator!=( PhysicsHandle lhs, PhysicsHandle rhs ) noexcept { return lhs._slot != rhs._slot; }
        friend constexpr bool operator<( PhysicsHandle lhs, PhysicsHandle rhs ) noexcept { return lhs._slot < rhs._slot; }

    private:
        SlotHandle _slot{};
    };
} // namespace sw

namespace sw
{
    /** @brief 바디 핸들의 태그입니다. */
    struct PhysicsBodyTag
    {
    };
} // namespace sw

namespace sw
{
    /** @brief 관절 핸들의 태그입니다. */
    struct PhysicsJointTag
    {
    };
} // namespace sw

namespace sw
{
    /** @brief 캐릭터 컨트롤러 핸들의 태그입니다. */
    struct PhysicsCharacterTag
    {
    };
} // namespace sw

namespace sw
{
    /** @brief 미리 지어 두고 여러 바디가 나눠 쓰는 셰이프 묶음 핸들의 태그입니다. */
    struct PhysicsShapeTag
    {
    };
} // namespace sw

namespace sw
{
    using PhysicsBodyHandle      = PhysicsHandle<PhysicsBodyTag>;
    using PhysicsJointHandle     = PhysicsHandle<PhysicsJointTag>;
    using PhysicsCharacterHandle = PhysicsHandle<PhysicsCharacterTag>;
    using PhysicsShapeHandle     = PhysicsHandle<PhysicsShapeTag>;

    /**
     * @brief 바디가 움직이는 방식입니다(유니티 `RigidbodyType2D` · Jolt `EMotionType` · Box2D `b2BodyType`).
     * @details Static 은 움직이지 않고(바닥 · 벽), Kinematic 은 코드가 움직이고(문 · 엘리베이터 · 애니메이션을 따르는 히트박스) 힘을 받지
     *          않으며, Dynamic 은 힘 · 중력 · 접촉으로 움직입니다.
     */
    ENUM()
    enum class PhysicsBodyType : uint8
    {
        Static = 0,
        Kinematic,
        Dynamic,
    };

    /**
     * @brief 관절 종류입니다. 2D 는 Cone 을 지원하지 않습니다(평면에는 스윙이 없다 — 만들면 오류).
     * @details Fixed(용접) · Hinge(한 축 회전, 2D 의 revolute) · Cone(스윙 원뿔 + 비틀림 — 래그돌 어깨 · 목, Jolt SwingTwist) ·
     *          Distance(두 점 사이 거리 범위) · Slider(한 축 이동, 2D 의 prismatic).
     */
    ENUM()
    enum class PhysicsJointType : uint8
    {
        Fixed = 0,
        Hinge,
        Cone,
        Distance,
        Slider,
    };

    /** @brief 관절 모터의 동작입니다. Velocity 는 목표 속도(라디안/초 · 미터/초), Position 은 목표 각 · 거리로 끕니다. */
    ENUM()
    enum class PhysicsMotorMode : uint8
    {
        Off = 0,
        Velocity,
        Position,
    };

    /** @brief 접촉 · 트리거 이벤트의 단계입니다(유니티 Enter · Stay · Exit). */
    enum class PhysicsContactPhase : uint8
    {
        Begin = 0,
        Stay,
        End,
    };
} // namespace sw

namespace sw
{
    /** @brief 관절 하나의 모터입니다. Hinge(각) · Slider(거리) · Distance(거리)가 씁니다. */
    struct PhysicsJointMotor
    {
        float32          _target{ 0.0f };   ///< Velocity 면 목표 속도, Position 이면 목표 각(라디안) · 거리(미터)
        float32          _maxForce{ 0.0f }; ///< 모터가 낼 수 있는 가장 큰 힘(뉴턴) · 토크(뉴턴미터). 0 이면 모터가 없는 것과 같다
        PhysicsMotorMode _mode{ PhysicsMotorMode::Off };
    };
} // namespace sw
