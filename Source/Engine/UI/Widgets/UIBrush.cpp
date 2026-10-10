#include "pch.h"

#include "Engine/UI/Widgets/UIBrush.h"

namespace sw
{
    CanvasBrush UIBrush::makeCanvasBrush( const shared_ptr<const Texture2D>& image ) const
    {
        CanvasBrush brush{};
        brush._image           = image;
        brush._color           = _color;
        brush._borderColor     = _borderColor;
        brush._cornerRadius    = _cornerRadius;
        brush._nineSliceMargin = _nineSliceMargin;
        brush._borderWidth     = _borderWidth;
        return brush;
    }

    UIBrush UIBrush::makeSolid( const float4& color, float32 cornerRadius )
    {
        UIBrush brush{};
        brush._color        = color;
        brush._cornerRadius = float4{ cornerRadius, cornerRadius, cornerRadius, cornerRadius };
        return brush;
    }
} // namespace sw
