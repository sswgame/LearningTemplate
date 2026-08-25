#pragma once
#include "Core/Container/string.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	REFLECT()
	struct GameKitConfig
	{
		REFLECT_BODY();

		PROPERTY()
		string _name;

		PROPERTY()
		vector<string> _dependencyModuleList;
	};

	REFLECT()
	struct AppConfig : IConfig
	{
		REFLECT_BODY();

		PROPERTY()
		vector<GameKitConfig> _gameKitModuleList;
	};
} // namespace sw
