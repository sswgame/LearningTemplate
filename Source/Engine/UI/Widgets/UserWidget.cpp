#include "pch.h"

#include "Engine/UI/Widgets/UserWidget.h"

namespace sw
{
    UserWidget::UserWidget()
        : OverlayPanel{}
        , _document{}
    {
    }

    UserWidget::~UserWidget() = default;

    const TypeInfo* UserWidget::getTypeInfo() const
    {
        return StaticType();
    }
} // namespace sw
