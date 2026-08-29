#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{

	REFLECT()
	struct SW_API WindowConfig
	{
		REFLECT_BODY();

		PROPERTY()
		string _title{ "SWEngine" };

		PROPERTY()
		string _clearColor{ "0.12 0.15 0.18 1.0" };

		PROPERTY()
		uint32 _width{ 1280 };

		PROPERTY()
		uint32 _height{ 720 };

		PROPERTY()
		RHIBackend _defaultRHI{ RHIBackend::DirectX12 };

		PROPERTY()
		bool _bVSync{ false };
	};

	REFLECT()
	struct SW_API EngineConfig : IConfig
	{
		REFLECT_BODY();

		PROPERTY()
		string _engineData{ "engine/data/enginedata.xml" };

		PROPERTY()
		WindowConfig _window;

		PROPERTY()
		float32 _maxFrameDeltaTime{ 0.1f };
	};
} // namespace sw
