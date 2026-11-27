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
        , _leaderAttack{ 0 }
        , _timingRatio{ 0.0f }
        , _timingVisibility{ WidgetVisibility::Collapsed }
        , _swingVisibility{ WidgetVisibility::Collapsed }
        , _blockVisibility{ WidgetVisibility::Collapsed }
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

    void JRPGBattleHUDViewModel::setPhase( JRPGGamePhase phase )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _exploreVisibility, toHUDVisibility( phase == JRPGGamePhase::Explore ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _battleVisibility, toHUDVisibility( phase == JRPGGamePhase::Battle ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _timingVisibility, toHUDVisibility( phase == JRPGGamePhase::Timing ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _victoryVisibility, toHUDVisibility( phase == JRPGGamePhase::Victory ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _defeatVisibility, toHUDVisibility( phase == JRPGGamePhase::Defeat ) );
    }

    void JRPGBattleHUDViewModel::setMember( string_view name, int32 hp, int32 maxHP, int32 mp )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _memberName, string( name ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _memberHP, hp );
        (void)SW_SET_VIEW_MODEL_FIELD( _memberMaxHP, maxHP );
        (void)SW_SET_VIEW_MODEL_FIELD( _memberMP, mp );
    }

    void JRPGBattleHUDViewModel::setTarget( string_view name, int32 hp )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _targetName, string( name ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _targetHP, hp );
    }

    void JRPGBattleHUDViewModel::setGold( int32 gold )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _gold, gold );
    }

    void JRPGBattleHUDViewModel::setTiming( float32 ratio, bool bBlock )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _timingRatio, ratio );
        (void)SW_SET_VIEW_MODEL_FIELD( _swingVisibility, toHUDVisibility( bBlock == false ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _blockVisibility, toHUDVisibility( bBlock ) );
    }

    void JRPGBattleHUDViewModel::setLeaderAttack( int32 attack )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _leaderAttack, attack );
    }

    void JRPGBattleHUDViewModel::setRestShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _restVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
