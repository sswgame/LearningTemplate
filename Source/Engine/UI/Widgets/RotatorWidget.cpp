#include "pch.h"

#include "Engine/UI/Widgets/RotatorWidget.h"

#include "Core/Common/Defines.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"
#include "Engine/UI/Base/UIEvents.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    namespace
    {
        struct RotatorWidgetInternal
        {
            /** @brief 양쪽 화살표 영역의 너비(안쪽 여백)입니다. */
            static constexpr float32 kArrowSpace = 32.0f;
            /** @brief 화살표 삼각형의 높이(가로 막대 수), 막대 하나의 두께, 가운데 막대의 길이(막대마다 둘씩 줄어든다)입니다. */
            static constexpr uint32  kArrowSteps     = 5;
            static constexpr float32 kArrowThickness = 2.0f;
            static constexpr float32 kArrowBaseWidth = 6.0f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    RotatorWidget::RotatorWidget()
        : ButtonWidget{}
        , _onSelectionChanged{}
        , _pLabel{ nullptr }
        , _releaseLocalX{ 0.0f }
        , _bHasReleasePoint{ false }
        , _listOption{}
        , _selectedIndex{ invalid_index::kUint32 }
    {
        setContentPadding( float4{ RotatorWidgetInternal::kArrowSpace, 8.0f, RotatorWidgetInternal::kArrowSpace, 8.0f } );
        unique_ptr<TextWidget> label = make_unique<TextWidget>();
        TextLayoutStyle        style = label->getTextStyle();
        style._bWrap                 = false;
        style._overflow              = TextOverflow::Ellipsis;
        style._alignment             = TextAlignment::Center;
        label->setTextStyle( style );
        _pLabel = static_cast<TextWidget*>( addChild( std::move( label ) ) );
    }

    RotatorWidget::~RotatorWidget() = default;

    const TypeInfo* RotatorWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void RotatorWidget::setOptions( const vector<string>& listOption )
    {
        _listOption = listOption;
        if ( _selectedIndex != invalid_index::kUint32 && _selectedIndex >= _listOption.size() )
            _selectedIndex = invalid_index::kUint32;
        refreshLabel();
    }

    void RotatorWidget::setSelectedIndex( uint32 index )
    {
        _selectedIndex = index < _listOption.size() ? index : invalid_index::kUint32;
        refreshLabel();
    }

    void RotatorWidget::onBoundPropertyChanged( const PropertyInfo& property )
    {
        if ( property._name == hashed_string( "_listOption" ) || property._name == hashed_string( "_selectedIndex" ) )
        {
            if ( _selectedIndex != invalid_index::kUint32 && _selectedIndex >= _listOption.size() )
                _selectedIndex = invalid_index::kUint32;
            refreshLabel();
            return;
        }
        ButtonWidget::onBoundPropertyChanged( property );
    }

    void RotatorWidget::refreshLabel()
    {
        _pLabel->setText( _selectedIndex != invalid_index::kUint32 ? string_view{ _listOption[_selectedIndex] } : string_view{} );
        invalidate( WidgetDirty::kPaint ); // 화살표의 켜짐은 항목 수에 따른다
    }

    bool RotatorWidget::stepSelection( int32 step )
    {
        const int32 count = static_cast<int32>( _listOption.size() );
        if ( count < 2 || step == 0 )
            return false;
        const int32 current = _selectedIndex == invalid_index::kUint32 ? ( step > 0 ? -1 : 0 ) : static_cast<int32>( _selectedIndex );
        const int32 next    = ( ( current + step ) % count + count ) % count;
        setSelectedIndex( static_cast<uint32>( next ) );
        _onSelectionChanged.broadcast( _selectedIndex );
        notifyValueEdited( "_selectedIndex" );
        return true;
    }

    UIReply RotatorWidget::onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase )
    {
        const bool bLeftButtonRelease = phase == UIRoutePhase::Bubble && event._kind == UIPointerEventKind::Up && event._button == MouseButton::Left;
        if ( bLeftButtonRelease )
        {
            float2 local{};
            if ( getGeometry().inverseTransformPoint( event._position, local ) )
            {
                _releaseLocalX    = local._x;
                _bHasReleasePoint = true;
            }
        }
        const UIReply reply = ButtonWidget::onPointerEvent( event, phase );
        _bHasReleasePoint   = false; // 클릭이 아니면(밖에서 뗌) 위치를 남기지 않는다
        return reply;
    }

    UIReply RotatorWidget::onActionEvent( const UIActionEvent& event, UIRoutePhase phase )
    {
        if ( phase == UIRoutePhase::Bubble && _listOption.size() >= 2 )
        {
            const bool bLeft  = event._action == hashed_string( UIActionName::kNavigateLeft );
            const bool bRight = event._action == hashed_string( UIActionName::kNavigateRight );
            if ( bLeft || bRight )
            {
                // 화면 기준 오른쪽은 왼쪽에서 오른쪽 위젯에서는 다음이고, 오른쪽에서 왼쪽 위젯에서는 이전입니다.
                const bool bNext = bRight != isRightToLeft();
                (void)stepSelection( bNext ? 1 : -1 );
                return UIReply::makeHandled();
            }
        }
        return ButtonWidget::onActionEvent( event, phase );
    }

    void RotatorWidget::handleClick()
    {
        ButtonWidget::handleClick();
        // 포인터로 눌렀으면 왼쪽 화살표 쪽(화면 기준)이 이전, 그 밖은 다음이다. 오른쪽에서 왼쪽이면 화살표 방향이 뒤집힌다. `UI.Accept` 는 다음이다.
        bool bPrevious = false;
        if ( _bHasReleasePoint )
        {
            const bool bOnLeftArrow = _releaseLocalX < RotatorWidgetInternal::kArrowSpace;
            bPrevious               = isRightToLeft() ? ( _releaseLocalX > getGeometry()._size._x - RotatorWidgetInternal::kArrowSpace ) : bOnLeftArrow;
        }
        (void)stepSelection( bPrevious ? -1 : 1 );
    }

    void RotatorWidget::paintOverChildren( CanvasPainter& painter, const UIPaintContext& context ) const
    {
        (void)context;
        // 양옆 화살표: 가로 막대를 줄여 쌓은 삼각형이다. 항목이 둘보다 적거나 꺼져 있으면 흐리게 그린다.
        using Internal        = RotatorWidgetInternal;
        const float2& size    = getGeometry()._size;
        const bool    bActive = _listOption.size() >= 2 && isEnabledInHierarchy();
        CanvasBrush   arrow{};
        arrow._color         = float4{ 0.85f, 0.88f, 0.95f, bActive ? 1.0f : 0.35f };
        const float32 center = static_cast<float32>( Internal::kArrowSteps - 1 ) * 0.5f;
        const float32 top    = size._y * 0.5f - static_cast<float32>( Internal::kArrowSteps ) * 0.5f * Internal::kArrowThickness;
        for ( uint32 side = 0; side < 2; ++side )
        {
            // 가운데 막대가 가장 길고 위아래로 둘씩 줄어든다. 왼쪽 화살표(◀)는 막대의 오른쪽 끝을, 오른쪽 화살표(▶)는 왼쪽 끝을 맞춘다.
            const bool    bLeftSide = side == 0;
            const float32 alignX    = bLeftSide ? Internal::kArrowSpace * 0.5f + Internal::kArrowBaseWidth * 0.5f : size._x - Internal::kArrowSpace * 0.5f - Internal::kArrowBaseWidth * 0.5f;
            for ( uint32 step = 0; step < Internal::kArrowSteps; ++step )
            {
                const float32 distance = static_cast<float32>( step ) < center ? center - static_cast<float32>( step ) : static_cast<float32>( step ) - center;
                const float32 width    = Internal::kArrowBaseWidth - 2.0f * distance;
                painter.fillRect( float2{ bLeftSide ? alignX - width : alignX, top + Internal::kArrowThickness * static_cast<float32>( step ) }, float2{ width, Internal::kArrowThickness }, arrow );
            }
        }
    }
} // namespace sw

namespace sw
{
    void RotatorWidget::describeAccessible( AccessibleInfo& outInfo )
    {
        ButtonWidget::describeAccessible( outInfo );
        outInfo._role = AccessibleRole::SpinButton;
        outInfo._name = getAccessibleName(); // 안의 글은 고른 항목이라 이름이 아니라 값이다
        if ( _selectedIndex < _listOption.size() )
            outInfo._value = _listOption[_selectedIndex];
    }
} // namespace sw
