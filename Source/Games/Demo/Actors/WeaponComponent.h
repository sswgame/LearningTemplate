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
		string _weaponName{};

		PROPERTY()
		int32 _attackPower{ 0 };

		PROPERTY()
		float32 _attackSpeed{ 0.0f };

		PROPERTY()
		float32 _skillCoolTime{ 0.0f };

		PROPERTY()
		float32 _currentSkillCoolTime{ 0.0f };

		PROPERTY()
		bool _bAttacking{ false };

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
