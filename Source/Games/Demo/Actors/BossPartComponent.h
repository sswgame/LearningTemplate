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

	REFLECT()
	class BossPartComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		BossPartType _partType{ BossPartType::Hand };

		PROPERTY()
		float32 _attackTimer{ 0.0f };

		PROPERTY()
		float32 _attackInterval{ 0.0f };

		PROPERTY()
		float2 _offsetFromBoss{ 0.0f, 0.0f };

		PROPERTY()
		bool _bIsActive{ true };

		BossPartComponent()											 = default;
		virtual ~BossPartComponent() override						 = default;
		BossPartComponent( BossPartComponent&& ) noexcept			 = default;
		BossPartComponent& operator=( BossPartComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;
	};
} // namespace sw
