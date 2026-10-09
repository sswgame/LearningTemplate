#include "pch.h"

#include "Editor/Viewport/EditorViewTargetUtil.h"

#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    bool EditorViewTargetUtil::needsResize( uint32 currentWidth, uint32 currentHeight, uint32 targetWidth, uint32 targetHeight )
    {
        if ( targetWidth == 0 || targetHeight == 0 )
            return false;
        if ( currentWidth == 0 || currentHeight == 0 )
            return true;
        const int32 deltaWidth  = static_cast<int32>( targetWidth ) - static_cast<int32>( currentWidth );
        const int32 deltaHeight = static_cast<int32>( targetHeight ) - static_cast<int32>( currentHeight );
        return MathUtil::abs( deltaWidth ) > 1 || MathUtil::abs( deltaHeight ) > 1;
    }

    EditorViewRect EditorViewTargetUtil::fitViewImage( const float2& available, EditorGameViewAspect aspect )
    {
        EditorViewRect rect{};
        const float32  width  = MathUtil::floor( MathUtil::max( available._x, 0.0f ) );
        const float32  height = MathUtil::floor( MathUtil::max( available._y, 0.0f ) );
        if ( aspect != EditorGameViewAspect::Ratio16x9 || width <= 0.0f || height <= 0.0f )
        {
            rect._size = float2{ width, height };
            return rect;
        }
        constexpr float32 kRatio    = 16.0f / 9.0f;
        const float32     fitWidth  = MathUtil::floor( MathUtil::min( width, height * kRatio ) );
        const float32     fitHeight = MathUtil::floor( MathUtil::min( height, fitWidth / kRatio ) );
        rect._size                  = float2{ fitWidth, fitHeight };
        rect._offset                = float2{ MathUtil::floor( ( width - fitWidth ) * 0.5f ), MathUtil::floor( ( height - fitHeight ) * 0.5f ) };
        return rect;
    }

    const utf8* EditorViewTargetUtil::getAspectLabel( EditorGameViewAspect aspect )
    {
        switch ( aspect )
        {
            case EditorGameViewAspect::Free:
                return "Free Aspect";
            case EditorGameViewAspect::Ratio16x9:
                return "16:9";
            case EditorGameViewAspect::Count:
                break;
        }
        return "?";
    }
} // namespace sw::editor
