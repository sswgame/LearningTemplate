#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionRoomHUDViewModel.h"

namespace sw
{
    ActionRoomHUDViewModel::ActionRoomHUDViewModel()
        : UIViewModel{}
        , _gateLockedVisibility{ WidgetVisibility::Collapsed }
        , _clearedVisibility{ WidgetVisibility::Collapsed }
        , _defeatedVisibility{ WidgetVisibility::Collapsed }
    {
    }

    ActionRoomHUDViewModel::~ActionRoomHUDViewModel() = default;

    const TypeInfo* ActionRoomHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void ActionRoomHUDViewModel::setGateLockedShown( bool bShown )
    {
        (void)setField( _gateLockedVisibility, toVisibility( bShown ), "_gateLockedVisibility" );
    }

    void ActionRoomHUDViewModel::setClearedShown( bool bShown )
    {
        (void)setField( _clearedVisibility, toVisibility( bShown ), "_clearedVisibility" );
    }

    void ActionRoomHUDViewModel::setDefeatedShown( bool bShown )
    {
        (void)setField( _defeatedVisibility, toVisibility( bShown ), "_defeatedVisibility" );
    }

    WidgetVisibility ActionRoomHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
