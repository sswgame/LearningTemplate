#include "pch.h"

#include "Engine/UI/Base/Widget.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Animation/UiStyleTransition.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Render/UiPaintPass.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/Style/WidgetStyle.h"

namespace sw
{
    namespace
    {
        /** @brief 위젯 번호를 주는 프로세스 전역 카운터입니다(Engine.dll 안이라 모듈 핫 리로드에도 이어진다). 0 은 무효 번호라 1 부터. */
        atomic<uint32> s_nextWidgetID{ 1 };
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
        , _paintCache{}
        , _computedStyle{}
        , _styleTransition{}
        , _styleAncestorKey{ 0 }
        , _geometry{}
        , _desiredSize{}
        , _lastAvailableSize{}
        , _lastSlotPosition{}
        , _lastSlotSize{}
        , _pParent{ nullptr }
        , _pTree{ nullptr }
        , _id{ s_nextWidgetID.fetch_add( 1, std::memory_order_relaxed ) }
        , _dirtyFlags{ WidgetDirty::kLayout | WidgetDirty::kPaint | WidgetDirty::kStyle }
        , _layoutSerial{ 0 }
        , _opacity{ 1.0f }
        , _visibility{ WidgetVisibility::Visible }
        , _flowDirection{ UiFlowDirection::Inherit }
        , _bEnabled{ true }
        , _bRightToLeft{ false }
        , _bHovered{ false }
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
        // 기하에 얹히는 값이다 — 이 위젯을 지난 슬롯 자리에 다시 놓아(measure 0) 자기와 자손 기하에 새 변환을 얹는다.
        invalidate( WidgetDirty::kTransform | WidgetDirty::kArrange );
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
        invalidate( WidgetDirty::kLayout | WidgetDirty::kPaint ); // z 순서도 슬롯이다 — 기하가 그대로여도 그리기 순서가 바뀐다
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

    uint32 Widget::computeStyleStates() const
    {
        uint32 states = UiStyleState::kNone;
        if ( _bHovered )
            states |= UiStyleState::kHover;
        if ( hasFocus() )
            states |= UiStyleState::kFocus;
        if ( isEnabledInHierarchy() == false )
            states |= UiStyleState::kDisabled;
        return states;
    }

    const UiComputedStyle* Widget::getComputedStyle() const
    {
        if ( _styleTransition != nullptr )
            return &_styleTransition->_shown;
        return _computedStyle.get();
    }

    float32 Widget::computeEffectiveOpacity() const
    {
        const UiComputedStyle* pStyle = getComputedStyle();
        if ( pStyle != nullptr && pStyle->has( UiStyleField::Opacity ) )
            return _opacity * pStyle->_value._opacity;
        return _opacity;
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

    void Widget::paintOverChildren( CanvasPainter& painter, const UiPaintContext& context ) const
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

    void Widget::onBoundPropertyChanged( const PropertyInfo& property )
    {
        const hashed_string& name = property._name;
        _opacity                  = MathUtil::clamp( _opacity, 0.0f, 1.0f ); // 세터와 같은 묶기(불투명도 칸이 아니면 그대로다)
        if ( name == hashed_string( "_opacity" ) )
            invalidate( WidgetDirty::kTransform );
        else if ( name == hashed_string( "_renderTransform" ) )
            invalidate( WidgetDirty::kTransform | WidgetDirty::kArrange ); // 세터와 같다 — 지난 슬롯 자리에 다시 놓는다
        else if ( name == hashed_string( "_visibility" ) )
            invalidate( WidgetDirty::kVisibility );
        else if ( name == hashed_string( "_bEnabled" ) )
            invalidate( WidgetDirty::kStyle | WidgetDirty::kPaint );
        else if ( name == hashed_string( "_styleClass" ) )
            invalidate( WidgetDirty::kStyle );
        else if ( name == hashed_string( "_flowDirection" ) )
            invalidate( WidgetDirty::kArrange );
        else
            invalidate( WidgetDirty::kLayout | WidgetDirty::kPaint );
    }

    void Widget::onTextRevisionChanged()
    {
    }

    void Widget::onInputGlyphsChanged()
    {
    }

    void Widget::notifyValueEdited( const hashed_string& propertyName )
    {
        UiScreen* pScreen = _pTree != nullptr ? _pTree->getScreen() : nullptr;
        if ( pScreen != nullptr )
            pScreen->onWidgetValueEdited( *this, propertyName );
    }

    void Widget::attachToTree( WidgetTree* pTree, PanelWidget* pParent )
    {
        _pParent = pParent;
        _pTree   = pTree;
        if ( pTree == nullptr )
            return;
        pTree->registerWidget( *this );
        // 떨어져 있는 동안 쌓인 무효화를 트리의 목록으로 옮긴다(새 위젯은 생성자에서 레이아웃 · 그리기 · 스타일이 더럽다).
        // 스타일은 늘 다시 맞춘다 — 선택자가 조상을 보므로 다른 자리에 붙으면 결과가 달라진다.
        const uint32 pendingDirty = ( _dirtyFlags & ~( WidgetDirty::kChildLayout | WidgetDirty::kLayoutRoot ) ) | WidgetDirty::kStyle;
        _dirtyFlags               = WidgetDirty::kNone;
        pTree->notifyDirty( *this, pendingDirty );
        onAttachedToTree();
        PanelWidget* pPanel = castTo<PanelWidget>( this );
        if ( pPanel != nullptr )
        {
            for ( const unique_ptr<Widget>& child : pPanel->_listChild )
            {
                child->attachToTree( pTree, pPanel );
            }
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
            {
                child->detachFromTree();
            }
        }
        onDetachedFromTree();
        _styleTransition.reset(); // 트리의 전환 목록은 다음 진행에서 이 번호를 버린다
        _pTree->unregisterWidget( *this );
        _pTree = nullptr;
    }
} // namespace sw
