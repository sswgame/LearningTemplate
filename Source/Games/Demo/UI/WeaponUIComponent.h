#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class WeaponUIComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		string _weaponName;

		PROPERTY()
		float32 _coolTimeRatio;

		PROPERTY()
		int32 _slotIndex;

		WeaponUIComponent();
		virtual ~WeaponUIComponent() override						 = default;
		WeaponUIComponent( WeaponUIComponent&& ) noexcept			 = default;
		WeaponUIComponent& operator=( WeaponUIComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void setCoolTime( float32 ratio );
	};
} // namespace sw
