#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/Model/NetSimWorldLayout.h"

#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    float2 NetSimWorldLayout::computeViewSize( float2 available, int32 worldCount, float32 itemSpacingX, float32 lineHeight, float32 minHeight )
    {
        const int32 columnCount = MathUtil::max( 1, worldCount );
        return float2{ MathUtil::max( 1.0f, available._x / static_cast<float32>( columnCount ) - itemSpacingX ), MathUtil::max( minHeight, available._y - lineHeight * 2.0f ) };
    }
} // namespace sw::editor
