#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Client/MonsterHuntHUDViewModel.h"

namespace sw
{
    MonsterHuntHUDViewModel::MonsterHuntHUDViewModel()
        : UIViewModel{}
        , _reward{ 0 }
        , _hunterHP{ 0 }
        , _stamina{ 0 }
        , _monsterHP{ 0 }
        , _oilHits{ 0 }
        , _toxicity{ 0 }
        , _foundClueCount{ 0 }
        , _haggleVisibility{ WidgetVisibility::Collapsed }
        , _investigateVisibility{ WidgetVisibility::Collapsed }
        , _senseVisibility{ WidgetVisibility::Collapsed }
        , _huntVisibility{ WidgetVisibility::Collapsed }
        , _weaknessVisibility{ WidgetVisibility::Collapsed }
        , _victoryVisibility{ WidgetVisibility::Collapsed }
        , _defeatVisibility{ WidgetVisibility::Collapsed }
    {
    }

    MonsterHuntHUDViewModel::~MonsterHuntHUDViewModel() = default;

    const TypeInfo* MonsterHuntHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    WidgetVisibility MonsterHuntHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }

    void MonsterHuntHUDViewModel::setPhase( MonsterHuntPhase phase )
    {
        (void)setField( _haggleVisibility, toVisibility( phase == MonsterHuntPhase::Haggle ), "_haggleVisibility" );
        (void)setField( _investigateVisibility, toVisibility( phase == MonsterHuntPhase::Investigate ), "_investigateVisibility" );
        (void)setField( _huntVisibility, toVisibility( phase == MonsterHuntPhase::Hunt ), "_huntVisibility" );
        (void)setField( _victoryVisibility, toVisibility( phase == MonsterHuntPhase::Victory ), "_victoryVisibility" );
        (void)setField( _defeatVisibility, toVisibility( phase == MonsterHuntPhase::Defeat ), "_defeatVisibility" );
    }

    void MonsterHuntHUDViewModel::setReward( int32 reward )
    {
        (void)setField( _reward, reward, "_reward" );
    }

    void MonsterHuntHUDViewModel::setHunter( int32 hp, int32 stamina )
    {
        (void)setField( _hunterHP, hp, "_hunterHP" );
        (void)setField( _stamina, stamina, "_stamina" );
    }

    void MonsterHuntHUDViewModel::setMonster( int32 hp, bool bWeaknessKnown )
    {
        (void)setField( _monsterHP, hp, "_monsterHP" );
        (void)setField( _weaknessVisibility, toVisibility( bWeaknessKnown ), "_weaknessVisibility" );
    }

    void MonsterHuntHUDViewModel::setAlchemy( int32 oilHits, int32 toxicity )
    {
        (void)setField( _oilHits, oilHits, "_oilHits" );
        (void)setField( _toxicity, toxicity, "_toxicity" );
    }

    void MonsterHuntHUDViewModel::setClues( int32 foundCount, bool bNearby )
    {
        (void)setField( _foundClueCount, foundCount, "_foundClueCount" );
        (void)setField( _senseVisibility, toVisibility( bNearby ), "_senseVisibility" );
    }
} // namespace sw
