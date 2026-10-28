/**
 * @file PhysicsHandle.h
 * @brief 물리 객체(바디, 관절, 캐릭터, 셰이프, 탈것)를 가리키는 세대 포함 핸들입니다.
 * @details 컴포넌트 모델 코어(`Component.h` 의 충돌 · 트리거 정보)가 바디 핸들을 값으로 든다. 코어는 Physics 폴더를 include 하지 않으므로
 *          핸들만 Engine/Foundation 에 두고, 나머지 물리 기본 타입은 `Engine/Physics/PhysicsTypes.h` 에 있다.
 */
#pragma once
#include "Core/Container/SlotHandle.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 물리 객체(바디, 관절, 캐릭터, 셰이프)를 가리키는 핸들입니다. 슬롯 인덱스와 세대를 담습니다(`SlotHandle`).
     * @details 객체를 지운 뒤 슬롯을 다시 써도 이전 핸들은 세대가 달라 무효다. 씬 함수는 무효 핸들을 받으면 아무것도 하지 않고 false 나 0 을
     *          반환한다. 태그 타입으로 종류를 나눠서, 바디 핸들을 관절 함수에 넘기는 실수를 컴파일 오류로 만든다.
     */
    template <typename TTag>
    class PhysicsHandle
    {
    public:
        constexpr PhysicsHandle() noexcept = default;

        /** @brief 슬롯 핸들에서 만듭니다(백엔드 씬이 자기 슬롯 테이블로 만듭니다). */
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
    /** @brief 바퀴 차(차체 바디에 붙은 탈것 구속) 핸들의 태그입니다. */
    struct PhysicsVehicleTag
    {
    };
} // namespace sw

namespace sw
{
    /** @brief 미리 만들어 두고 여러 바디가 나눠 쓰는 셰이프의 핸들 태그입니다. */
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
    using PhysicsVehicleHandle   = PhysicsHandle<PhysicsVehicleTag>;
} // namespace sw
