#include "pch.h"

#include "Engine/UI/Layout/CanvasPanel.h"

#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/UiLayoutPass.h"

namespace sw
{
    namespace
    {
        struct CanvasPanelInternal
        {
            /** @brief 자동 크기의 변을 정합니다 — End 는 왼쪽(위) 변을 두고 오른쪽(아래)을, Begin 은 반대로, Both 는 가운데를 두고 양쪽으로. */
            static void growEdges( UiGrowDirection direction, float32 length, float32& inoutBegin, float32& inoutEnd )
            {
                switch ( direction )
                {
                    case UiGrowDirection::End:
                    {
                        inoutEnd = inoutBegin + length;
                        break;
                    }
                    case UiGrowDirection::Begin:
                    {
                        inoutBegin = inoutEnd - length;
                        break;
                    }
                    case UiGrowDirection::Both:
                    {
                        const float32 center = ( inoutBegin + inoutEnd ) * 0.5f;
                        inoutBegin           = center - length * 0.5f;
                        inoutEnd             = center + length * 0.5f;
                        break;
                    }
                }
            }

            /** @brief 앵커 · 오프셋이 주는 한 축의 길이입니다. 패널 크기가 무한이고 앵커가 벌어져 있으면 무한입니다. */
            static float32 computeSpan( float32 anchorMin, float32 anchorMax, float32 offsetMin, float32 offsetMax, float32 panelLength )
            {
                const bool bStretched = anchorMin != anchorMax;
                if ( bStretched && UiLayoutPass::isUnbounded( panelLength ) )
                    return kUiUnbounded;
                const float32 anchorSpan = bStretched ? ( anchorMax - anchorMin ) * panelLength : 0.0f;
                return MathUtil::max( 0.0f, anchorSpan + offsetMax - offsetMin );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CanvasPanel::CanvasPanel()
        : PanelWidget{}
    {
    }

    CanvasPanel::~CanvasPanel() = default;

    const TypeInfo* CanvasPanel::getTypeInfo() const
    {
        return StaticType();
    }

    void CanvasPanel::collectPaintOrder( vector<uint32>& outListIndex ) const
    {
        outListIndex.clear();
        // 삽입 정렬 — 안정 정렬이고 자식 수가 작다.
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            const int16 zOrder = getChild( index )->getLayoutSlot()._zOrder;
            uint32      at     = static_cast<uint32>( outListIndex.size() );
            while ( at > 0 && getChild( outListIndex[at - 1] )->getLayoutSlot()._zOrder > zOrder )
                --at;
            outListIndex.insert( outListIndex.begin() + at, index );
        }
    }

    float2 CanvasPanel::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        float2 extent{};
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            const WidgetLayoutSlot& slot = child.getLayoutSlot();
            const float32           padX = slot._padding._x + slot._padding._z;
            const float32           padY = slot._padding._y + slot._padding._w;
            float2                  childAvailable{ kUiUnbounded, kUiUnbounded };
            if ( slot._bAutoSize == false )
            {
                childAvailable._x = UiLayoutPass::computeRemaining(
                    CanvasPanelInternal::computeSpan( slot._anchorMin._x, slot._anchorMax._x, slot._offsetMin._x, slot._offsetMax._x, availableSize._x ), padX );
                childAvailable._y = UiLayoutPass::computeRemaining(
                    CanvasPanelInternal::computeSpan( slot._anchorMin._y, slot._anchorMax._y, slot._offsetMin._y, slot._offsetMax._y, availableSize._y ), padY );
            }
            const float2 desired = UiLayoutPass::measure( child, context, childAvailable );

            const bool bTopLeftAnchored = slot._anchorMin._x == 0.0f && slot._anchorMin._y == 0.0f && slot._anchorMax._x == 0.0f && slot._anchorMax._y == 0.0f;
            if ( bTopLeftAnchored == false )
                continue;
            const float32 right  = slot._bAutoSize ? slot._offsetMin._x + desired._x + padX : slot._offsetMax._x;
            const float32 bottom = slot._bAutoSize ? slot._offsetMin._y + desired._y + padY : slot._offsetMax._y;
            extent._x            = MathUtil::max( extent._x, right );
            extent._y            = MathUtil::max( extent._y, bottom );
        }
        return extent;
    }

    void CanvasPanel::arrangeChildren( const UiLayoutContext& context, const float2& size )
    {
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            const WidgetLayoutSlot& slot   = child.getLayoutSlot();
            float32                 left   = slot._anchorMin._x * size._x + slot._offsetMin._x;
            float32                 top    = slot._anchorMin._y * size._y + slot._offsetMin._y;
            float32                 right  = slot._anchorMax._x * size._x + slot._offsetMax._x;
            float32                 bottom = slot._anchorMax._y * size._y + slot._offsetMax._y;
            if ( slot._bAutoSize )
            {
                const float2 desired = child.getDesiredSize();
                CanvasPanelInternal::growEdges( slot._growHorizontal, desired._x + slot._padding._x + slot._padding._z, left, right );
                CanvasPanelInternal::growEdges( slot._growVertical, desired._y + slot._padding._y + slot._padding._w, top, bottom );
            }
            arrangeChild( context, child, float2{ left, top }, float2{ MathUtil::max( 0.0f, right - left ), MathUtil::max( 0.0f, bottom - top ) } );
        }
    }
} // namespace sw
