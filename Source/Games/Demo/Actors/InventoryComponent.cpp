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
		_usedSlots = static_cast<int32>( _listItem.size() );
	}

	void InventoryComponent::onEndPlay()
	{
	}

	void InventoryComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;
		_usedSlots = static_cast<int32>( _listItem.size() );
	}

	void InventoryComponent::toggleInventory()
	{
		_bIsOpen = ( _bIsOpen == false );
	}

	bool InventoryComponent::addItem( const string& itemId, int32 count )
	{
		if ( itemId.empty() || count <= 0 )
			return false;

		for ( int32 itemIndex = 0; itemIndex < count; ++itemIndex )
		{
			if ( static_cast<int32>( _listItem.size() ) >= _maxSlots )
			{
				_usedSlots = static_cast<int32>( _listItem.size() );
				return false;
			}
			_listItem.push_back( itemId );
		}
		_usedSlots = static_cast<int32>( _listItem.size() );
		return true;
	}

	bool InventoryComponent::removeItem( const string& itemId, int32 count )
	{
		if ( itemId.empty() || count <= 0 )
			return false;

		int32 removedCount{ 0 };
		for ( auto itemIter = _listItem.begin(); itemIter != _listItem.end() && removedCount < count; )
		{
			if ( *itemIter == itemId )
			{
				itemIter = _listItem.erase( itemIter );
				removedCount++;
			}
			else
			{
				++itemIter;
			}
		}
		_usedSlots = static_cast<int32>( _listItem.size() );
		return ( removedCount == count );
	}

	bool InventoryComponent::hasItem( const string& itemId, int32 count ) const
	{
		if ( itemId.empty() || count <= 0 )
			return false;

		int32 foundCount{ 0 };
		for ( const string& item : _listItem )
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
			_gold += amount;
	}

	bool InventoryComponent::spendGold( int32 amount )
	{
		if ( amount <= 0 )
			return true;
		if ( _gold >= amount )
		{
			_gold -= amount;
			return true;
		}
		return false;
	}
} // namespace sw
