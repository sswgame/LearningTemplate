/**
 * @file ComponentHandle.h
 * @brief 컴포넌트 핸들입니다. 보관할 때는 핸들을 쓰고, T* 는 핸들을 푼 그 순간에만 씁니다.
 *
 * @note `SlotHandle` 과 같은 곳(`Core/Container`)에 둡니다. objectId 와 componentId 두 정수뿐인 값 타입이라 Core 밖을
 *       전혀 모릅니다. 예전에는 `Engine/Object/Component/` 에 있었고, 그 때문에 직렬화기가 이 핸들의 텍스트 핸들러를
 *       등록하려면 Object 를 include 해야 했습니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief 타입을 지운 컴포넌트 핸들입니다.
     * @details GameObject 가 소유한 컴포넌트를 objectId + componentId 로 식별합니다.
     */
    class ComponentHandle
    {
    public:
        /** @brief 무효 핸들을 만듭니다. */
        constexpr ComponentHandle() noexcept = default;

        /** @brief GameObject 가 소유한 컴포넌트의 핸들을 만듭니다. */
        static constexpr ComponentHandle makeOwned( uint64 objectId, uint64 componentId ) noexcept
        {
            ComponentHandle handle;
            handle._objectId    = objectId;
            handle._componentId = componentId;
            return handle;
        }

        /** @brief 소유 GameObject 의 ID 를 반환합니다. */
        [[nodiscard]] constexpr uint64 objectId() const noexcept { return _objectId; }
        /** @brief 컴포넌트 인스턴스의 ID 를 반환합니다. */
        [[nodiscard]] constexpr uint64 componentId() const noexcept { return _componentId; }
        /** @brief objectId 와 componentId 가 모두 있으면 true 입니다. 대상이 살아 있는지는 매니저로 풀어 봐야 압니다. */
        [[nodiscard]] constexpr bool     isValid() const noexcept { return _objectId != 0 && _componentId != 0; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

        friend constexpr bool operator==( ComponentHandle lhs, ComponentHandle rhs ) noexcept
        {
            return lhs._objectId == rhs._objectId && lhs._componentId == rhs._componentId;
        }
        friend constexpr bool operator!=( ComponentHandle lhs, ComponentHandle rhs ) noexcept { return ( lhs == rhs ) == false; }

    private:
        uint64 _objectId{ 0 };
        uint64 _componentId{ 0 };
    };
} // namespace sw
