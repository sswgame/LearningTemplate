#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	ENUM()
	enum class BossPartType : uint8
	{
		Hand = 0,
		Laser,
		Sword,
		Shield
	};

	REFLECT_SCRIPT()
	class BossPartComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		BossPartType partType{ BossPartType::Hand };

		PROPERTY()
		float32 attackTimer{ 0.0f };

		PROPERTY()
		float32 attackInterval{ 0.0f };

		PROPERTY()
		float2 offsetFromBoss{ 0.0f, 0.0f };

		PROPERTY()
		bool bIsActive{ true };

		BossPartComponent()											 = default;
		virtual ~BossPartComponent() override						 = default;
		BossPartComponent( BossPartComponent&& ) noexcept			 = default;
		BossPartComponent& operator=( BossPartComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;
	};
} // namespace sw
