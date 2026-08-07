#pragma once

#include "Core/Reflection/ReflectionCore.h"

namespace sw
{
	REFLECT()
	struct GamePlayerData
	{
		PROPERTY()
		int32 _health = 100;

		PROPERTY()
		float32 _speed = 5.0f;
	};
}
