#pragma once
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/UI/HPBarBaseComponent.h"

namespace sw
{
	ENUM()
	enum class HPBarTargetKind : uint8
	{
		Player = 0,
		Monster,
		Boss
	};

	REFLECT()
	class HPBarComponent : public HPBarBaseComponent
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		HPBarTargetKind _targetKind;

		PROPERTY()
		string _bossName;

		HPBarComponent();
		virtual ~HPBarComponent() override					   = default;
		HPBarComponent( HPBarComponent&& ) noexcept			   = default;
		HPBarComponent& operator=( HPBarComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;
	};
} // namespace sw
