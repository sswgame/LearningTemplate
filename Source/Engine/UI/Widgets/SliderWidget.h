/**
 * @file SliderWidget.h
 * @brief 값을 끌어 고르는 슬라이더와 진행 막대를 정의합니다(UMG 의 Slider 와 ProgressBar, 유니티의 Slider 와 ProgressBar, Godot 의 HSlider 와 ProgressBar 에 해당).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/WidgetTree/Widget.h"

namespace sw
{
    /**
     * @class SliderWidget
     * @brief 가로 슬라이더입니다. 포커스를 가지면 `UI.NavigateLeft` 와 `UI.NavigateRight` 를 소비해 값을 한 단계(`_step`)씩 바꿉니다(포커스가 옆으로 가지 않으며,
     *        위나 아래 탐색으로 떠납니다). 트랙을 누르면 그 위치로 이동하고 포인터를 잡아 드래그합니다.
     * @details 오른쪽에서 왼쪽 배치에서는 최소값이 오른쪽에 온다(왼쪽 액션이 값을 올린다). 값이 바뀌면 `getOnValueChanged()` 를 부른다.
     */
    REFLECT( Category = "UI", DisplayName = "Slider", Tooltip = "Horizontal value slider" )
    class SW_API SliderWidget : public Widget
    {
    public:
        REFLECT_BODY();

        /** @brief 값이 바뀌었을 때의 알림입니다. */
        using ValueChangedDelegate = MulticastDelegate<void( float32 )>;

        /** @brief 기본 원하는 크기(UI 단위)입니다. 슬롯 덮어쓰기로 바꿀 수 있습니다. */
        static constexpr float32 kDefaultLength    = 200.0f;
        static constexpr float32 kDefaultThickness = 24.0f;

        SliderWidget();
        ~SliderWidget() override;

        const TypeInfo* getTypeInfo() const override;
        /** @brief 바인딩이 쓴 필드에 맞춰 무효화합니다(값과 범위는 값을 범위로 제한하고 그리기만 합니다). */
        void onBoundPropertyChanged( const PropertyInfo& property ) override;

        bool supportsFocus() const override { return true; }

        /** @brief 범위와 단계를 정합니다(값은 범위로 제한합니다). */
        void setRange( float32 minValue, float32 maxValue, float32 step );
        /** @brief 값을 바꿉니다(범위로 제한). 바뀌면 알림을 보내고 kPaint 로 무효화합니다. */
        void    setValue( float32 value );
        float32 getValue() const { return _value; }
        /** @brief 사용자 입력(드래그, 좌우 액션, 스크린 리더의 값 바꾸기)으로 값을 바꿉니다. 바뀌면 양방향 바인딩에 알립니다(`notifyValueEdited`). */
        void setValueFromUser( float32 value );
        /** @brief 슬라이더로 내보입니다(범위 · 값 · 단계). 이름은 `accessibleName` 만 쓴다. */
        void                  describeAccessible( AccessibleInfo& outInfo ) override;
        ValueChangedDelegate& getOnValueChanged() { return _onValueChanged; }

        UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override;
        UIReply onActionEvent( const UIActionEvent& event, UIRoutePhase phase ) override;

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   paint( CanvasPainter& painter, const UIPaintContext& context ) const override;

    private:
        /** @brief 화면 점을 값으로 바꿉니다(트랙 위 비율이며, 오른쪽에서 왼쪽이면 뒤집습니다). */
        float32 computeValueAt( const float2& screenPoint ) const;
        /** @brief 지금 값의 트랙 위 비율(0..1)입니다. */
        float32 computeFraction() const;

    private:
        ValueChangedDelegate _onValueChanged;
        PROPERTY( DisplayName = "Value" )
        float32 _value;
        PROPERTY( DisplayName = "Min Value" )
        float32 _minValue;
        PROPERTY( DisplayName = "Max Value" )
        float32 _maxValue;
        PROPERTY( DisplayName = "Step", Tooltip = "Change per left/right action; 0 = 5% of the range", Min = 0.0 )
        float32 _step;
        bool    _bDragging; ///< 트랙을 눌러 드래그하는 중인지 나타냅니다(포인터를 잡은 상태).
    };
} // namespace sw

namespace sw
{
    /**
     * @class ProgressBarWidget
     * @brief 0..1 비율을 채운 막대로 그립니다(입력 없음). 오른쪽에서 왼쪽이면 오른쪽부터 찹니다.
     */
    REFLECT( Category = "UI", DisplayName = "Progress Bar", Tooltip = "Fills a bar by a 0..1 fraction" )
    class SW_API ProgressBarWidget : public Widget
    {
    public:
        REFLECT_BODY();

        ProgressBarWidget();
        ~ProgressBarWidget() override;

        const TypeInfo* getTypeInfo() const override;
        /** @brief 바인딩이 쓴 필드에 맞춰 무효화합니다(비율은 0..1 로 제한하고, 비율과 색은 그리기만 합니다). */
        void onBoundPropertyChanged( const PropertyInfo& property ) override;

        /** @brief 비율을 바꿉니다(0..1 로 제한). kPaint 로 무효화합니다. */
        void    setPercent( float32 percent );
        float32 getPercent() const { return _percent; }
        /** @brief 진행 막대로 내보입니다(0..1 읽기 전용 범위). */
        void describeAccessible( AccessibleInfo& outInfo ) override;
        /** @brief 채움 색을 바꿉니다. kPaint 로 무효화합니다. */
        void          setFillColor( const float4& color );
        const float4& getFillColor() const { return _fillColor; }
        /** @brief 바탕 색(채우지 않은 쪽)을 바꿉니다. 알파가 0 이면 바탕을 칠하지 않습니다(막대를 겹칠 때). kPaint 로 무효화합니다. */
        void          setBackgroundColor( const float4& color );
        const float4& getBackgroundColor() const { return _backgroundColor; }

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   paint( CanvasPainter& painter, const UIPaintContext& context ) const override;

    private:
        PROPERTY( DisplayName = "Percent", Min = 0.0, Max = 1.0 )
        float32 _percent;
        PROPERTY( DisplayName = "Fill Color" )
        float4 _fillColor;
        PROPERTY( DisplayName = "Background Color" )
        float4 _backgroundColor;
    };
} // namespace sw
