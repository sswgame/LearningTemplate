/**
 * @file ButtonWidget.h
 * @brief 누르면 명령을 내는 위젯입니다(UMG Button · 유니티 Button · Godot Button).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Widgets/BorderPanel.h"

namespace sw
{
    class ButtonWidget;

    /** @brief 버튼이 눌렸을 때 부르는 함수입니다(코드로 거는 쪽 — 문서는 `_command`). */
    using ButtonClickedDelegate = Delegate<void( ButtonWidget& )>;

    /**
     * @class ButtonWidget
     * @brief 테두리 패널(바탕 · 여백 · 자식 하나)에 클릭을 더한 위젯입니다. 포커스를 받습니다.
     * @details 클릭 = 포인터를 이 버튼 위에서 누르고 **이 버튼 위에서** 뗀 것(누른 채 밖으로 나갔다 떼면 클릭이 아니다 — 포인터를 잡아 끝까지 본다),
     *          또는 포커스를 쥔 채 `UI.Accept`. 클릭하면 코드가 건 함수(`setClickedHandler`) 다음 화면의 명령(`_command` → `UiScreen::onCommand`)입니다.
     *          꺼진 버튼은 사건을 받지 않습니다(라우터가 거른다). 누른 상태(`isPressed`)가 바뀌면 `kStyle`(스타일 `:pressed`).
     */
    REFLECT( Category = "UI", DisplayName = "Button", Tooltip = "Clickable border that sends a command to its screen" )
    class SW_API ButtonWidget : public BorderPanel
    {
    public:
        REFLECT_BODY();

        ButtonWidget();
        ~ButtonWidget() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 클릭하면 화면에 보낼 명령입니다. 비면 명령을 내지 않습니다. */
        const hashed_string& getCommand() const { return _command; }
        void                 setCommand( const hashed_string& command ) { _command = command; }
        /** @brief 클릭할 때 부를 함수를 겁니다(명령보다 먼저 불린다). */
        void setClickedHandler( const ButtonClickedDelegate& handler ) { _clickedHandler = handler; }
        /** @brief 포인터로 누른 채인가입니다(스타일 `:pressed`). */
        bool isPressed() const { return _bPressed; }
        /** @brief 클릭합니다 — 건 함수를 부르고 명령을 냅니다. 꺼져 있으면 아무것도 하지 않습니다. */
        void click();

        bool    supportsFocus() const override { return true; }
        UiReply onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase ) override;
        UiReply onActionEvent( const UiActionEvent& event, UiRoutePhase phase ) override;

    private:
        /** @brief 누른 상태를 바꿉니다. 바뀌면 kStyle. */
        void setPressed( bool bPressed );

    private:
        ButtonClickedDelegate _clickedHandler;
        PROPERTY( DisplayName = "Command", Tooltip = "Command sent to the screen when clicked (UiScreen::onCommand)" )
        hashed_string _command;
        bool          _bPressed; ///< 포인터로 누른 채(이 버튼에서 눌렀고 아직 떼지 않았다)
    };
} // namespace sw
