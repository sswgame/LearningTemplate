/**
 * @file ButtonWidget.h
 * @brief 누르는 버튼입니다(UMG Button · 유니티 Button · Godot Button).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Widgets/BorderPanel.h"

namespace sw
{
    /**
     * @class ButtonWidget
     * @brief 포커스를 받는 테두리 패널입니다 — 자식(글 · 그림)을 감싸고, 상태(보통 · 호버 · 누름 · 꺼짐)마다 배경 브러시를 바꿉니다.
     * @details **클릭** = 왼쪽 버튼을 이 버튼 위에서 누르고 이 버튼 위에서 뗐다(누른 동안 포인터를 잡는다 — 밖에서 떼면 클릭이 아니다, Slate 와 같다) 또는
     *          포커스를 쥔 채 `UI.Accept`. 클릭하면 `getOnClicked()` 를 부릅니다. 누르면 포커스가 이 버튼으로 옵니다. 포커스 표시는 포커스 테두리(탐색 방식)입니다.
     */
    REFLECT( Category = "UI", DisplayName = "Button", Tooltip = "Focusable bordered panel that fires a click" )
    class SW_API ButtonWidget : public BorderPanel
    {
    public:
        REFLECT_BODY();

        /** @brief 클릭 알림입니다(인자는 버튼 번호). */
        using ClickedDelegate = MulticastDelegate<void( WidgetId )>;

        ButtonWidget();
        ~ButtonWidget() override;

        const TypeInfo* getTypeInfo() const override;

        bool             supportsFocus() const override { return true; }
        ClickedDelegate& getOnClicked() { return _onClicked; }
        /** @brief 지금까지 클릭된 수입니다(시험 · 디버그). */
        uint32 getClickCount() const { return _clickCount; }
        bool   isPressed() const { return _bPressed; }
        bool   isHovered() const { return _bHovered; }

        /** @brief 상태별 브러시를 바꿉니다(보통은 `setBackground`). kPaint. */
        void setStateBrushes( const UiBrush& hovered, const UiBrush& pressed, const UiBrush& disabled );

        UiReply onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase ) override;
        UiReply onActionEvent( const UiActionEvent& event, UiRoutePhase phase ) override;
        void    onHoverChanged( bool bHovered ) override;

    protected:
        const UiBrush& getBackgroundBrush() const override;
        /** @brief 클릭됐다 — 기본은 알림을 부른다. 체크 상자는 값을 뒤집고 부른다. */
        virtual void handleClick();

    private:
        ClickedDelegate _onClicked;
        uint32          _clickCount;
        PROPERTY( DisplayName = "Hovered Brush" )
        UiBrush _hoveredBrush;
        PROPERTY( DisplayName = "Pressed Brush" )
        UiBrush _pressedBrush;
        PROPERTY( DisplayName = "Disabled Brush" )
        UiBrush _disabledBrush;
        bool    _bPressed; ///< 이 버튼 위에서 눌렀고 아직 떼지 않았다(포인터를 잡았다)
        bool    _bHovered;
    };
} // namespace sw
