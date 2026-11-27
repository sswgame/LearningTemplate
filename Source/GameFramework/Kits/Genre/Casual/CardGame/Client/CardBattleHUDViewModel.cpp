#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Client/CardBattleHUDViewModel.h"

namespace sw
{
    CardBattleHUDViewModel::CardBattleHUDViewModel()
        : UIViewModel{}
        , _playerHP{ 0 }
        , _playerBlock{ 0 }
        , _enemyHP{ 0 }
        , _enemyIntent{ 0 }
        , _energy{ 0 }
        , _turn{ 0 }
        , _cost{ 0 }
        , _drawCount{ 0 }
        , _wonVisibility{ WidgetVisibility::Collapsed }
        , _lostVisibility{ WidgetVisibility::Collapsed }
    {
    }

    CardBattleHUDViewModel::~CardBattleHUDViewModel() = default;

    const TypeInfo* CardBattleHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void CardBattleHUDViewModel::setPlayerHP( int32 playerHP )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _playerHP, playerHP );
    }

    void CardBattleHUDViewModel::setPlayerBlock( int32 playerBlock )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _playerBlock, playerBlock );
    }

    void CardBattleHUDViewModel::setEnemyHP( int32 enemyHP )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _enemyHP, enemyHP );
    }

    void CardBattleHUDViewModel::setEnemyIntent( int32 enemyIntent )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _enemyIntent, enemyIntent );
    }

    void CardBattleHUDViewModel::setEnergy( int32 energy )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _energy, energy );
    }

    void CardBattleHUDViewModel::setTurn( int32 turn )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _turn, turn );
    }

    void CardBattleHUDViewModel::setCost( int32 cost )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _cost, cost );
    }

    void CardBattleHUDViewModel::setDrawCount( int32 drawCount )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _drawCount, drawCount );
    }

    void CardBattleHUDViewModel::setWonShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _wonVisibility, toHUDVisibility( bShown ) );
    }

    void CardBattleHUDViewModel::setLostShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _lostVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
