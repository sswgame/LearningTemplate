/**
 * @file TextInputWidget.h
 * @brief 한 줄 글 입력 칸입니다(UMG EditableTextBox · 유니티 TextField · Godot LineEdit).
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
     * @brief 포커스를 쥐면 키보드 포커스 `Ui` 를 잡아(게임은 키를 보지 못한다) 글자 사건을 받습니다. 확정 글은 끝에 붙이고, IME 조합 중인 글은 그 뒤에
     *        이어 보입니다. `UI.TextBackspace` 는 끝 글자(코드 포인트) 하나를 지우고, 줄 바꿈(Enter)은 `getOnCommitted()` 를 부릅니다.
     * @details 겉은 테두리 패널(배경)이고 안의 글 위젯이 글을 그립니다(비면 `_hintText` 를 흐리게). 커서는 글 끝(포커스일 때 세로 막대).
     *          커서 이동 · 선택 · 붙여넣기는 백로그입니다.
     */
    REFLECT( Category = "UI", DisplayName = "Text Input", Tooltip = "Single-line editable text field" )
    class SW_API TextInputWidget : public BorderPanel
    {
    public:
        REFLECT_BODY();

        /** @brief 글 알림입니다(바뀐 글 · 확정한 글). */
        using TextDelegate = MulticastDelegate<void( const string& )>;

        TextInputWidget();
        ~TextInputWidget() override;

        const TypeInfo* getTypeInfo() const override;
        /** @brief 바인딩이 쓴 칸에 맞춰 무효화합니다(글 · 힌트는 보이는 글을 다시). */
        void onBoundPropertyChanged( const PropertyInfo& property ) override;

        bool supportsFocus() const override { return true; }
        bool supportsTextInput() const override { return true; }

        /** @brief 글을 바꿉니다(알림은 부르지 않는다 — 코드가 정한 값). */
        void          setText( string_view text );
        const string& getText() const { return _text; }
        /** @brief 조합 중인 글(IME — 확정 전)입니다. */
        const string& getComposition() const { return _composition; }
        /** @brief 빈 칸에 흐리게 보일 글입니다. */
        void          setHintText( string_view hintText );
        TextDelegate& getOnTextChanged() { return _onTextChanged; }
        TextDelegate& getOnCommitted() { return _onCommitted; }

        UiReply onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase ) override;
        UiReply onActionEvent( const UiActionEvent& event, UiRoutePhase phase ) override;
        UiReply onTextEvent( const UiTextEvent& event ) override;
        void    onFocusChanged( bool bFocused ) override;

    protected:
        void paintOverChildren( CanvasPainter& painter, const UiPaintContext& context ) const override;

    private:
        /** @brief 안의 글 위젯에 보일 글(글 + 조합, 비면 힌트)을 넣습니다. */
        void refreshDisplay();

    private:
        TextDelegate _onTextChanged;
        TextDelegate _onCommitted;
        string       _composition;
        TextWidget*  _pDisplay; ///< 글을 그리는 자식(이 패널이 만들고 소유한다)
        PROPERTY( DisplayName = "Text" )
        string _text;
        PROPERTY( DisplayName = "Hint Text", Tooltip = "Shown dimmed while the field is empty" )
        string _hintText;
        PROPERTY( DisplayName = "Max Length", Tooltip = "Maximum code points; 0 = unlimited" )
        uint32 _maxLength;
    };
} // namespace sw
