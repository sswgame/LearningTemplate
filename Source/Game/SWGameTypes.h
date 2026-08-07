#pragma once
/**
 * @file SWGameTypes.h
 * @brief SWGame 모듈 공용 리플렉션 타입
 */

#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/Component.h"
#include "Game/SWGameModuleHeads.h"

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

	/**
	 * @brief Hierarchy↔Inspector↔reflection E2E용 샘플 컴포넌트
	 */
	REFLECT()
	class SampleHealthComponent : public Component
	{
	public:
		SW_REFLECT_TYPE_API();

		PROPERTY()
		float32 _health = 100.0f;

		FUNCTION()
		void takeDamage( float32 amount )
		{
			_health -= amount;
			if ( _health < 0.0f )
				_health = 0.0f;
		}

		FUNCTION()
		float32 getHealth() const
		{
			return _health;
		}

		FUNCTION()
		void heal( float32 amount )
		{
			_health += amount;
		}
	};
} // namespace sw
