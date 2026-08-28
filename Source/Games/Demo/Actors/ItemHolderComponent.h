#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class ItemHolderComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		string _equippedWeapon{};

		PROPERTY()
		string _equippedAccessory{};

		PROPERTY()
		int32 _currentSlot{ 0 };

		PROPERTY()
		int32 _gold{ 0 };

		ItemHolderComponent()											 = default;
		virtual ~ItemHolderComponent() override							 = default;
		ItemHolderComponent( ItemHolderComponent&& ) noexcept			 = default;
		ItemHolderComponent& operator=( ItemHolderComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void equipWeapon( const string& name );
		void equipAccessory( const string& name );
		void unequipWeapon();
		void unequipAccessory();
	};
} // namespace sw
