/**
 * @file ComponentHandle.h
 * @brief 엔티티+타입 컴포넌트 핸들. 저장은 핸들, T*는 resolve 순간에만 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/ECS/Entity.h"

#include <cstddef>

namespace sw
{
	class Component;
} // namespace sw

namespace sw
{
	class Registry;

	/**
	 * @brief 타입 소거 컴포넌트 핸들입니다. 세대는 Entity에 있습니다.
	 */
	class ComponentHandle
	{
	public:
		/** @brief 무효 핸들을 만듭니다. */
		constexpr ComponentHandle() noexcept = default;

		/** @brief 엔티티와 컴포넌트 타입 ID로 핸들을 만듭니다. */
		static constexpr ComponentHandle make( Entity entity, uint32 typeId ) noexcept
		{
			ComponentHandle handle;
			handle._entity = entity;
			handle._typeId = typeId;
			return handle;
		}

		/** @brief 대상 엔티티를 반환합니다. */
		[[nodiscard]] constexpr Entity entity() const noexcept { return _entity; }
		/** @brief 컴포넌트 타입 ID를 반환합니다. */
		[[nodiscard]] constexpr uint32 typeId() const noexcept { return _typeId; }
		/** @brief 엔티티 세대가 있고 typeId가 0이 아니면 true입니다. 풀 점유는 resolve로 확인합니다. */
		[[nodiscard]] constexpr bool	 isValid() const noexcept { return _entity.isValid() && _typeId != 0; }
		[[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

		friend constexpr bool operator==( ComponentHandle lhs, ComponentHandle rhs ) noexcept { return lhs._entity == rhs._entity && lhs._typeId == rhs._typeId; }
		friend constexpr bool operator!=( ComponentHandle lhs, ComponentHandle rhs ) noexcept { return ( lhs == rhs ) == false; }

	private:
		Entity _entity{};
		uint32 _typeId{ 0 };
	};

	/**
	 * @brief 타입 T 컴포넌트 핸들입니다. get()/operator-> 가 레지스트리에서 다시 찾습니다.
	 */
	template <typename T>
	class TComponentHandle
	{
	public:
		/** @brief 무효 핸들을 만듭니다. */
		TComponentHandle() = default;
		/** @brief 엔티티와 레지스트리로 핸들을 만듭니다. 컴포넌트 유무는 get()에서 확인합니다. */
		TComponentHandle( Entity entity, Registry* pRegistry )
			: _entity{ entity }
			, _pRegistry{ pRegistry } {}

		/** @brief 대상 엔티티를 반환합니다. */
		Entity entity() const { return _entity; }
		/** @brief 타입 소거 핸들을 반환합니다. */
		ComponentHandle untyped() const;
		/** @brief 현재 풀에서 T를 찾습니다. 없거나 pending-kill이면 nullptr. */
		T* get() const;
		/** @brief get()과 같습니다. */
		T* operator->() const { return get(); }
		/** @brief 살아 있는 컴포넌트가 있으면 true. */
		[[nodiscard]] bool isValid() const { return get() != nullptr; }
		/** @brief 살아 있는 컴포넌트가 있으면 true. */
		explicit operator bool() const { return isValid(); }

		friend bool operator==( const TComponentHandle& handle, std::nullptr_t ) { return handle.get() == nullptr; }
		friend bool operator==( std::nullptr_t, const TComponentHandle& handle ) { return handle.get() == nullptr; }
		friend bool operator!=( const TComponentHandle& handle, std::nullptr_t ) { return handle.get() != nullptr; }
		friend bool operator!=( std::nullptr_t, const TComponentHandle& handle ) { return handle.get() != nullptr; }

	private:
		Entity	  _entity{};
		Registry* _pRegistry{ nullptr };
	};
} // namespace sw
