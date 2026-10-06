/**
 * @file CheckBoxWidget.h
 * @brief 체크 상자입니다(UMG CheckBox · 유니티 Toggle · Godot CheckBox).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Widgets/ButtonWidget.h"

namespace sw
{
    /**
     * @class CheckBoxWidget
     * @brief 클릭하면 켜짐 · 꺼짐이 바뀌는 버튼입니다. 왼쪽(오른쪽에서 왼쪽이면 오른쪽)에 상자를 그리고 자식(글)은 그 옆입니다.
     */
    REFLECT( Category = "UI", DisplayName = "Check Box", Tooltip = "Button that toggles a checked state" )
    class SW_API CheckBoxWidget : public ButtonWidget
    {
    public:
        REFLECT_BODY();

        /** @brief 바뀐 값 알림입니다. */
        using CheckedDelegate = MulticastDelegate<void( bool )>;

        /** @brief 상자 한 변(UI 단위)입니다. */
        static constexpr float32 kBoxSize = 20.0f;
        /** @brief 상자와 글 사이 틈입니다. */
        static constexpr float32 kBoxGap = 8.0f;

        CheckBoxWidget();
        ~CheckBoxWidget() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 값을 바꿉니다(알림은 부르지 않는다 — 코드가 정한 값). kPaint. */
        void             setChecked( bool bChecked );
        bool             isChecked() const { return _bChecked; }
        CheckedDelegate& getOnCheckedChanged() { return _onCheckedChanged; }

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UiLayoutContext& context, const float2& size ) override;
        void   paint( CanvasPainter& painter, const UiPaintContext& context ) const override;
        void   handleClick() override;

    private:
        CheckedDelegate _onCheckedChanged;
        PROPERTY( DisplayName = "Checked" )
        bool _bChecked;
    };
} // namespace sw
