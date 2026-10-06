#include "pch.h"

#include "Engine/UI/Widgets/SliderWidget.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Core/UiEvents.h"

namespace sw
{
    namespace
    {
        struct SliderWidgetInternal
        {
            /** @brief 트랙 두께 · 엄지 지름(UI 단위)입니다. */
            static constexpr float32 kTrackThickness = 4.0f;
            static constexpr float32 kThumbSize      = 16.0f;
            /** @brief 한 칸을 정하지 않았을 때 범위의 비율입니다. */
            static constexpr float32 kDefaultStepFraction = 0.05f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SliderWidget::SliderWidget()
        : Widget{}
        , _onValueChanged{}
        , _value{ 0.0f }
        , _minValue{ 0.0f }
        , _maxValue{ 1.0f }
        , _step{ 0.0f }
        , _bDragging{ false }
    {
    }

    SliderWidget::~SliderWidget() = default;

    const TypeInfo* SliderWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void SliderWidget::setRange( float32 minValue, float32 maxValue, float32 step )
    {
        _minValue = minValue;
        _maxValue = MathUtil::max( minValue, maxValue );
        _step     = MathUtil::max( 0.0f, step );
        setValue( _value );
        invalidate( WidgetDirty::kPaint );
    }

    void SliderWidget::setValue( float32 value )
    {
        const float32 clamped = MathUtil::clamp( value, _minValue, _maxValue );
        if ( clamped == _value )
            return;
        _value = clamped;
        invalidate( WidgetDirty::kPaint );
        _onValueChanged.broadcast( _value );
    }

    void SliderWidget::setValueFromUser( float32 value )
    {
        const float32 previous = _value;
        setValue( value );
        if ( _value != previous )
            notifyValueEdited( "_value" );
    }

    void SliderWidget::onBoundPropertyChanged( const PropertyInfo& property )
    {
        const hashed_string& name = property._name;
        if ( name == hashed_string( "_value" ) || name == hashed_string( "_minValue" ) || name == hashed_string( "_maxValue" ) || name == hashed_string( "_step" ) )
        {
            // 세터(`setRange` · `setValue`)와 같은 묶기 — 알림은 부르지 않는다(바인딩이 쓴 값이다).
            _maxValue = MathUtil::max( _minValue, _maxValue );
            _step     = MathUtil::max( 0.0f, _step );
            _value    = MathUtil::clamp( _value, _minValue, _maxValue );
            invalidate( WidgetDirty::kPaint );
            return;
        }
        Widget::onBoundPropertyChanged( property );
    }

    float32 SliderWidget::computeFraction() const
    {
        const float32 range = _maxValue - _minValue;
        return range > 0.0f ? ( _value - _minValue ) / range : 0.0f;
    }

    float32 SliderWidget::computeValueAt( const float2& screenPoint ) const
    {
        float2 local{};
        if ( getGeometry().inverseTransformPoint( screenPoint, local ) == false )
            return _value;
        const float32 usable   = MathUtil::max( 1.0f, getGeometry()._size._x - SliderWidgetInternal::kThumbSize );
        float32       fraction = MathUtil::saturate( ( local._x - SliderWidgetInternal::kThumbSize * 0.5f ) / usable );
        if ( isRightToLeft() )
            fraction = 1.0f - fraction;
        return _minValue + fraction * ( _maxValue - _minValue );
    }

    UiReply SliderWidget::onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase )
    {
        if ( phase != UiRoutePhase::Bubble )
            return UiReply::makeUnhandled();
        switch ( event._kind )
        {
            case UiPointerEventKind::Down:
            {
                if ( event._button != MouseButton::Left )
                    return UiReply::makeUnhandled();
                _bDragging = true;
                setValueFromUser( computeValueAt( event._position ) );
                return UiReply::makeHandled().capturePointer().requestFocus( getId() );
            }
            case UiPointerEventKind::Move:
            {
                if ( _bDragging == false )
                    return UiReply::makeUnhandled();
                setValueFromUser( computeValueAt( event._position ) );
                return UiReply::makeHandled();
            }
            case UiPointerEventKind::Up:
            {
                if ( _bDragging == false || event._button != MouseButton::Left )
                    return UiReply::makeUnhandled();
                _bDragging = false;
                return UiReply::makeHandled().releasePointer();
            }
            case UiPointerEventKind::Wheel:
            {
                return UiReply::makeUnhandled();
            }
        }
        return UiReply::makeUnhandled();
    }

