#include "pch.h"

#include "Editor/Viewport/EditorGridUtil.h"

#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    EditorGridStats& EditorGridStats::get()
    {
        static EditorGridStats s_stats;
        return s_stats;
    }

    EditorGridLevel EditorGridUtil::selectLevel( float32 viewHeight )
    {
        const float32 height     = MathUtil::max( MathUtil::abs( viewHeight ), kLevelReferenceHeight );
        const float32 lod        = MathUtil::min( MathUtil::log( height / kLevelReferenceHeight ) / MathUtil::log( static_cast<float32>( kLevelRatio ) ),
                                                  static_cast<float32>( kMaxLevel ) );
        const float32 levelIndex = MathUtil::min( MathUtil::floor( lod ), static_cast<float32>( kMaxLevel ) );
        const float32 fraction   = lod - levelIndex;

        EditorGridLevel level{};
        level._step        = MathUtil::pow( static_cast<float32>( kLevelRatio ), levelIndex );
        level._coarseBlend = MathUtil::clamp( ( fraction - kBlendStart ) / ( 1.0f - kBlendStart ), 0.0f, 1.0f );
        level._radius      = kBaseRadius * MathUtil::pow( static_cast<float32>( kLevelRatio ), lod );
        return level;
    }

    EditorGridLineStyle EditorGridUtil::evaluateLine( int64 worldIndex, const EditorGridLevel& level )
    {
        const bool    bNextLevelLine = ( worldIndex % kLevelRatio ) == 0;
        const float32 majorNow       = isMajorLine( worldIndex ) ? 1.0f : 0.0f;
        const float32 majorNext      = ( worldIndex % ( kMajorEvery * kLevelRatio ) ) == 0 ? 1.0f : 0.0f;

        EditorGridLineStyle style{};
        style._visibility  = bNextLevelLine ? 1.0f : 1.0f - level._coarseBlend;
        style._majorWeight = MathUtil::lerp( majorNow, majorNext, level._coarseBlend );
        return style;
    }

    float32 EditorGridUtil::computeEdgeFade( float32 distance, float32 radius )
    {
        if ( radius <= 0.0f )
            return 0.0f;
        return 1.0f - MathUtil::smoothstep( kEdgeFadeStart * radius, radius, distance );
    }
} // namespace sw::editor
