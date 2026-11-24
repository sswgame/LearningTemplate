#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Client/BattleRoyaleHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    BattleRoyaleHUDViewModel::BattleRoyaleHUDViewModel()
        : UIViewModel{}
        , _healthRatio{ 1.0f }
        , _remaining{ 0 }
        , _zoneSeconds{ 0 }
        , _dropHintVisibility{ WidgetVisibility::Collapsed }
        , _downedVisibility{ WidgetVisibility::Collapsed }
        , _winVisibility{ WidgetVisibility::Collapsed }
        , _loseVisibility{ WidgetVisibility::Collapsed }
    {
    }

    BattleRoyaleHUDViewModel::~BattleRoyaleHUDViewModel() = default;

    const TypeInfo* BattleRoyaleHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void BattleRoyaleHUDViewModel::setHealth( float32 ratio )
    {
        (void)setField( _healthRatio, MathUtil::clamp( ratio, 0.0f, 1.0f ), "_healthRatio" );
    }

    void BattleRoyaleHUDViewModel::setRemaining( int32 remaining )
    {
        (void)setField( _remaining, remaining, "_remaining" );
    }

    void BattleRoyaleHUDViewModel::setZoneSeconds( float32 seconds )
    {
        (void)setField( _zoneSeconds, static_cast<int32>( MathUtil::ceil( seconds ) ), "_zoneSeconds" );
    }

    void BattleRoyaleHUDViewModel::setHints( bool bDropHint, bool bDowned )
    {
        (void)setField( _dropHintVisibility, toVisibility( bDropHint ), "_dropHintVisibility" );
        (void)setField( _downedVisibility, toVisibility( bDowned ), "_downedVisibility" );
    }

    void BattleRoyaleHUDViewModel::setResult( bool bWon, bool bLost )
    {
        (void)setField( _winVisibility, toVisibility( bWon ), "_winVisibility" );
        (void)setField( _loseVisibility, toVisibility( bLost ), "_loseVisibility" );
    }

    WidgetVisibility BattleRoyaleHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
