/**
 * @file ButtonWidget.h
 * @brief 누르는 버튼입니다(UMG 의 Button, 유니티의 Button, Godot 의 Button 에 해당).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Widgets/BorderPanel.h"

namespace sw
{
    /**
     * @class ButtonWidget
     * @brief 포커스를 받는 테두리 패널입니다. 자식(텍스트, 이미지)을 감싸고, 상태(보통, 호버, 누름, 꺼짐)마다 배경 브러시를 바꿉니다.
     * @details **클릭**은 왼쪽 버튼을 이 버튼 위에서 누르고 이 버튼 위에서 떼는 것이다(누른 동안 포인터를 잡으며, 밖에서 떼면 클릭이 아니다. Slate 와 같다). 포커스를 가진 채 `UI.Accept` 를 눌러도 클릭이다.
     *          클릭하면 `getOnClicked()` 를 부르고, `_command` 가 있으면 화면의 명령(`UIScreen::onCommand`, UI 문서가 쓰는 경로)을 낸다. 누르면 포커스가 이 버튼으로 온다. 포커스 표시는 포커스 테두리(탐색 방식)다.
     */
    REFLECT( Category = "UI", DisplayName = "Button", Tooltip = "Focusable bordered panel that fires a click" )
    class SW_API ButtonWidget : public BorderPanel
    {
    public:
        REFLECT_BODY();

        /** @brief 클릭 알림입니다(인자는 버튼 번호). */
        using ClickedDelegate = MulticastDelegate<void( WidgetID )>;

        ButtonWidget();
        ~ButtonWidget() override;

        const TypeInfo* getTypeInfo() const override;

        bool             supportsFocus() const override { return true; }
        ClickedDelegate& getOnClicked() { return _onClicked; }
        /** @brief 지금까지 클릭된 횟수입니다(테스트와 디버그용). */
        uint32 getClickCount() const { return _clickCount; }
        bool   isPressed() const { return _bPressed; }
        /** @brief 클릭하면 화면에 보낼 명령입니다. 비면 명령을 내지 않습니다. */
        const hashed_string& getCommand() const { return _command; }
        void                 setCommand( const hashed_string& command ) { _command = command; }

        /** @brief 상태별 브러시를 바꿉니다(보통은 `setBackground`). kPaint. */
        void setStateBrushes( const UIBrush& hovered, const UIBrush& pressed, const UIBrush& disabled );

        UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override;
        UIReply onActionEvent( const UIActionEvent& event, UIRoutePhase phase ) override;
        /** @brief 기본 상태에 누름(`:pressed`)을 더합니다. */
        uint32 computeStyleStates() const override;

    protected:
        const UIBrush& getBackgroundBrush() const override;
        /** @brief 클릭됐을 때 부릅니다. 기본 구현은 알림을 부르고, 체크 상자는 값을 뒤집은 뒤 부릅니다. */
        virtual void handleClick();

    private:
        ClickedDelegate _onClicked;
        uint32          _clickCount;
        PROPERTY( DisplayName = "Hovered Brush" )
        UIBrush _hoveredBrush;
        PROPERTY( DisplayName = "Pressed Brush" )
        UIBrush _pressedBrush;
        PROPERTY( DisplayName = "Disabled Brush" )
        UIBrush _disabledBrush;
        PROPERTY( DisplayName = "Command", Tooltip = "Command sent to the screen when clicked (UIScreen::onCommand)" )
        hashed_string _command;
        bool          _bPressed; ///< 이 버튼 위에서 눌렀고 아직 떼지 않았다(포인터를 잡았다)
    };
} // namespace sw
