#include "pch.h"

#include "Engine/UI/Layout/WrapPanel.h"

#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/UILayoutPass.h"

namespace sw
{
    namespace
    {
        struct WrapPanelInternal
        {
            /** @brief 자식의 (원하는 크기 + 여백) 주축 · 교차축 길이입니다. */
            static void computeExtent( UIOrientation orientation, const Widget& child, float32& outMain, float32& outCross )
            {
                const float4& padding = child.getLayoutSlot()._padding;
                outMain               = UILayoutPass::getMainAxis( orientation, child.getDesiredSize() ) + UILayoutPass::getMainPadding( orientation, padding );
                outCross              = UILayoutPass::getCrossAxis( orientation, child.getDesiredSize() ) + UILayoutPass::getCrossPadding( orientation, padding );
            }

            /** @brief 지금 줄(길이 @p lineMain)에 길이 @p itemMain 인 자식이 더 들어가지 못하는가. 줄의 첫 자식은 늘 들어간다. */
            static bool shouldBreak( float32 lineMain, float32 itemMain, float32 spacing, float32 mainAvailable, bool bLineEmpty )
            {
                if ( bLineEmpty || UILayoutPass::isUnbounded( mainAvailable ) )
                    return false;
                return lineMain + spacing + itemMain > mainAvailable;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    WrapPanel::WrapPanel()
        : PanelWidget{}
        , _itemSpacing{ 0.0f }
        , _lineSpacing{ 0.0f }
        , _orientation{ UIOrientation::Horizontal }
    {
    }

    WrapPanel::~WrapPanel() = default;

    const TypeInfo* WrapPanel::getTypeInfo() const
    {
        return StaticType();
    }

    void WrapPanel::setOrientation( UIOrientation orientation )
    {
        if ( _orientation == orientation )
            return;
        _orientation = orientation;
        invalidate( WidgetDirty::kLayout );
    }

    void WrapPanel::setItemSpacing( float32 itemSpacing )
    {
        if ( _itemSpacing == itemSpacing )
            return;
        _itemSpacing = itemSpacing;
        invalidate( WidgetDirty::kLayout );
    }

    void WrapPanel::setLineSpacing( float32 lineSpacing )
    {
        if ( _lineSpacing == lineSpacing )
            return;
        _lineSpacing = lineSpacing;
        invalidate( WidgetDirty::kLayout );
    }

    float2 WrapPanel::computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const
    {
        const float32 mainAvailable = UILayoutPass::getMainAxis( _orientation, availableSize );
        float32       maxLineMain   = 0.0f;
        float32       crossTotal    = 0.0f;
        float32       lineMain      = 0.0f;
        float32       lineCross     = 0.0f;
        uint32        lineCount     = 0;
        bool          bLineEmpty    = true;
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget& child = *getChild( index );
            if ( child.getVisibility() == WidgetVisibility::Collapsed )
                continue;
            const float4& padding = child.getLayoutSlot()._padding;
            (void)UILayoutPass::measure( child, context,
                                         UILayoutPass::makeAxisVector( _orientation,
                                                                       UILayoutPass::computeRemaining( mainAvailable, UILayoutPass::getMainPadding( _orientation, padding ) ),
                                                                       kUIUnbounded ) );
            float32 itemMain  = 0.0f;
            float32 itemCross = 0.0f;
            WrapPanelInternal::computeExtent( _orientation, child, itemMain, itemCross );
            if ( WrapPanelInternal::shouldBreak( lineMain, itemMain, _itemSpacing, mainAvailable, bLineEmpty ) )
            {
                maxLineMain = MathUtil::max( maxLineMain, lineMain );
                crossTotal += lineCross;
                lineMain   = 0.0f;
                lineCross  = 0.0f;
                bLineEmpty = true;
            }
            if ( bLineEmpty )
                ++lineCount;
            lineMain   = bLineEmpty ? itemMain : lineMain + _itemSpacing + itemMain;
            lineCross  = MathUtil::max( lineCross, itemCross );
            bLineEmpty = false;
        }
        maxLineMain = MathUtil::max( maxLineMain, lineMain );
        crossTotal += lineCross;
        if ( lineCount > 1 )
            crossTotal += _lineSpacing * static_cast<float32>( lineCount - 1 );
        return UILayoutPass::makeAxisVector( _orientation, maxLineMain, crossTotal );
    }

    void WrapPanel::arrangeChildren( const UILayoutContext& context, const float2& size )
    {
        const float32 mainSize   = UILayoutPass::getMainAxis( _orientation, size );
        float32       lineOffset = 0.0f;
        uint32        lineBegin  = 0;
        while ( lineBegin < getChildCount() )
        {
            // 한 줄의 끝과 줄 높이를 먼저 정하고, 그 높이를 교차축 슬롯으로 놓는다.
            float32 lineMain   = 0.0f;
            float32 lineCross  = 0.0f;
            bool    bLineEmpty = true;
            uint32  lineEnd    = lineBegin;
            for ( ; lineEnd < getChildCount(); ++lineEnd )
            {
                const Widget& child = *getChild( lineEnd );
                if ( child.getVisibility() == WidgetVisibility::Collapsed )
                    continue;
                float32 itemMain  = 0.0f;
                float32 itemCross = 0.0f;
                WrapPanelInternal::computeExtent( _orientation, child, itemMain, itemCross );
                if ( WrapPanelInternal::shouldBreak( lineMain, itemMain, _itemSpacing, mainSize, bLineEmpty ) )
                    break;
                lineMain   = bLineEmpty ? itemMain : lineMain + _itemSpacing + itemMain;
                lineCross  = MathUtil::max( lineCross, itemCross );
                bLineEmpty = false;
            }
            float32 cursor = 0.0f;
            for ( uint32 index = lineBegin; index < lineEnd; ++index )
            {
                Widget& child = *getChild( index );
                if ( child.getVisibility() == WidgetVisibility::Collapsed )
                    continue;
                float32 itemMain  = 0.0f;
                float32 itemCross = 0.0f;
                WrapPanelInternal::computeExtent( _orientation, child, itemMain, itemCross );
                arrangeChild( context, child, UILayoutPass::makeAxisVector( _orientation, cursor, lineOffset ),
                              UILayoutPass::makeAxisVector( _orientation, itemMain, lineCross ) );
                cursor += itemMain + _itemSpacing;
            }
            if ( bLineEmpty == false )
                lineOffset += lineCross + _lineSpacing;
            lineBegin = lineEnd;
        }
    }
} // namespace sw
