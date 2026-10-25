#include "pch.h"

#include "Engine/UI/Widgets/TextInputWidget.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Text/TextLayout.h"
#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"
#include "Engine/UI/Base/UIEvents.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Input/UIClipboardService.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    namespace
    {
        struct TextInputWidgetInternal
        {
            /** @brief 텍스트 색과 힌트 색입니다. */
            static float4 makeTextColor( bool bHint ) { return bHint ? float4{ 1.0f, 1.0f, 1.0f, 0.4f } : float4{ 1.0f, 1.0f, 1.0f, 1.0f }; }

            /** @brief 선택 영역 색, 커서(조합 밑줄 포함) 색입니다. */
            static float4            makeSelectionColor() { return float4{ 0.30f, 0.50f, 1.00f, 0.40f }; }
            static float4            makeCaretColor() { return float4{ 1.0f, 1.0f, 1.0f, 0.9f }; }
            static constexpr float32 kCaretWidth         = 2.0f;
            static constexpr float32 kUnderlineThickness = 1.0f;
            /** @brief 포인터로 가장 가까운 글자 사이를 찾을 때 살피는 최대 코드 포인트 수입니다(그보다 길면 끝으로 갑니다). */
            static constexpr uint32 kMaxPointerSearchCodepoints = 512;

            /** @brief UTF-8 글의 코드 포인트 수입니다. */
            static uint32 countCodepoints( string_view text )
            {
                uint32 count  = 0;
                size_t offset = 0;
                while ( offset < text.size() )
                {
                    (void)StringUtil::decodeUtf8( text, offset );
                    ++count;
                }
                return count;
            }

            /** @brief @p byte 를 글 길이로 자르고 코드 포인트 시작 바이트까지 물립니다. */
            static uint32 snapToBoundary( string_view text, uint32 byte )
            {
                if ( byte >= text.size() )
                    return static_cast<uint32>( text.size() );
                return static_cast<uint32>( StringUtil::floorToUtf8Boundary( text, byte ) );
            }

            /** @brief @p byte 앞 코드 포인트의 시작 바이트입니다(0 이면 0). */
            static uint32 previousBoundary( string_view text, uint32 byte )
            {
                if ( byte == 0 )
                    return 0;
                return snapToBoundary( text, byte - 1 );
            }

            /** @brief @p byte 에서 코드 포인트 하나 뒤의 바이트입니다(끝이면 글 길이). */
            static uint32 nextBoundary( string_view text, uint32 byte )
            {
                if ( byte >= text.size() )
                    return static_cast<uint32>( text.size() );
                size_t offset = byte;
                (void)StringUtil::decodeUtf8( text, offset );
                return static_cast<uint32>( offset );
            }

            /** @brief 앞에서 코드 포인트 @p count 개만 남긴 글의 바이트 길이입니다. */
            static size_t measureCodepointPrefix( string_view text, uint32 count )
            {
                size_t offset = 0;
                uint32 taken  = 0;
                while ( offset < text.size() && taken < count )
                {
                    (void)StringUtil::decodeUtf8( text, offset );
                    ++taken;
                }
                return offset;
            }

            /** @brief 제어 문자(줄 바꿈, 탭 포함)이면 true 입니다. 한 줄 필드는 이런 글자를 넣지 않습니다. */
            static bool isControlCodepoint( uint32 codepoint )
            {
                return codepoint < 0x20u || ( codepoint >= 0x7Fu && codepoint <= 0x9Fu );
            }

            /** @brief 붙여넣을 글에서 제어 문자를 뺍니다(HTML 입력 칸처럼 줄 바꿈은 없애 한 줄로 이어 붙입니다). */
            static string makeSingleLine( string_view text )
            {
                string result{};
                size_t offset = 0;
                while ( offset < text.size() )
                {
                    const size_t start     = offset;
                    const uint32 codepoint = StringUtil::decodeUtf8( text, offset );
                    if ( isControlCodepoint( codepoint ) )
                        continue;
                    result.append( text.data() + start, offset - start );
                }
                return result;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TextInputWidget::TextInputWidget()
        : BorderPanel{}
        , _onTextChanged{}
        , _onCommitted{}
        , _composition{}
        , _pDisplay{ nullptr }
        , _caretByte{ 0 }
        , _anchorByte{ 0 }
        , _bKeepCaretOnFocus{ false }
        , _text{}
        , _hintText{}
        , _maxLength{ 0 }
    {
        UIBrush background      = UIBrush::makeSolid( float4{ 0.06f, 0.07f, 0.09f, 0.95f }, 4.0f );
        background._borderColor = float4{ 0.45f, 0.48f, 0.55f, 1.0f };
        background._borderWidth = 1.0f;
        setBackground( background );
        setContentPadding( float4{ 8.0f, 6.0f, 8.0f, 6.0f } );
        unique_ptr<TextWidget> display = make_unique<TextWidget>();
        TextLayoutStyle        style   = display->getTextStyle();
        style._bWrap                   = false;
        style._overflow                = TextOverflow::Clip;
        display->setTextStyle( style );
        _pDisplay = static_cast<TextWidget*>( addChild( std::move( display ) ) );
        refreshDisplay();
    }

    TextInputWidget::~TextInputWidget() = default;

    const TypeInfo* TextInputWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void TextInputWidget::setText( string_view text )
    {
        if ( _text == text )
            return;
        _text       = string{ text };
        _caretByte  = static_cast<uint32>( _text.size() );
        _anchorByte = _caretByte;
        refreshDisplay();
    }

    void TextInputWidget::setHintText( string_view hintText )
    {
        _hintText = string{ hintText };
        refreshDisplay();
    }

    string TextInputWidget::getSelectedText() const
    {
        if ( hasSelection() == false )
            return string{};
        const uint32 low  = MathUtil::min( _caretByte, _anchorByte );
        const uint32 high = MathUtil::max( _caretByte, _anchorByte );
        return string{
            string_view{ _text.data() + low, high - low }
        };
    }

    void TextInputWidget::setCaretByte( uint32 byte, bool bExtendSelection )
    {
        const uint32 snapped = TextInputWidgetInternal::snapToBoundary( _text, byte );
        const uint32 anchor  = bExtendSelection ? _anchorByte : snapped;
        if ( _caretByte == snapped && _anchorByte == anchor )
            return;
        _caretByte  = snapped;
        _anchorByte = anchor;
        if ( _composition.empty() == false )
            refreshDisplay();
        else
            invalidate( WidgetDirty::kPaint );
    }

    void TextInputWidget::selectAll()
    {
        _anchorByte = 0;
        _caretByte  = static_cast<uint32>( _text.size() );
        invalidate( WidgetDirty::kPaint );
    }

    void TextInputWidget::onBoundPropertyChanged( const PropertyInfo& property )
    {
        if ( property._name == hashed_string( "_text" ) )
        {
            // 바인딩이 되쓴 값(우리가 방금 편집한 글)은 커서를 그대로 두고, 더 짧은 글로 바뀌었으면 글 안으로 물린다.
            _caretByte  = TextInputWidgetInternal::snapToBoundary( _text, _caretByte );
            _anchorByte = TextInputWidgetInternal::snapToBoundary( _text, _anchorByte );
            refreshDisplay();
            return;
        }
        if ( property._name == hashed_string( "_hintText" ) )
        {
            refreshDisplay();
            return;
        }
        BorderPanel::onBoundPropertyChanged( property );
    }

    void TextInputWidget::refreshDisplay()
    {
        const bool bHint = _text.empty() && _composition.empty();
        _pDisplay->setLocalized( bHint ); // 힌트는 현지화 키일 수 있고, 사용자가 입력한 텍스트는 해석하지 않는다
        if ( bHint )
        {
            _pDisplay->setText( _hintText );
        }
        else if ( _composition.empty() )
        {
            _pDisplay->setText( _text );
        }
        else
        {
            // 조합 중인 글은 커서 자리에 끼워 보인다(확정 전이라 _text 에는 들어가지 않는다).
            string display{};
            display.append( _text.data(), _caretByte );
            display.append( _composition.data(), _composition.size() );
            display.append( _text.data() + _caretByte, _text.size() - _caretByte );
            _pDisplay->setText( display );
        }
        _pDisplay->setColor( TextInputWidgetInternal::makeTextColor( bHint ) );
        invalidate( WidgetDirty::kPaint ); // 커서 위치가 바뀐다
    }

    bool TextInputWidget::replaceSelection( string_view insert )
    {
        using Internal    = TextInputWidgetInternal;
        const uint32 low  = MathUtil::min( _caretByte, _anchorByte );
        const uint32 high = MathUtil::max( _caretByte, _anchorByte );
        if ( _maxLength > 0 )
        {
            const uint32 keptCount = Internal::countCodepoints( string_view{ _text.data(), low } ) + Internal::countCodepoints( string_view{ _text.data() + high, _text.size() - high } );
            const uint32 allowed   = _maxLength > keptCount ? _maxLength - keptCount : 0;
            insert                 = insert.substr( 0, Internal::measureCodepointPrefix( insert, allowed ) );
        }
        if ( low == high && insert.empty() )
            return false;
        string next{};
        next.append( _text.data(), low );
        next.append( insert.data(), insert.size() );
        next.append( _text.data() + high, _text.size() - high );
        _text       = std::move( next );
        _caretByte  = low + static_cast<uint32>( insert.size() );
        _anchorByte = _caretByte;
        return true;
    }

    void TextInputWidget::notifyTextEdited()
    {
        refreshDisplay();
        _onTextChanged.broadcast( _text );
        notifyValueEdited( "_text" );
    }

    int32 TextInputWidget::resolveHorizontalStep( bool bScreenLeft ) const
    {
        // 화면 기준 왼쪽은 왼쪽에서 오른쪽 위젯에서는 논리 순서로 앞이고, 오른쪽에서 왼쪽 위젯에서는 뒤입니다.
        const bool bBackward = bScreenLeft != isRightToLeft();
        return bBackward ? -1 : 1;
    }

    UIClipboardService* TextInputWidget::findClipboard() const
    {
        WidgetTree* const pTree   = getTree();
        UIScreen* const   pScreen = pTree != nullptr ? pTree->getScreen() : nullptr;
        UISystem* const   pUI     = pScreen != nullptr ? pScreen->getUISystem() : nullptr;
        return pUI != nullptr ? &pUI->getClipboard() : nullptr;
    }

    void TextInputWidget::notifyClipboardWriteFailed() const
    {
        WidgetTree* const pTree   = getTree();
        UIScreen* const   pScreen = pTree != nullptr ? pTree->getScreen() : nullptr;
        UISystem* const   pUI     = pScreen != nullptr ? pScreen->getUISystem() : nullptr;
        if ( pUI == nullptr )
            return;
        UINotificationDesc notice{};
        notice._text = SW_LOCTEXT( "Notification", "ClipboardWriteFailed", "Could not copy to the clipboard." );
        notice._kind = UINotificationKind::Warning;
        pUI->getNotifications().post( notice );
    }

    bool TextInputWidget::handleEditAction( const hashed_string& action )
    {
        using Internal = TextInputWidgetInternal;
        if ( action == hashed_string( UIActionName::kTextSelectAll ) )
        {
            if ( _composition.empty() )
                selectAll();
            return true;
        }
        const bool bBackspace = action == hashed_string( UIActionName::kTextBackspace );
        const bool bDelete    = action == hashed_string( UIActionName::kTextDelete );
        if ( bBackspace || bDelete )
        {
            if ( _composition.empty() == false )
                return true; // 조합 중인 글은 IME 가 지운다
            if ( hasSelection() == false )
                _anchorByte = bBackspace ? Internal::previousBoundary( _text, _caretByte ) : Internal::nextBoundary( _text, _caretByte );
            if ( replaceSelection( string_view{} ) )
                notifyTextEdited();
            else
                invalidate( WidgetDirty::kPaint );
            return true;
        }

        const bool bSelectLeft     = action == hashed_string( UIActionName::kTextSelectLeft );
        const bool bSelectRight    = action == hashed_string( UIActionName::kTextSelectRight );
        const bool bLeft           = action == hashed_string( UIActionName::kTextCaretLeft );
        const bool bRight          = action == hashed_string( UIActionName::kTextCaretRight );
        const bool bCaretKeyAction = bSelectLeft || bSelectRight || bLeft || bRight;
        if ( bCaretKeyAction )
        {
            if ( _composition.empty() == false )
                return true;
            const int32 step = resolveHorizontalStep( bLeft || bSelectLeft );
            if ( bSelectLeft || bSelectRight )
            {
                setCaretByte( step < 0 ? Internal::previousBoundary( _text, _caretByte ) : Internal::nextBoundary( _text, _caretByte ), true );
            }
            else if ( hasSelection() )
            {
                // 선택을 풀 때는 이동한 쪽 끝에 붙는다(대부분의 편집기와 같다).
                const uint32 low  = MathUtil::min( _caretByte, _anchorByte );
                const uint32 high = MathUtil::max( _caretByte, _anchorByte );
                setCaretByte( step < 0 ? low : high );
            }
            else
            {
                setCaretByte( step < 0 ? Internal::previousBoundary( _text, _caretByte ) : Internal::nextBoundary( _text, _caretByte ) );
            }
            return true;
        }

        const bool bHome          = action == hashed_string( UIActionName::kTextHome );
        const bool bEnd           = action == hashed_string( UIActionName::kTextEnd );
        const bool bSelectHome    = action == hashed_string( UIActionName::kTextSelectHome );
        const bool bSelectEnd     = action == hashed_string( UIActionName::kTextSelectEnd );
        const bool bHomeEndAction = bHome || bEnd || bSelectHome || bSelectEnd;
        if ( bHomeEndAction )
        {
            if ( _composition.empty() == false )
                return true;
            // 처음과 끝은 논리 순서입니다(오른쪽에서 왼쪽 글에서 Home 은 오른쪽 끝).
            setCaretByte( ( bHome || bSelectHome ) ? 0u : static_cast<uint32>( _text.size() ), bSelectHome || bSelectEnd );
            return true;
        }

        const bool bCopy            = action == hashed_string( UIActionName::kTextCopy );
        const bool bCut             = action == hashed_string( UIActionName::kTextCut );
        const bool bPaste           = action == hashed_string( UIActionName::kTextPaste );
        const bool bClipboardAction = bCopy || bCut || bPaste;
        if ( bClipboardAction )
        {
            if ( _composition.empty() == false )
                return true;
            UIClipboardService* const pClipboard = findClipboard();
            if ( pClipboard == nullptr )
                return true;
            if ( bPaste )
            {
                string pasted{};
                if ( pClipboard->tryGetText( pasted ) && replaceSelection( Internal::makeSingleLine( pasted ) ) )
                    notifyTextEdited();
                return true;
            }
            if ( hasSelection() == false )
                return true;
            if ( pClipboard->setText( getSelectedText() ) == false )
            {
                notifyClipboardWriteFailed();
                return true; // 잘라내기는 복사가 된 뒤에만 지운다(안 그러면 글이 사라진다)
            }
            if ( bCut && replaceSelection( string_view{} ) )
                notifyTextEdited();
            return true;
        }
        return false;
    }

    UIReply TextInputWidget::onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase )
    {
        const bool bNotLeftDown = phase != UIRoutePhase::Bubble || event._kind != UIPointerEventKind::Down || event._button != MouseButton::Left;
        if ( bNotLeftDown )
            return UIReply::makeUnhandled();
        if ( hasFocus() == false )
            _bKeepCaretOnFocus = true; // 곧 올 포커스 알림이 눌러서 정한 커서를 글 끝으로 덮지 않게 한다
        if ( _composition.empty() )
        {
            if ( event._clickCount >= 2 )
            {
                selectAll();
            }
            else
            {
                float2 local{};
                if ( getGeometry().inverseTransformPoint( event._position, local ) )
                {
                    // 가장 가까운 글자 사이를 고른다(필드는 한 줄이고 짧아서 처음부터 훑는다).
                    using Internal       = TextInputWidgetInternal;
                    uint32  bestByte     = static_cast<uint32>( _text.size() );
                    float32 bestDistance = MathUtil::abs( computeEdgeX( bestByte ) - local._x );
                    uint32  byte         = 0;
                    uint32  visited      = 0;
                    while ( byte < _text.size() && visited < Internal::kMaxPointerSearchCodepoints )
                    {
                        const float32 distance = MathUtil::abs( computeEdgeX( byte ) - local._x );
                        if ( distance < bestDistance )
                        {
                            bestDistance = distance;
                            bestByte     = byte;
                        }
                        byte = Internal::nextBoundary( _text, byte );
                        ++visited;
                    }
                    setCaretByte( bestByte );
                }
            }
        }
        return UIReply::makeHandled().requestFocus( getID() );
    }

    UIReply TextInputWidget::onActionEvent( const UIActionEvent& event, UIRoutePhase phase )
    {
        if ( phase != UIRoutePhase::Bubble )
            return UIReply::makeUnhandled();
        return handleEditAction( event._action ) ? UIReply::makeHandled() : UIReply::makeUnhandled();
    }

    UIReply TextInputWidget::onTextEvent( const UITextEvent& event )
    {
        using Internal = TextInputWidgetInternal;
        if ( event._bComposition == SW_TRUE )
        {
            // 조합이 시작되면 선택은 그 글로 바뀐다(조합을 확정할 때 대신하는 것이 아니라 시작할 때 지운다).
            const bool bReplaceSelectionOnCompose = _composition.empty() && hasSelection() && replaceSelection( string_view{} );
            if ( bReplaceSelectionOnCompose )
                notifyTextEdited();
            _composition = event._text;
            refreshDisplay();
            return UIReply::makeHandled();
        }
        _composition.clear();
        bool   bChanged = false;
        string pending{};
        size_t offset = 0;
        while ( offset < event._text.size() )
        {
            const size_t start     = offset;
            const uint32 codepoint = StringUtil::decodeUtf8( event._text, offset );
            if ( codepoint == '\r' || codepoint == '\n' )
            {
                if ( pending.empty() == false )
                {
                    bChanged = replaceSelection( pending ) || bChanged;
                    pending.clear();
                }
                _onCommitted.broadcast( _text );
                continue;
            }
            if ( Internal::isControlCodepoint( codepoint ) )
                continue; // 탭은 포커스 이동(UI.FocusNext)이고, Ctrl 조합 글자(Ctrl+A 의 0x01)는 편집 액션이 처리한다
            pending.append( event._text.data() + start, offset - start );
        }
        if ( pending.empty() == false )
            bChanged = replaceSelection( pending ) || bChanged;
        if ( bChanged )
            notifyTextEdited();
        else
            refreshDisplay();
        return UIReply::makeHandled();
    }

    void TextInputWidget::onFocusChanged( bool bFocused )
    {
        if ( bFocused == false )
        {
            _composition.clear();
        }
        else if ( _bKeepCaretOnFocus )
        {
            _bKeepCaretOnFocus = false;
        }
        else
        {
            // 포커스가 들어올 때(패드, Tab, 코드)는 커서를 글 끝에 둔다. 문서가 정한 글에는 커서 값이 없기 때문이다.
            _caretByte  = static_cast<uint32>( _text.size() );
            _anchorByte = _caretByte;
        }
        refreshDisplay();
    }

    float32 TextInputWidget::computeEdgeX( uint32 displayByte ) const
    {
        const WidgetGeometry& display   = _pDisplay->getGeometry();
        const WidgetGeometry& self      = getGeometry();
        const bool            bEmpty    = _text.empty() && _composition.empty();
        const bool            bRtl      = isRightToLeft();
        const float32         localLeft = display._position._x - self._position._x;
        const float32         textWidth = bEmpty ? 0.0f : _pDisplay->getDesiredSize()._x;
        const float32         startX    = bRtl ? localLeft + display._size._x : localLeft;
        const float32         endX      = bRtl ? localLeft + display._size._x - textWidth : localLeft + textWidth;
        if ( bEmpty || displayByte == 0 )
            return startX;
        const vector<LaidOutGlyph>& listGlyph = _pDisplay->getLastLayout()._listGlyph;
        if ( bRtl == false )
        {
            // 글리프는 보이는 순서이고 왼쪽에서 오른쪽 글에서는 논리 순서와 같다. 그 바이트 위치 이후 첫 글리프의 왼쪽 변이 경계다.
            for ( const LaidOutGlyph& glyph : listGlyph )
            {
                if ( glyph._cluster >= displayByte )
                    return localLeft + glyph._origin._x;
            }
            return endX;
        }
        // 오른쪽에서 왼쪽 글: 경계는 그 앞(논리) 글자의 왼쪽 변이다. 섞어 쓴 글(양방향)에서는 근사다.
        const LaidOutGlyph* pPrevious = nullptr;
        for ( const LaidOutGlyph& glyph : listGlyph )
        {
            if ( glyph._cluster < displayByte && ( pPrevious == nullptr || glyph._cluster > pPrevious->_cluster ) )
                pPrevious = &glyph;
        }
        return pPrevious != nullptr ? localLeft + pPrevious->_origin._x : endX;
    }

    void TextInputWidget::paintOverChildren( CanvasPainter& painter, const UIPaintContext& context ) const
    {
        using Internal = TextInputWidgetInternal;
        (void)context;
        if ( hasFocus() == false )
            return;
        const WidgetGeometry& display = _pDisplay->getGeometry();
        const WidgetGeometry& self    = getGeometry();
        const float32         top     = display._position._y - self._position._y;
        if ( hasSelection() )
        {
            const float32 first = computeEdgeX( MathUtil::min( _caretByte, _anchorByte ) );
            const float32 last  = computeEdgeX( MathUtil::max( _caretByte, _anchorByte ) );
            CanvasBrush   selection{};
            selection._color = Internal::makeSelectionColor();
            painter.fillRect( float2{ MathUtil::min( first, last ), top }, float2{ MathUtil::abs( last - first ), display._size._y }, selection );
        }
        if ( _composition.empty() == false )
        {
            const float32 first = computeEdgeX( _caretByte );
            const float32 last  = computeEdgeX( _caretByte + static_cast<uint32>( _composition.size() ) );
            CanvasBrush   underline{};
            underline._color = Internal::makeCaretColor();
            painter.fillRect( float2{ MathUtil::min( first, last ), top + display._size._y - Internal::kUnderlineThickness },
                              float2{ MathUtil::abs( last - first ), Internal::kUnderlineThickness }, underline );
        }
        // 커서는 조합 중이면 조합 글 끝에 둔다.
        const uint32 caretDisplayByte = _caretByte + static_cast<uint32>( _composition.size() );
        CanvasBrush  caret{};
        caret._color = Internal::makeCaretColor();
        painter.fillRect( float2{ computeEdgeX( caretDisplayByte ), top }, float2{ Internal::kCaretWidth, display._size._y }, caret );
    }
} // namespace sw

namespace sw
{
    void TextInputWidget::describeAccessible( AccessibleInfo& outInfo )
    {
        outInfo._role  = AccessibleRole::TextInput;
        outInfo._bLeaf = true;
        outInfo._name  = getAccessibleName();
        outInfo._value = _text;
    }
} // namespace sw
