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
        (void)SW_SET_VIEW_MODEL_FIELD( _gateLockedVisibility, toHUDVisibility( bShown ) );
    }

    void ActionCombatRoomHUDViewModel::setClearedShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _clearedVisibility, toHUDVisibility( bShown ) );
    }

    void ActionCombatRoomHUDViewModel::setDefeatedShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _defeatedVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
