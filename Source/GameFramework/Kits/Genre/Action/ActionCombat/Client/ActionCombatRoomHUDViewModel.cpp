#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionCombatRoomHUDViewModel.h"

namespace sw
{
    ActionCombatRoomHUDViewModel::ActionCombatRoomHUDViewModel()
        : UIViewModel{}
        , _gateLockedVisibility{ WidgetVisibility::Collapsed }
        , _clearedVisibility{ WidgetVisibility::Collapsed }
        , _defeatedVisibility{ WidgetVisibility::Collapsed }
    {
    }

    ActionCombatRoomHUDViewModel::~ActionCombatRoomHUDViewModel() = default;

    const TypeInfo* ActionCombatRoomHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void ActionCombatRoomHUDViewModel::setGateLockedShown( bool bShown )
    {
        (void)setField( _gateLockedVisibility, toVisibility( bShown ), "_gateLockedVisibility" );
    }

    void ActionCombatRoomHUDViewModel::setClearedShown( bool bShown )
    {
        (void)setField( _clearedVisibility, toVisibility( bShown ), "_clearedVisibility" );
    }

    void ActionCombatRoomHUDViewModel::setDefeatedShown( bool bShown )
    {
        (void)setField( _defeatedVisibility, toVisibility( bShown ), "_defeatedVisibility" );
    }

    WidgetVisibility ActionCombatRoomHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
