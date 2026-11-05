/**
 * @file UITestWidgets.h
 * @brief UI 레이아웃과 포커스 테스트가 함께 쓰는 잎 위젯과 트리 도우미입니다. 글꼴 · 디바이스 없이 돕니다.
 */
#pragma once
#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    /** @brief 정한 크기를 원하는 잎 위젯입니다. 측정과 포커스 변화 횟수를 셉니다. */
    class UITestLeaf final : public Widget
    {
    public:
        explicit UITestLeaf( const float2& size, bool bFocusable = false )
            : Widget{}
            , _size{ size }
            , _measureCount{ 0 }
            , _focusGainedCount{ 0 }
            , _focusLostCount{ 0 }
            , _bFocusable{ bFocusable }
        {
        }

        bool supportsFocus() const override { return _bFocusable != 0; }
        void onFocusChanged( bool bFocused ) override { ++( bFocused ? _focusGainedCount : _focusLostCount ); }

        void setSize( const float2& size )
        {
            _size = size;
            invalidate( WidgetDirty::kLayout );
        }
        uint32 getMeasureCount() const { return _measureCount; }
        uint32 getFocusGainedCount() const { return _focusGainedCount; }
        uint32 getFocusLostCount() const { return _focusLostCount; }

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override
        {
            (void)context;
            (void)availableSize;
            ++_measureCount;
            return _size;
        }

    private:
        float2         _size;
        mutable uint32 _measureCount;
        uint32         _focusGainedCount;
        uint32         _focusLostCount;
        uint8          _bFocusable : 1;
    };

    /** @brief 트리를 만들고 배치 결과를 확인하는 도우미입니다. */
    struct UITestUtil
    {
        /** @brief @p PanelType 을 트리의 루트로 놓습니다. */
        template <typename PanelType>
        static PanelType& setRoot( WidgetTree& tree )
        {
            unique_ptr<PanelType> root = make_unique<PanelType>();
            PanelType&            ref  = *root;
            tree.setRoot( std::move( root ) );
            return ref;
        }

        /** @brief @p PanelType 자식 패널을 붙입니다. */
        template <typename PanelType>
        static PanelType& addPanel( PanelWidget& parent, const WidgetLayoutSlot& slot = {} )
        {
            unique_ptr<PanelType> panel = make_unique<PanelType>();
            PanelType&            ref   = *panel;
            ref.setLayoutSlot( slot );
            parent.addChild( std::move( panel ) );
            return ref;
        }

        static UITestLeaf& addLeaf( PanelWidget& parent, const float2& size, const WidgetLayoutSlot& slot = {} )
        {
            unique_ptr<UITestLeaf> leaf = make_unique<UITestLeaf>( size );
            UITestLeaf&            ref  = *leaf;
            ref.setLayoutSlot( slot );
            parent.addChild( std::move( leaf ) );
            return ref;
        }

        /** @brief 부모 왼쪽 위 기준 (@p x, @p y) 에 @p width × @p height 로 놓는 캔버스 슬롯입니다. */
        static WidgetLayoutSlot makeCanvasSlot( float32 x, float32 y, float32 width, float32 height )
        {
            WidgetLayoutSlot slot;
            slot._offsetMin = float2{ x, y };
            slot._offsetMax = float2{ x + width, y + height };
            return slot;
        }

        /** @brief 캔버스에 이름 있는 포커스 가능 잎을 놓습니다. */
        static UITestLeaf& addFocusable( PanelWidget& parent, const utf8* pName, float32 x, float32 y, float32 width = 50.0f, float32 height = 50.0f )
        {
            unique_ptr<UITestLeaf> leaf = make_unique<UITestLeaf>( float2{ width, height }, true );
            UITestLeaf&            ref  = *leaf;
            ref.setName( hashed_string( pName ) );
            ref.setLayoutSlot( makeCanvasSlot( x, y, width, height ) );
            parent.addChild( std::move( leaf ) );
            return ref;
        }

        static UILayoutContext makeContext( const float2& viewportSize )
        {
            UILayoutContext context;
            context._viewportSize = viewportSize;
            return context;
        }

        static uint32 layout( WidgetTree& tree, const float2& viewportSize )
        {
            return UILayoutPass::update( tree, makeContext( viewportSize ) );
        }

        static void expectRect( const Widget& widget, float32 x, float32 y, float32 width, float32 height )
        {
            const WidgetGeometry& geometry = widget.getGeometry();
            SW_EXPECT_NEAR( x, geometry._position._x, 1e-3f );
            SW_EXPECT_NEAR( y, geometry._position._y, 1e-3f );
            SW_EXPECT_NEAR( width, geometry._size._x, 1e-3f );
            SW_EXPECT_NEAR( height, geometry._size._y, 1e-3f );
        }
    };
} // namespace sw
