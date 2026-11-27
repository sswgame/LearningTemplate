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

    void RhythmHUDViewModel::setScore( int32 score )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _score, score );
    }

    void RhythmHUDViewModel::setCombo( int32 combo )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _combo, combo );
    }

    void RhythmHUDViewModel::setAccuracy( int32 accuracy )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _accuracy, accuracy );
    }

    void RhythmHUDViewModel::setLifeRatio( float32 lifeRatio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _lifeRatio, MathUtil::clamp( lifeRatio, 0.0f, 1.0f ) );
    }

    void RhythmHUDViewModel::setClearedShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _clearedVisibility, toHUDVisibility( bShown ) );
    }

    void RhythmHUDViewModel::setFailedShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _failedVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
