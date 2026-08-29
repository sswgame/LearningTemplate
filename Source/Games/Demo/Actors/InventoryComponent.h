#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class InventoryComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		vector<string> _listItem;

		PROPERTY()
		int32 _maxSlots{ 0 };

		PROPERTY()
		int32 _usedSlots{ 0 };

		PROPERTY()
		int32 _gold{ 0 };

		PROPERTY()
		bool _bIsOpen{ false };

		InventoryComponent()										   = default;
		virtual ~InventoryComponent() override						   = default;
		InventoryComponent( InventoryComponent&& ) noexcept			   = default;
		InventoryComponent& operator=( InventoryComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void toggleInventory();
		bool addItem( const string& itemId, int32 count = 1 );
		bool removeItem( const string& itemId, int32 count = 1 );
		bool hasItem( const string& itemId, int32 count = 1 ) const;
		void addGold( int32 amount );
		bool spendGold( int32 amount );
	};
} // namespace sw
