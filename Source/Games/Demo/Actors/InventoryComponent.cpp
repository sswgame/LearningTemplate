#include "pch.h"

#include "Games/Demo/Actors/InventoryComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void InventoryComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Inventory"_tag );
		usedSlots = static_cast<int32>( itemList.size() );
	}

	void InventoryComponent::onEndPlay()
	{
	}

	void InventoryComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;
		usedSlots = static_cast<int32>( itemList.size() );
	}

	void InventoryComponent::toggleInventory()
	{
		bIsOpen = ( bIsOpen == false );
	}

	bool InventoryComponent::addItem( const string& itemId, int32 count )
	{
		if ( itemId.empty() || count <= 0 )
			return false;

		for ( int32 itemIndex = 0; itemIndex < count; ++itemIndex )
		{
			if ( static_cast<int32>( itemList.size() ) >= maxSlots )
			{
				usedSlots = static_cast<int32>( itemList.size() );
				return false;
			}
			itemList.push_back( itemId );
		}
		usedSlots = static_cast<int32>( itemList.size() );
		return true;
	}

	bool InventoryComponent::removeItem( const string& itemId, int32 count )
	{
		if ( itemId.empty() || count <= 0 )
			return false;

		int32 removedCount{ 0 };
		for ( auto itemIter = itemList.begin(); itemIter != itemList.end() && removedCount < count; )
		{
			if ( *itemIter == itemId )
			{
				itemIter = itemList.erase( itemIter );
				removedCount++;
			}
			else
			{
				++itemIter;
			}
		}
		usedSlots = static_cast<int32>( itemList.size() );
		return ( removedCount == count );
	}

	bool InventoryComponent::hasItem( const string& itemId, int32 count ) const
	{
		if ( itemId.empty() || count <= 0 )
			return false;

		int32 foundCount{ 0 };
		for ( const string& item : itemList )
		{
			if ( item == itemId )
			{
				foundCount++;
				if ( foundCount >= count )
					return true;
			}
		}
		return false;
	}

	void InventoryComponent::addGold( int32 amount )
	{
		if ( amount > 0 )
			gold += amount;
	}

	bool InventoryComponent::spendGold( int32 amount )
	{
		if ( amount <= 0 )
			return true;
		if ( gold >= amount )
		{
			gold -= amount;
			return true;
		}
		return false;
	}
} // namespace sw
