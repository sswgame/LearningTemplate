#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Client/MonsterCollectorHUDViewModel.h"

namespace sw
{
    MonsterCollectorHUDViewModel::MonsterCollectorHUDViewModel()
        : UIViewModel{}
        , _leadName{}
        , _foeName{}
        , _moveName{}
        , _leadLevel{ 0 }
        , _leadHP{ 0 }
        , _leadMaxHP{ 0 }
        , _foeLevel{ 0 }
        , _foeHP{ 0 }
        , _remainingBallCount{ 0 }
        , _captureCount{ 0 }
        , _captureGoal{ 0 }
        , _exploreVisibility{ WidgetVisibility::Collapsed }
        , _restVisibility{ WidgetVisibility::Collapsed }
        , _battleVisibility{ WidgetVisibility::Collapsed }
        , _commandVisibility{ WidgetVisibility::Collapsed }
        , _switchVisibility{ WidgetVisibility::Collapsed }
        , _victoryVisibility{ WidgetVisibility::Collapsed }
        , _defeatVisibility{ WidgetVisibility::Collapsed }
    {
    }

    MonsterCollectorHUDViewModel::~MonsterCollectorHUDViewModel() = default;

    const TypeInfo* MonsterCollectorHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void MonsterCollectorHUDViewModel::setPhase( MonsterCollectorPhase phase, bool bAwaitingSwitch )
    {
        const bool bBattle = phase == MonsterCollectorPhase::Battle;
        (void)SW_SET_VIEW_MODEL_FIELD( _exploreVisibility, toHUDVisibility( phase == MonsterCollectorPhase::Explore ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _battleVisibility, toHUDVisibility( bBattle ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _commandVisibility, toHUDVisibility( bBattle && bAwaitingSwitch == false ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _switchVisibility, toHUDVisibility( bBattle && bAwaitingSwitch ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _victoryVisibility, toHUDVisibility( phase == MonsterCollectorPhase::Victory ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _defeatVisibility, toHUDVisibility( phase == MonsterCollectorPhase::Defeat ) );
    }

    void MonsterCollectorHUDViewModel::setLead( string_view name, int32 level, int32 hp, int32 maxHP )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _leadName, string( name ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _leadLevel, level );
        (void)SW_SET_VIEW_MODEL_FIELD( _leadHP, hp );
        (void)SW_SET_VIEW_MODEL_FIELD( _leadMaxHP, maxHP );
    }

    void MonsterCollectorHUDViewModel::setFoe( string_view name, int32 level, int32 hp )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _foeName, string( name ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _foeLevel, level );
        (void)SW_SET_VIEW_MODEL_FIELD( _foeHP, hp );
    }

    void MonsterCollectorHUDViewModel::setMoveName( string_view name )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _moveName, string( name ) );
    }

    void MonsterCollectorHUDViewModel::setCapture( int32 remainingBallCount, int32 captureCount, int32 captureGoal )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _remainingBallCount, remainingBallCount );
        (void)SW_SET_VIEW_MODEL_FIELD( _captureCount, captureCount );
        (void)SW_SET_VIEW_MODEL_FIELD( _captureGoal, captureGoal );
    }

    void MonsterCollectorHUDViewModel::setRestShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _restVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
