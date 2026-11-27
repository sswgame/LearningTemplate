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
        , _skillPoints{ 0 }
        , _signIntensity{ 0 }
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

    void MonsterHuntHUDViewModel::setPhase( MonsterHuntPhase phase )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _haggleVisibility, toHUDVisibility( phase == MonsterHuntPhase::Haggle ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _investigateVisibility, toHUDVisibility( phase == MonsterHuntPhase::Investigate ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _huntVisibility, toHUDVisibility( phase == MonsterHuntPhase::Hunt ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _victoryVisibility, toHUDVisibility( phase == MonsterHuntPhase::Victory ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _defeatVisibility, toHUDVisibility( phase == MonsterHuntPhase::Defeat ) );
    }

    void MonsterHuntHUDViewModel::setReward( int32 reward )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _reward, reward );
    }

    void MonsterHuntHUDViewModel::setHunter( int32 hp, int32 stamina )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _hunterHP, hp );
        (void)SW_SET_VIEW_MODEL_FIELD( _stamina, stamina );
    }

    void MonsterHuntHUDViewModel::setMonster( int32 hp, bool bWeaknessKnown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _monsterHP, hp );
        (void)SW_SET_VIEW_MODEL_FIELD( _weaknessVisibility, toHUDVisibility( bWeaknessKnown ) );
    }

    void MonsterHuntHUDViewModel::setAlchemy( int32 oilHits, int32 toxicity )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _oilHits, oilHits );
        (void)SW_SET_VIEW_MODEL_FIELD( _toxicity, toxicity );
    }

    void MonsterHuntHUDViewModel::setSkills( int32 skillPoints, int32 signIntensity )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _skillPoints, skillPoints );
        (void)SW_SET_VIEW_MODEL_FIELD( _signIntensity, signIntensity );
    }

    void MonsterHuntHUDViewModel::setClues( int32 foundCount, bool bNearby )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _foundClueCount, foundCount );
        (void)SW_SET_VIEW_MODEL_FIELD( _senseVisibility, toHUDVisibility( bNearby ) );
    }
} // namespace sw
