#pragma once
#include "Core/Container/ObjectHandle.h"

namespace sw
{
	/**
	 * @brief 세대가 붙은 엔티티 핸들입니다. 인덱스 재사용 시 옛 핸들은 무효입니다.
	 */
	using EntityHandle = ObjectHandle;
	using Entity	   = EntityHandle;

	/**
	 * @brief 무효 엔티티 (generation 0).
	 */
	constexpr Entity kNullEntity{};

} // namespace sw
