#include "pch.h"

#include "Engine/Presentation/UIStatusViewModel.h"

namespace sw
{
    UIStatusViewModel::UIStatusViewModel()
        : UIViewModel{}
        , _status{}
    {
    }

    UIStatusViewModel::~UIStatusViewModel() = default;

    const TypeInfo* UIStatusViewModel::getTypeInfo() const
    {
        return StaticType();
    }
} // namespace sw
