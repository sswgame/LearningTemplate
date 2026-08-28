#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class WeaponComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		string weaponName{};

		PROPERTY()
		int32 attackPower{ 0 };

		PROPERTY()
		float32 attackSpeed{ 0.0f };

		PROPERTY()
		float32 skillCoolTime{ 0.0f };

		PROPERTY()
		float32 currentSkillCoolTime{ 0.0f };

		PROPERTY()
		bool bAttacking{ false };

		WeaponComponent()										 = default;
		virtual ~WeaponComponent() override						 = default;
		WeaponComponent( WeaponComponent&& ) noexcept			 = default;
		WeaponComponent& operator=( WeaponComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		bool trySkill();
	};
} // namespace sw
