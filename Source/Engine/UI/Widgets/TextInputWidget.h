/**
 * @file TextInputWidget.h
 * @brief 한 줄 텍스트 입력 필드입니다(UMG 의 EditableTextBox, 유니티의 TextField, Godot 의 LineEdit 에 해당).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Widgets/BorderPanel.h"

namespace sw
{
    class TextWidget;

    /**
     * @class TextInputWidget
     * @brief 포커스를 가지면 키보드 포커스 `UI` 를 잡아(게임은 키를 보지 못합니다) 글자 이벤트를 받습니다. 확정한 텍스트는 끝에 붙이고, IME 조합 중인 텍스트는 그 뒤에
     *        이어서 보여 줍니다. `UI.TextBackspace` 는 끝 글자(코드 포인트) 하나를 지우고, 줄 바꿈(Enter)은 `getOnCommitted()` 를 부릅니다.
     * @details 겉은 테두리 패널(배경)이고 안의 텍스트 위젯이 텍스트를 그린다(비어 있으면 `_hintText` 를 흐리게 보인다). 커서는 텍스트 끝에 있다(포커스일 때 세로 막대).
     *          커서 이동, 선택, 붙여넣기는 백로그에 있다.
     */
    REFLECT( Category = "UI", DisplayName = "Text Input", Tooltip = "Single-line editable text field" )
    class SW_API TextInputWidget : public BorderPanel
    {
    public:
        REFLECT_BODY();

        /** @brief 텍스트 알림입니다(바뀐 텍스트와 확정한 텍스트). */
        using TextDelegate = MulticastDelegate<void( const string& )>;

        TextInputWidget();
        ~TextInputWidget() override;

        const TypeInfo* getTypeInfo() const override;
        /** @brief 바인딩이 쓴 필드에 맞춰 무효화합니다(텍스트와 힌트는 보이는 텍스트를 다시 만듭니다). */
        void onBoundPropertyChanged( const PropertyInfo& property ) override;

        bool supportsFocus() const override { return true; }
        bool supportsTextInput() const override { return true; }

        /** @brief 텍스트를 바꿉니다(알림은 부르지 않으며, 코드가 정한 값입니다). */
        void          setText( string_view text );
        const string& getText() const { return _text; }
        /** @brief 조합 중인 텍스트(IME, 확정 전)입니다. */
        const string& getComposition() const { return _composition; }
        /** @brief 비어 있을 때 흐리게 보일 텍스트입니다. */
        void          setHintText( string_view hintText );
        TextDelegate& getOnTextChanged() { return _onTextChanged; }
        TextDelegate& getOnCommitted() { return _onCommitted; }

        UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override;
        UIReply onActionEvent( const UIActionEvent& event, UIRoutePhase phase ) override;
        UIReply onTextEvent( const UITextEvent& event ) override;
        void    onFocusChanged( bool bFocused ) override;

    protected:
        void paintOverChildren( CanvasPainter& painter, const UIPaintContext& context ) const override;

    private:
        /** @brief 안의 텍스트 위젯에 보일 텍스트(확정 + 조합이며, 비면 힌트)를 넣습니다. */
        void refreshDisplay();

    private:
        TextDelegate _onTextChanged;
        TextDelegate _onCommitted;
        string       _composition;
        TextWidget*  _pDisplay; ///< 텍스트를 그리는 자식입니다(이 패널이 만들고 소유합니다).
        PROPERTY( DisplayName = "Text" )
        string _text;
        PROPERTY( DisplayName = "Hint Text", Tooltip = "Shown dimmed while the field is empty" )
        string _hintText;
        PROPERTY( DisplayName = "Max Length", Tooltip = "Maximum code points; 0 = unlimited" )
        uint32 _maxLength;
    };
} // namespace sw
