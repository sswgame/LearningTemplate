#include "pch.h"

#include "GameFramework/Client/Shop/ShopViewModel.h"

namespace sw
{
    ShopViewModel::ShopViewModel()
        : UIViewModel{}
        , _balance{}
        , _status{}
    {
    }

    ShopViewModel::~ShopViewModel() = default;

    const TypeInfo* ShopViewModel::getTypeInfo() const
    {
        return StaticType();
    }
} // namespace sw
