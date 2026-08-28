#pragma once
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/ActionCombat/AttackBaseComponent.h"

namespace sw
{
	ENUM()
	enum class AttackKind : uint8
	{
		MeleeSweep = 0,
		StompCrush,
		SwordSlash,
		BossSmash
	};

	REFLECT()
	class AttackComponent : public AttackBaseComponent
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		AttackKind attackKind;

		PROPERTY()
		float32 knockbackPower;

		PROPERTY()
		float2 hitboxSize;

		PROPERTY()
		vector<uint64> hitVictimList;

		AttackComponent();
		virtual ~AttackComponent() override						 = default;
		AttackComponent( AttackComponent&& ) noexcept			 = default;
		AttackComponent& operator=( AttackComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void triggerAttack( AttackKind kind, float32 damage, float32 duration, const float2& size = { 64.0f, 64.0f } );
	};
} // namespace sw
