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
		uint32 _width{ 1280 };

		PROPERTY()
		uint32 _height{ 720 };

		PROPERTY()
		bool _bVSync{ false };

		PROPERTY()
		string _title{ "SWEngine" };

		PROPERTY()
		RHIBackend _defaultRHI{ RHIBackend::DirectX12 };

		PROPERTY()
		string _clearColor{ "0.12 0.15 0.18 1.0" };
	};

	REFLECT()
	struct SW_API EngineConfig : IConfig
	{
		REFLECT_BODY();

		PROPERTY()
		WindowConfig _window;

		PROPERTY()
		string _engineData{ "engine/data/enginedata.xml" };

		PROPERTY()
		float32 _maxFrameDeltaTime{ 0.1f };
	};
} // namespace sw
