#include "pch.h"

#include "Games/Demo/Actors/ItemHolderComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void ItemHolderComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Inventory"_tag );
	}

	void ItemHolderComponent::onEndPlay()
	{
	}

	void ItemHolderComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;
	}

	void ItemHolderComponent::equipWeapon( const string& name )
	{
		_equippedWeapon = name;
	}

	void ItemHolderComponent::equipAccessory( const string& name )
	{
		_equippedAccessory = name;
	}

	void ItemHolderComponent::unequipWeapon()
	{
		_equippedWeapon.clear();
	}

	void ItemHolderComponent::unequipAccessory()
	{
		_equippedAccessory.clear();
	}
} // namespace sw
