/**
 * @file GameObjectManagerInternal.h
 * @brief Engine / Editor / Test 전용 Registry 접근. Game 모듈에서 include 하지 마세요.
 */
#pragma once
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
	struct GameObjectManagerAccess
	{
		/** @brief 반환합니다. */
		static sw::Registry& get( GameObjectManager& manager ) { return manager.getRegistry(); }

		/** @brief 반환합니다. */
		static const sw::Registry& get( const GameObjectManager& manager ) { return manager.getRegistry(); }
	};
} // namespace sw
