#include "pch.h"

#include "GameFramework/Client/Inventory/InventoryViewModel.h"

namespace sw
{
    InventoryViewModel::InventoryViewModel()
        : UIViewModel{}
        , _details{}
        , _stats{}
        , _weight{}
        , _status{}
    {
    }

    InventoryViewModel::~InventoryViewModel() = default;

    const TypeInfo* InventoryViewModel::getTypeInfo() const
    {
        return StaticType();
    }
} // namespace sw
