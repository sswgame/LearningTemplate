#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/Client/JRPGBattleHUDViewModel.h"

namespace sw
{
    JRPGBattleHUDViewModel::JRPGBattleHUDViewModel()
        : UIViewModel{}
        , _memberName{}
        , _targetName{}
        , _memberHP{ 0 }
        , _memberMaxHP{ 0 }
        , _memberMP{ 0 }
        , _targetHP{ 0 }
        , _gold{ 0 }
        , _exploreVisibility{ WidgetVisibility::Collapsed }
        , _restVisibility{ WidgetVisibility::Collapsed }
        , _battleVisibility{ WidgetVisibility::Collapsed }
        , _victoryVisibility{ WidgetVisibility::Collapsed }
        , _defeatVisibility{ WidgetVisibility::Collapsed }
    {
    }

    JRPGBattleHUDViewModel::~JRPGBattleHUDViewModel() = default;

    const TypeInfo* JRPGBattleHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    WidgetVisibility JRPGBattleHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }

    void JRPGBattleHUDViewModel::setPhase( JRPGGamePhase phase )
    {
        (void)setField( _exploreVisibility, toVisibility( phase == JRPGGamePhase::Explore ), "_exploreVisibility" );
        (void)setField( _battleVisibility, toVisibility( phase == JRPGGamePhase::Battle ), "_battleVisibility" );
        (void)setField( _victoryVisibility, toVisibility( phase == JRPGGamePhase::Victory ), "_victoryVisibility" );
        (void)setField( _defeatVisibility, toVisibility( phase == JRPGGamePhase::Defeat ), "_defeatVisibility" );
    }

    void JRPGBattleHUDViewModel::setMember( string_view name, int32 hp, int32 maxHP, int32 mp )
    {
        (void)setField( _memberName, string( name ), "_memberName" );
        (void)setField( _memberHP, hp, "_memberHP" );
        (void)setField( _memberMaxHP, maxHP, "_memberMaxHP" );
        (void)setField( _memberMP, mp, "_memberMP" );
    }

    void JRPGBattleHUDViewModel::setTarget( string_view name, int32 hp )
    {
        (void)setField( _targetName, string( name ), "_targetName" );
        (void)setField( _targetHP, hp, "_targetHP" );
    }

    void JRPGBattleHUDViewModel::setGold( int32 gold )
    {
        (void)setField( _gold, gold, "_gold" );
    }

    void JRPGBattleHUDViewModel::setRestShown( bool bShown )
    {
        (void)setField( _restVisibility, toVisibility( bShown ), "_restVisibility" );
    }
} // namespace sw
