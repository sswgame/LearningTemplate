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
    class UIClipboardService;
} // namespace sw

namespace sw
{
    /**
     * @class TextInputWidget
     * @brief 포커스를 가지면 키보드 포커스 `UI` 를 잡아(게임은 키를 보지 못합니다) 글자 이벤트를 받습니다. 확정한 텍스트는 커서 자리에 들어가고(선택이 있으면 선택을 대신하고),
     *        IME 조합 중인 텍스트는 커서 자리에 밑줄로 보여 줍니다. 줄 바꿈(Enter)은 `getOnCommitted()` 를 부릅니다.
     * @details 겉은 테두리 패널(배경)이고 안의 텍스트 위젯이 텍스트를 그린다(비어 있으면 `_hintText` 를 흐리게 보인다).
     *          **편집.** 커서(`getCaretByte`)와 선택 기준점(`getSelectionAnchorByte`)은 원문 UTF-8 바이트 위치이고 항상 코드 포인트 경계입니다. 둘이 다르면 그 사이가 선택입니다.
     *          액션은 `UI.TextBackspace` · `UI.TextDelete` · `UI.TextCaretLeft/Right` · `UI.TextHome/End` · `UI.TextSelectLeft/Right/Home/End` · `UI.TextSelectAll` ·
     *          `UI.TextCopy/Cut/Paste` 이고, 왼쪽과 오른쪽은 화면 기준이라 오른쪽에서 왼쪽 위젯에서는 논리 순서가 뒤집힙니다. 포인터를 누르면 가장 가까운 글자 사이로 커서가 가고,
     *          두 번 누르면 전체를 선택합니다. 복사와 붙여넣기는 `UISystem::getClipboard` 를 거칩니다(붙이는 글에서 줄 바꿈과 제어 문자는 뺍니다 — 한 줄 필드).
     *          글자 단위는 코드 포인트라서 결합 문자 묶음(자소 클러스터)은 낱낱으로 움직입니다. 필드보다 긴 글을 커서 쪽으로 스크롤하는 것과 끌어서 선택은 없습니다.
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

        /** @brief 텍스트를 바꿉니다(알림은 부르지 않으며, 코드가 정한 값입니다). 선택은 풀고 커서는 끝으로 갑니다. */
        void          setText( string_view text );
        const string& getText() const { return _text; }
        /** @brief 입력 칸으로 내보입니다. 값은 지금 글입니다. 이름은 `accessibleName` 만 쓴다. */
        void describeAccessible( AccessibleInfo& outInfo ) override;
        /** @brief 조합 중인 텍스트(IME, 확정 전)입니다. */
        const string& getComposition() const { return _composition; }
        /** @brief 비어 있을 때 흐리게 보일 텍스트입니다. */
        void setHintText( string_view hintText );
        /** @brief 최대 코드 포인트 수입니다(0 이면 제한 없음). 입력과 붙여넣기가 넘치는 만큼 버립니다. */
        void          setMaxLength( uint32 maxLength ) { _maxLength = maxLength; }
        TextDelegate& getOnTextChanged() { return _onTextChanged; }
        TextDelegate& getOnCommitted() { return _onCommitted; }

        /** @brief 커서의 원문 바이트 위치입니다(코드 포인트 경계). */
        uint32 getCaretByte() const { return _caretByte; }
        /** @brief 선택을 시작한 바이트 위치입니다. 커서와 같으면 선택이 없습니다. */
        uint32 getSelectionAnchorByte() const { return _anchorByte; }
        bool   hasSelection() const { return _caretByte != _anchorByte; }
        /** @brief 선택한 글입니다(선택이 없으면 빈 글). */
        string getSelectedText() const;
        /**
         * @brief 커서를 @p byte 로 옮깁니다(코드 포인트 경계로 맞추고 글 길이로 자릅니다). @p bExtendSelection 이 true 이면 기준점은 두고 선택을 넓힙니다.
         */
        void setCaretByte( uint32 byte, bool bExtendSelection = false );
        /** @brief 글 전체를 선택합니다(커서는 끝). */
        void selectAll();

        UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override;
        UIReply onActionEvent( const UIActionEvent& event, UIRoutePhase phase ) override;
        UIReply onTextEvent( const UITextEvent& event ) override;
        void    onFocusChanged( bool bFocused ) override;

    protected:
        void paintOverChildren( CanvasPainter& painter, const UIPaintContext& context ) const override;

    private:
        /** @brief 안의 텍스트 위젯에 보일 텍스트(커서 자리에 조합을 끼운 확정 글이며, 비면 힌트)를 넣습니다. */
        void refreshDisplay();
        /** @brief 선택(없으면 커서 자리)을 @p insert 로 바꿉니다. 최대 길이를 넘는 만큼은 버립니다. 글이 바뀌었으면 true 입니다(알림은 부르지 않습니다). */
        bool replaceSelection( string_view insert );
        /** @brief 글이 바뀐 뒤 알림을 보냅니다(바뀐 글 대리자, 바인딩 되쓰기). */
        void notifyTextEdited();
        /** @brief 화면 기준 방향의 커서 이동 한 칸이 논리 순서로 앞(-1)인지 뒤(+1)인지 반환합니다. */
        int32 resolveHorizontalStep( bool bScreenLeft ) const;
        /** @brief 편집 액션 하나를 처리합니다. 처리했으면 true 입니다. */
        bool handleEditAction( const hashed_string& action );
        /** @brief 이 위젯이 든 화면의 UI 시스템 클립보드입니다(화면 밖이면 nullptr). */
        UIClipboardService* findClipboard() const;
        /** @brief 보이는 글(커서 자리에 조합을 끼운 것) 안의 바이트 위치 @p displayByte 가 놓인 가로 위치입니다(이 위젯 로컬, 마지막 그리기의 배치 기준). */
        float32 computeEdgeX( uint32 displayByte ) const;

    private:
        TextDelegate _onTextChanged;
        TextDelegate _onCommitted;
        string       _composition;
        TextWidget*  _pDisplay; ///< 텍스트를 그리는 자식입니다(이 패널이 만들고 소유합니다).
        uint32       _caretByte;
        uint32       _anchorByte;
        bool         _bKeepCaretOnFocus; ///< 포인터로 눌러 정한 커서를 곧 올 포커스 알림이 글 끝으로 덮지 않게 합니다.
        PROPERTY( DisplayName = "Text" )
        string _text;
        PROPERTY( DisplayName = "Hint Text", Tooltip = "Shown dimmed while the field is empty" )
        string _hintText;
        PROPERTY( DisplayName = "Max Length", Tooltip = "Maximum code points; 0 = unlimited" )
        uint32 _maxLength;
    };
} // namespace sw