    UiReply SliderWidget::onActionEvent( const UiActionEvent& event, UiRoutePhase phase )
    {
        if ( phase != UiRoutePhase::Bubble )
            return UiReply::makeUnhandled();
        const bool bLeft  = event._action == hashed_string( UiActionName::kNavigateLeft );
        const bool bRight = event._action == hashed_string( UiActionName::kNavigateRight );
        if ( bLeft == false && bRight == false )
            return UiReply::makeUnhandled();
        const float32 step = _step > 0.0f ? _step : ( _maxValue - _minValue ) * SliderWidgetInternal::kDefaultStepFraction;
        // 화면 왼쪽 = 최소(오른쪽에서 왼쪽이면 최대) — 행동 방향은 화면 기준이다.
        const bool bDecrease = bLeft != isRightToLeft();
        setValueFromUser( _value + ( bDecrease ? -step : step ) );
        return UiReply::makeHandled();
    }

    float2 SliderWidget::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        (void)context;
        (void)availableSize;
        return float2{ kDefaultLength, kDefaultThickness };
    }

    void SliderWidget::paint( CanvasPainter& painter, const UiPaintContext& context ) const
    {
        (void)context;
        using Internal         = SliderWidgetInternal;
        const float2& size     = getGeometry()._size;
        const bool    bOn      = isEnabledInHierarchy();
        const float32 alpha    = bOn ? 1.0f : 0.4f;
        const float32 half     = Internal::kThumbSize * 0.5f;
        const float32 usable   = MathUtil::max( 0.0f, size._x - Internal::kThumbSize );
        const float32 trackY   = ( size._y - Internal::kTrackThickness ) * 0.5f;
        float32       fraction = computeFraction();
        if ( isRightToLeft() )
            fraction = 1.0f - fraction;
        const float32 thumbX = usable * fraction;

        const float32 radius = Internal::kTrackThickness * 0.5f;
        CanvasBrush   track{};
        track._color        = float4{ 0.3f, 0.32f, 0.38f, 0.9f * alpha };
        track._cornerRadius = float4{ radius, radius, radius, radius };
        painter.fillRect( float2{ half, trackY }, float2{ usable, Internal::kTrackThickness }, track );
        CanvasBrush fill        = track;
        fill._color             = float4{ 0.95f, 0.75f, 0.2f, alpha };
        const float32 fillStart = isRightToLeft() ? half + thumbX : half;
        const float32 fillWidth = isRightToLeft() ? usable - thumbX : thumbX;
        if ( fillWidth > 0.0f )
            painter.fillRect( float2{ fillStart, trackY }, float2{ fillWidth, Internal::kTrackThickness }, fill );
        CanvasBrush thumb{};
        thumb._color        = float4{ 0.95f, 0.95f, 0.98f, alpha };
        thumb._cornerRadius = float4{ half, half, half, half };
        painter.fillRect( float2{ thumbX, ( size._y - Internal::kThumbSize ) * 0.5f }, float2{ Internal::kThumbSize, Internal::kThumbSize }, thumb );
    }

    ProgressBarWidget::ProgressBarWidget()
        : Widget{}
        , _percent{ 0.0f }
        , _fillColor{ 0.35f, 0.8f, 0.4f, 1.0f }
        , _backgroundColor{ 0.08f, 0.09f, 0.12f, 0.85f }
    {
    }

    ProgressBarWidget::~ProgressBarWidget() = default;

    const TypeInfo* ProgressBarWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void ProgressBarWidget::setPercent( float32 percent )
    {
        const float32 clamped = MathUtil::saturate( percent );
        if ( clamped == _percent )
            return;
        _percent = clamped;
        invalidate( WidgetDirty::kPaint );
    }

    void ProgressBarWidget::onBoundPropertyChanged( const PropertyInfo& property )
    {
        const hashed_string& name = property._name;
        if ( name == hashed_string( "_percent" ) || name == hashed_string( "_fillColor" ) || name == hashed_string( "_backgroundColor" ) )
        {
            _percent = MathUtil::saturate( _percent );
            invalidate( WidgetDirty::kPaint );
            return;
        }
        Widget::onBoundPropertyChanged( property );
    }

    void ProgressBarWidget::setFillColor( const float4& color )
    {
        if ( _fillColor == color )
            return;
        _fillColor = color;
        invalidate( WidgetDirty::kPaint );
    }

    float2 ProgressBarWidget::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        (void)context;
        (void)availableSize;
        return float2{ SliderWidget::kDefaultLength, 12.0f };
    }

    void ProgressBarWidget::paint( CanvasPainter& painter, const UiPaintContext& context ) const
    {
        (void)context;
        const float2& size   = getGeometry()._size;
        const float32 radius = MathUtil::min( 4.0f, size._y * 0.5f );
        CanvasBrush   background{};
        background._color        = _backgroundColor;
        background._cornerRadius = float4{ radius, radius, radius, radius };
        painter.fillRect( float2{}, size, background );
        const float32 fillWidth = size._x * _percent;
        if ( fillWidth <= 0.0f )
            return;
        CanvasBrush fill = background;
        fill._color      = _fillColor;
        painter.fillRect( float2{ isRightToLeft() ? size._x - fillWidth : 0.0f, 0.0f }, float2{ fillWidth, size._y }, fill );
    }
} // namespace sw
