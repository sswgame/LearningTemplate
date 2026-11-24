#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/Client/RhythmHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    RhythmHUDViewModel::RhythmHUDViewModel()
        : UIViewModel{}
        , _score{ 0 }
        , _combo{ 0 }
        , _accuracy{ 0 }
        , _lifeRatio{ 0.0f }
        , _clearedVisibility{ WidgetVisibility::Collapsed }
        , _failedVisibility{ WidgetVisibility::Collapsed }
    {
    }

    RhythmHUDViewModel::~RhythmHUDViewModel() = default;

    const TypeInfo* RhythmHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    WidgetVisibility RhythmHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }

    void RhythmHUDViewModel::setScore( int32 score )
    {
        (void)setField( _score, score, "_score" );
    }

    void RhythmHUDViewModel::setCombo( int32 combo )
    {
        (void)setField( _combo, combo, "_combo" );
    }

    void RhythmHUDViewModel::setAccuracy( int32 accuracy )
    {
        (void)setField( _accuracy, accuracy, "_accuracy" );
    }

    void RhythmHUDViewModel::setLifeRatio( float32 lifeRatio )
    {
        (void)setField( _lifeRatio, MathUtil::clamp( lifeRatio, 0.0f, 1.0f ), "_lifeRatio" );
    }

    void RhythmHUDViewModel::setClearedShown( bool bShown )
    {
        (void)setField( _clearedVisibility, toVisibility( bShown ), "_clearedVisibility" );
    }

    void RhythmHUDViewModel::setFailedShown( bool bShown )
    {
        (void)setField( _failedVisibility, toVisibility( bShown ), "_failedVisibility" );
    }
} // namespace sw
