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

    WidgetVisibility CardBattleHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }

    void CardBattleHUDViewModel::setPlayerHP( int32 playerHP )
    {
        (void)setField( _playerHP, playerHP, "_playerHP" );
    }

    void CardBattleHUDViewModel::setPlayerBlock( int32 playerBlock )
    {
        (void)setField( _playerBlock, playerBlock, "_playerBlock" );
    }

    void CardBattleHUDViewModel::setEnemyHP( int32 enemyHP )
    {
        (void)setField( _enemyHP, enemyHP, "_enemyHP" );
    }

    void CardBattleHUDViewModel::setEnemyIntent( int32 enemyIntent )
    {
        (void)setField( _enemyIntent, enemyIntent, "_enemyIntent" );
    }

    void CardBattleHUDViewModel::setEnergy( int32 energy )
    {
        (void)setField( _energy, energy, "_energy" );
    }

    void CardBattleHUDViewModel::setTurn( int32 turn )
    {
        (void)setField( _turn, turn, "_turn" );
    }

    void CardBattleHUDViewModel::setCost( int32 cost )
    {
        (void)setField( _cost, cost, "_cost" );
    }

    void CardBattleHUDViewModel::setDrawCount( int32 drawCount )
    {
        (void)setField( _drawCount, drawCount, "_drawCount" );
    }

    void CardBattleHUDViewModel::setWonShown( bool bShown )
    {
        (void)setField( _wonVisibility, toVisibility( bShown ), "_wonVisibility" );
    }

    void CardBattleHUDViewModel::setLostShown( bool bShown )
    {
        (void)setField( _lostVisibility, toVisibility( bShown ), "_lostVisibility" );
    }
} // namespace sw
