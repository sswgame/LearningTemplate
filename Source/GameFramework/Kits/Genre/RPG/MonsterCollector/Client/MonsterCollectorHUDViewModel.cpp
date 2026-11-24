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

    WidgetVisibility MonsterCollectorHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }

    void MonsterCollectorHUDViewModel::setPhase( MonsterCollectorPhase phase, bool bAwaitingSwitch )
    {
        const bool bBattle = phase == MonsterCollectorPhase::Battle;
        (void)setField( _exploreVisibility, toVisibility( phase == MonsterCollectorPhase::Explore ), "_exploreVisibility" );
        (void)setField( _battleVisibility, toVisibility( bBattle ), "_battleVisibility" );
        (void)setField( _commandVisibility, toVisibility( bBattle && bAwaitingSwitch == false ), "_commandVisibility" );
        (void)setField( _switchVisibility, toVisibility( bBattle && bAwaitingSwitch ), "_switchVisibility" );
        (void)setField( _victoryVisibility, toVisibility( phase == MonsterCollectorPhase::Victory ), "_victoryVisibility" );
        (void)setField( _defeatVisibility, toVisibility( phase == MonsterCollectorPhase::Defeat ), "_defeatVisibility" );
    }

    void MonsterCollectorHUDViewModel::setLead( string_view name, int32 level, int32 hp, int32 maxHP )
    {
        (void)setField( _leadName, string( name ), "_leadName" );
        (void)setField( _leadLevel, level, "_leadLevel" );
        (void)setField( _leadHP, hp, "_leadHP" );
        (void)setField( _leadMaxHP, maxHP, "_leadMaxHP" );
    }

    void MonsterCollectorHUDViewModel::setFoe( string_view name, int32 level, int32 hp )
    {
        (void)setField( _foeName, string( name ), "_foeName" );
        (void)setField( _foeLevel, level, "_foeLevel" );
        (void)setField( _foeHP, hp, "_foeHP" );
    }

    void MonsterCollectorHUDViewModel::setMoveName( string_view name )
    {
        (void)setField( _moveName, string( name ), "_moveName" );
    }

    void MonsterCollectorHUDViewModel::setCapture( int32 remainingBallCount, int32 captureCount, int32 captureGoal )
    {
        (void)setField( _remainingBallCount, remainingBallCount, "_remainingBallCount" );
        (void)setField( _captureCount, captureCount, "_captureCount" );
        (void)setField( _captureGoal, captureGoal, "_captureGoal" );
    }

    void MonsterCollectorHUDViewModel::setRestShown( bool bShown )
    {
        (void)setField( _restVisibility, toVisibility( bShown ), "_restVisibility" );
    }
} // namespace sw
