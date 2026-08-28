/**
 * @file ComponentHandle.h
 * @brief 컴포넌트 핸들. 저장은 핸들, T*는 resolve 순간에만 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
	class Component;

	/**
	 * @brief 타입 소거 컴포넌트 핸들입니다.
	 * @details GameObject 소유 컴포넌트는 objectId+componentId로 식별합니다.
	 */
	class ComponentHandle
	{
	public:
		/** @brief 무효 핸들을 만듭니다. */
		constexpr ComponentHandle() noexcept = default;

		/** @brief GameObject 소유 컴포넌트 핸들을 만듭니다. */
		static constexpr ComponentHandle makeOwned( uint64 objectId, uint64 componentId ) noexcept
		{
			ComponentHandle handle;
			handle._objectId	= objectId;
			handle._componentId = componentId;
			return handle;
		}

		/** @brief 소유 GameObject ID를 반환합니다. */
		[[nodiscard]] constexpr uint64 objectId() const noexcept { return _objectId; }
		/** @brief 컴포넌트 인스턴스 ID를 반환합니다. */
		[[nodiscard]] constexpr uint64 componentId() const noexcept { return _componentId; }
		/** @brief objectId+componentId가 있으면 true입니다. */
		[[nodiscard]] constexpr bool	 isValid() const noexcept { return _objectId != 0 && _componentId != 0; }
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
