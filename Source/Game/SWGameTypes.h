#pragma once
/**
 * @file SWGameTypes.h
 * @brief SWGame 모듈 공용 리플렉션 타입
 */

#include "Core/Reflection/ReflectionCore.h"

namespace sw
{
	/** @brief 데모용 플레이어 체력·속도 데이터 */
	REFLECT()
	struct GamePlayerData
	{
		PROPERTY()
		int32 _health = 100; ///< 체력

		PROPERTY()
		float32 _speed = 5.0f; ///< 이동 속도
	};
}
