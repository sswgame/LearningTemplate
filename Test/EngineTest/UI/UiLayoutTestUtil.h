/**
 * @file UiLayoutTestUtil.h
 * @brief 레이아웃 시험이 쓰는 위젯과 고정 장치(스위트 아님) — 고정 크기 위젯 · 줄 바꿈 흉내 위젯 · 트리 + 레이아웃 문맥.
 * @details 시험 위젯은 `REFLECT` 를 쓰지 않습니다(시험 타깃에 리플렉션 단계가 없다) — 덤프의 타입 이름은 `Widget` 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Layout/UiLayoutDump.h"
#include "Engine/UI/Layout/UiLayoutPass.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"

namespace sw::test
{
    /** @brief 원하는 크기가 고정된 잎 위젯입니다. `computeDesiredSize` 호출 수를 셉니다. */
    class TestFixedWidget : public Widget
    {
    public:
        TestFixedWidget( const hashed_string& name, const float2& size )
            : Widget{}
            , _size{ size }
            , _measureCount{ 0 }
        {
            setName( name );
        }

        void setSize( const float2& size )
        {
            _size = size;
            invalidate( WidgetDirty::kLayout );
        }
        uint32 getMeasureCount() const { return _measureCount; }

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override
        {
            (void)context;
            (void)availableSize;
            ++_measureCount;
            return _size;
        }

    private:
        float2         _size;
        mutable uint32 _measureCount;
    };
} // namespace sw::test

namespace sw::test
{
    /**
     * @brief 줄 바꿈 글을 흉내 내는 잎 위젯입니다 — 글자 하나 너비 10 · 줄 높이 20(둘 다 글자 배율을 곱한다).
     * @details 가용 너비가 있으면 그 너비로 줄을 나눠 높이 = ceil( 글자 수 × 10 / 너비 ) × 20, 없으면 한 줄입니다.
     */
    class TestWrapWidget : public Widget
    {
    public:
        TestWrapWidget( const hashed_string& name, uint32 charCount )
            : Widget{}
            , _charCount{ charCount }
        {
            setName( name );
        }

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override
        {
            const float32 charWidth  = 10.0f * context._textScale;
            const float32 lineHeight = 20.0f * context._textScale;
            const float32 fullWidth  = charWidth * static_cast<float32>( _charCount );
            if ( UiLayoutPass::isUnbounded( availableSize._x ) || availableSize._x <= 0.0f || fullWidth <= availableSize._x )
                return float2{ fullWidth, lineHeight };
            const float32 lineCount = MathUtil::ceil( fullWidth / availableSize._x );
            return float2{ availableSize._x, lineCount * lineHeight };
        }

    private:
        uint32 _charCount;
    };
} // namespace sw::test

namespace sw::test
{
    /** @brief 트리 하나 + 레이아웃 문맥입니다. 루트를 정하고 자식을 붙인 뒤 `update` → `dump` 로 견줍니다. */
    class UiLayoutFixture
    {
    public:
        UiLayoutFixture( float32 viewportWidth, float32 viewportHeight )
            : _tree{}
            , _context{}
        {
            _context._viewportSize = float2{ viewportWidth, viewportHeight };
        }

        template <typename PanelType>
        PanelType* setRoot( const hashed_string& name )
        {
            unique_ptr<PanelType> root  = make_unique<PanelType>();
            PanelType* const      pRoot = root.get();
            pRoot->setName( name );
            _tree.setRoot( std::move( root ) );
            return pRoot;
        }

        template <typename PanelType>
        PanelType* addPanel( PanelWidget* pParent, const hashed_string& name )
        {
            unique_ptr<PanelType> panel  = make_unique<PanelType>();
            PanelType* const      pPanel = panel.get();
            pPanel->setName( name );
            pParent->addChild( std::move( panel ) );
            return pPanel;
        }

        TestFixedWidget* addFixed( PanelWidget* pParent, const hashed_string& name, float32 width, float32 height, UiSizeRule sizeRule = UiSizeRule::Auto )
        {
            TestFixedWidget* const pWidget = static_cast<TestFixedWidget*>( pParent->addChild( make_unique<TestFixedWidget>( name, float2{ width, height } ) ) );
            WidgetLayoutSlot       slot    = pWidget->getLayoutSlot();
            slot._sizeRule                 = sizeRule;
            pWidget->setLayoutSlot( slot );
            return pWidget;
        }

        uint32 update() { return UiLayoutPass::update( _tree, _context ); }
        string dump( float32 physicalScale = 1.0f ) const { return UiLayoutDump::makeDump( _tree, physicalScale ); }

        WidgetTree&      getTree() { return _tree; }
        UiLayoutContext& getContext() { return _context; }

    private:
        WidgetTree      _tree;
        UiLayoutContext _context;
    };
} // namespace sw::test
