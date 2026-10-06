#include "pch.h"

#include "Engine/UI/Widgets/TextInputWidget.h"

#include "Core/String/StringUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/UI/Core/UiEvents.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    namespace
    {
        struct TextInputWidgetInternal
        {
            /** @brief 글 색 · 힌트 색입니다. */
            static float4 makeTextColor( bool bHint ) { return bHint ? float4{ 1.0f, 1.0f, 1.0f, 0.4f } : float4{ 1.0f, 1.0f, 1.0f, 1.0f }; }

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

            /** @brief 끝 코드 포인트 하나를 지웁니다(이어지는 바이트 10xxxxxx 를 건너 시작 바이트까지). 지웠으면 true. */
            static bool popLastCodepoint( string& inoutText )
            {
                if ( inoutText.empty() )
                    return false;
                size_t end = inoutText.size() - 1;
                while ( end > 0 && ( static_cast<uint8>( inoutText[end] ) & 0xC0u ) == 0x80u )
                    --end;
                inoutText.resize( end );
                return true;
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
        , _text{}
        , _hintText{}
        , _maxLength{ 0 }
    {
        UiBrush background      = UiBrush::makeSolid( float4{ 0.06f, 0.07f, 0.09f, 0.95f }, 4.0f );
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
        _text = string{ text };
        refreshDisplay();
    }

    void TextInputWidget::setHintText( string_view hintText )
    {
        _hintText = string{ hintText };
        refreshDisplay();
    }

    void TextInputWidget::refreshDisplay()
    {
        const bool bHint = _text.empty() && _composition.empty();
        _pDisplay->setText( bHint ? string_view{ _hintText } : string_view{ _text + _composition } );
        _pDisplay->setColor( TextInputWidgetInternal::makeTextColor( bHint ) );
        invalidate( WidgetDirty::kPaint ); // 커서 자리
    }

    UiReply TextInputWidget::onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase )
    {
        if ( phase != UiRoutePhase::Bubble || event._kind != UiPointerEventKind::Down || event._button != MouseButton::Left )
            return UiReply::makeUnhandled();
        return UiReply::makeHandled().requestFocus( getId() );
    }

    UiReply TextInputWidget::onActionEvent( const UiActionEvent& event, UiRoutePhase phase )
    {
        if ( phase != UiRoutePhase::Bubble || event._action != hashed_string( UiActionName::kTextBackspace ) )
            return UiReply::makeUnhandled();
        if ( _composition.empty() && TextInputWidgetInternal::popLastCodepoint( _text ) )
        {
            refreshDisplay();
            _onTextChanged.broadcast( _text );
        }
        return UiReply::makeHandled();
    }

    UiReply TextInputWidget::onTextEvent( const UiTextEvent& event )
    {
        if ( event._bComposition == SW_TRUE )
        {
            _composition = event._text;
            refreshDisplay();
            return UiReply::makeHandled();
        }
        _composition.clear();
        bool   bChanged = false;
        size_t offset   = 0;
        while ( offset < event._text.size() )
        {
            const size_t start     = offset;
            const uint32 codepoint = StringUtil::decodeUtf8( event._text, offset );
            if ( codepoint == '\r' || codepoint == '\n' )
            {
                _onCommitted.broadcast( _text );
                continue;
            }
            if ( codepoint == '\t' )
                continue; // 탭은 포커스 이동(UI.FocusNext)이다
            if ( _maxLength > 0 && TextInputWidgetInternal::countCodepoints( _text ) >= _maxLength )
                continue;
            _text.append( event._text.data() + start, offset - start );
            bChanged = true;
        }
        refreshDisplay();
        if ( bChanged )
            _onTextChanged.broadcast( _text );
        return UiReply::makeHandled();
    }

    void TextInputWidget::onFocusChanged( bool bFocused )
    {
        if ( bFocused == false )
            _composition.clear();
        refreshDisplay();
    }

    void TextInputWidget::paintOverChildren( CanvasPainter& painter, const UiPaintContext& context ) const
    {
        (void)context;
        if ( hasFocus() == false )
            return;
        // 커서 — 글 끝(왼쪽에서 오른쪽이면 글 오른쪽, 오른쪽에서 왼쪽이면 왼쪽). 글 너비는 안의 글 위젯이 잰 원하는 크기다.
        const WidgetGeometry& display   = _pDisplay->getGeometry();
        const WidgetGeometry& self      = getGeometry();
        const float32         textWidth = ( _text.empty() && _composition.empty() ) ? 0.0f : _pDisplay->getDesiredSize()._x;
        const float32         localLeft = display._position._x - self._position._x;
        const float32         caretX    = isRightToLeft() ? localLeft + display._size._x - textWidth : localLeft + textWidth;
        CanvasBrush           caret{};
        caret._color = float4{ 1.0f, 1.0f, 1.0f, 0.9f };
        painter.fillRect( float2{ caretX, display._position._y - self._position._y }, float2{ 2.0f, display._size._y }, caret );
    }
} // namespace sw
