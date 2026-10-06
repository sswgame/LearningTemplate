#include "pch.h"

#include "Engine/UI/Core/Widget.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/WidgetTree.h"

namespace sw
{
    namespace
    {
        /** @brief 위젯 번호를 주는 프로세스 전역 카운터입니다(Engine.dll 안이라 모듈 핫 리로드에도 이어진다). 0 은 무효 번호라 1 부터. */
        atomic<uint32> s_nextWidgetId{ 1 };
    } // namespace
} // namespace sw

namespace sw
{
    Widget::Widget()
        : _name{}
        , _styleClass{}
        , _renderTransform{}
        , _slot{}
        , _navigation{}
        , _geometry{}
        , _desiredSize{}
        , _lastAvailableSize{}
        , _lastSlotPosition{}
        , _lastSlotSize{}
        , _pParent{ nullptr }
        , _pTree{ nullptr }
        , _id{ s_nextWidgetId.fetch_add( 1, std::memory_order_relaxed ) }
        , _dirtyFlags{ WidgetDirty::kLayout | WidgetDirty::kPaint | WidgetDirty::kStyle }
        , _layoutSerial{ 0 }
        , _opacity{ 1.0f }
        , _visibility{ WidgetVisibility::Visible }
        , _flowDirection{ UiFlowDirection::Inherit }
        , _bEnabled{ true }
        , _bRightToLeft{ false }
    {
    }

    Widget::~Widget() = default;

    const TypeInfo* Widget::getTypeInfo() const
    {
        return StaticType();
    }

    void Widget::setName( const hashed_string& name )
    {
        if ( _name.isEqual( name, NameCase::CaseSensitive ) )
            return;
        WidgetTree* pTree = _pTree;
        if ( pTree != nullptr )
            pTree->unregisterName( *this );
        _name = name;
        if ( pTree != nullptr )
            pTree->registerName( *this );
        invalidate( WidgetDirty::kStyle ); // 선택자 #이름 이 바뀐다
    }

    void Widget::setStyleClass( const string& styleClass )
    {
        if ( _styleClass == styleClass )
            return;
        _styleClass = styleClass;
        invalidate( WidgetDirty::kStyle );
    }

    void Widget::setVisibility( WidgetVisibility visibility )
    {
        if ( _visibility == visibility )
            return;
        _visibility = visibility;
        invalidate( WidgetDirty::kVisibility );
    }

    bool Widget::isVisible() const
    {
        return _visibility != WidgetVisibility::Collapsed && _visibility != WidgetVisibility::Hidden;
    }

    void Widget::setEnabled( bool bEnabled )
    {
        if ( _bEnabled == bEnabled )
            return;
        _bEnabled = bEnabled;
        invalidate( WidgetDirty::kStyle | WidgetDirty::kPaint ); // 꺼짐 상태 스타일
    }

    bool Widget::isEnabledInHierarchy() const
    {
        for ( const Widget* pWidget = this; pWidget != nullptr; pWidget = pWidget->_pParent )
        {
            if ( pWidget->_bEnabled == false )
                return false;
        }
        return true;
    }

    void Widget::setOpacity( float32 opacity )
    {
        const float32 clamped = MathUtil::clamp( opacity, 0.0f, 1.0f );
        if ( _opacity == clamped )
            return;
        _opacity = clamped;
        invalidate( WidgetDirty::kTransform );
    }

    void Widget::setRenderTransform( const WidgetRenderTransform& transform )
    {
        if ( _renderTransform == transform )
            return;
        _renderTransform = transform;
        invalidate( WidgetDirty::kTransform );
    }

    void Widget::setFlowDirection( UiFlowDirection flowDirection )
    {
        if ( _flowDirection == flowDirection )
            return;
        _flowDirection = flowDirection;
        invalidate( WidgetDirty::kArrange );
    }

    void Widget::setLayoutSlot( const WidgetLayoutSlot& slot )
    {
        _slot = slot;
        invalidate( WidgetDirty::kLayout );
    }

    void Widget::invalidate( uint32 dirtyReason )
    {
        if ( dirtyReason == WidgetDirty::kNone )
            return;
        if ( _pTree != nullptr )
            _pTree->notifyDirty( *this, dirtyReason );
        else
            _dirtyFlags |= dirtyReason;
    }

    void Widget::setArrangedGeometry( const WidgetGeometry& geometry )
    {
        if ( _geometry == geometry )
            return;
        _geometry = geometry;
        invalidate( WidgetDirty::kPaint );
    }

    bool Widget::hasFocus() const
    {
        return _pTree != nullptr && _pTree->getFocusedWidget() == _id;
    }

    UiReply Widget::onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase )
    {
        (void)event;
        (void)phase;
        return UiReply::makeUnhandled();
    }

    UiReply Widget::onActionEvent( const UiActionEvent& event, UiRoutePhase phase )
    {
        (void)event;
        (void)phase;
        return UiReply::makeUnhandled();
    }

    UiReply Widget::onTextEvent( const UiTextEvent& event )
    {
        (void)event;
        return UiReply::makeUnhandled();
    }

    void Widget::onFocusChanged( bool bFocused )
    {
        (void)bFocused;
    }

    void Widget::onHoverChanged( bool bHovered )
    {
        (void)bHovered;
    }

    float2 Widget::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        (void)context;
        (void)availableSize;
        return float2{};
    }

    void Widget::paint( CanvasPainter& painter, const UiPaintContext& context ) const
    {
        (void)painter;
        (void)context;
    }

    void Widget::onAttachedToTree()
    {
    }

    void Widget::onDetachedFromTree()
    {
    }

    void Widget::attachToTree( WidgetTree* pTree, PanelWidget* pParent )
    {
        _pParent = pParent;
        _pTree   = pTree;
        if ( pTree == nullptr )
            return;
        pTree->registerWidget( *this );
        // 떨어져 있는 동안 쌓인 무효화를 트리의 목록으로 옮긴다(새 위젯은 생성자에서 레이아웃 · 그리기 · 스타일이 더럽다).
        const uint32 pendingDirty = _dirtyFlags & ~( WidgetDirty::kChildLayout | WidgetDirty::kLayoutRoot );
        _dirtyFlags               = WidgetDirty::kNone;
        pTree->notifyDirty( *this, pendingDirty );
        onAttachedToTree();
        PanelWidget* pPanel = castTo<PanelWidget>( this );
        if ( pPanel != nullptr )
        {
            for ( const unique_ptr<Widget>& child : pPanel->_listChild )
                child->attachToTree( pTree, pPanel );
        }
    }

    void Widget::detachFromTree()
    {
        if ( _pTree == nullptr )
            return;
        PanelWidget* pPanel = castTo<PanelWidget>( this );
        if ( pPanel != nullptr )
        {
            for ( const unique_ptr<Widget>& child : pPanel->_listChild )
                child->detachFromTree();
        }
        onDetachedFromTree();
        _pTree->unregisterWidget( *this );
        _pTree = nullptr;
    }
} // namespace sw
