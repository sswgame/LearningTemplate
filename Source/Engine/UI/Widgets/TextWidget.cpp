#include "pch.h"

#include "Engine/UI/Widgets/TextWidget.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Layout/UiLayoutPass.h"
#include "Engine/UI/Layout/UiScale.h"
#include "Engine/UI/Render/UiPaintPass.h"
#include "Engine/UI/Style/WidgetStyle.h"

namespace sw
{
    namespace
    {
        struct TextWidgetInternal
        {
            /** @brief 리치 텍스트 색(0xRRGGBBAA)을 곧은 RGBA 로 바꿉니다. 알파는 위젯 색의 알파를 곱한다. */
            static float4 unpackColor( uint32 rgba, float32 alpha )
            {
                constexpr float32 kInv255 = 1.0f / 255.0f;
                return float4{ static_cast<float32>( ( rgba >> 24 ) & 0xFFu ) * kInv255, static_cast<float32>( ( rgba >> 16 ) & 0xFFu ) * kInv255,
                               static_cast<float32>( ( rgba >> 8 ) & 0xFFu ) * kInv255, static_cast<float32>( rgba & 0xFFu ) * kInv255 * alpha };
            }

            /** @brief 리치 텍스트 색이 없음(위젯 색 그대로)을 뜻하는 값입니다(`LaidOutGlyph::_colorRgba`). */
            static constexpr uint32 kWidgetColor = 0xFFFFFFFFu;
        };
    } // namespace
} // namespace sw

namespace sw
{
    TextWidget::TextWidget()
        : Widget{}
        , _text{}
        , _style{}
        , _color{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _outlineColor{ 0.0f, 0.0f, 0.0f, 1.0f }
        , _outlineWidth{ 0.0f }
        , _bRichText{ false }
        , _bLocalized{ true }
        , _displayText{}
        , _displayRevision{ 0 }
        , _bDisplayValid{ false }
        , _richText{}
        , _layoutCache{}
        , _layoutWidth{ 0.0f }
        , _layoutFontSize{ 0.0f }
        , _pLayoutStyle{ nullptr }
        , _bLayoutValid{ false }
        , _bLayoutRtl{ false }
        , _bRichParsed{ false }
    {
    }

    TextWidget::~TextWidget() = default;

    const TypeInfo* TextWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void TextWidget::setText( string_view text )
    {
        if ( _text == text )
            return;
        _text = string{ text };
        invalidateText();
    }

    void TextWidget::setTextStyle( const TextLayoutStyle& style )
    {
        _style = style;
        invalidateText();
    }

    void TextWidget::setColor( const float4& color )
    {
        if ( _color == color )
            return;
        _color = color;
        invalidate( WidgetDirty::kPaint );
    }

    void TextWidget::setOutline( const float4& color, float32 width )
    {
        if ( _outlineColor == color && _outlineWidth == width )
            return;
        _outlineColor = color;
        _outlineWidth = width;
        invalidate( WidgetDirty::kPaint );
    }

    void TextWidget::setRichText( bool bRichText )
    {
        if ( _bRichText == bRichText )
            return;
        _bRichText = bRichText;
        invalidateText();
    }

    void TextWidget::onBoundPropertyChanged( const PropertyInfo& property )
    {
        const hashed_string& name = property._name;
        if ( name == hashed_string( "_color" ) || name == hashed_string( "_outlineColor" ) || name == hashed_string( "_outlineWidth" ) )
        {
            invalidate( WidgetDirty::kPaint );
            return;
        }
        if ( name == hashed_string( "_text" ) || name == hashed_string( "_style" ) || name == hashed_string( "_bRichText" ) || name == hashed_string( "_bLocalized" ) )
        {
            invalidateText();
            return;
        }
        Widget::onBoundPropertyChanged( property );
    }

    void TextWidget::setLocalized( bool bLocalized )
    {
        if ( _bLocalized == bLocalized )
            return;
        _bLocalized = bLocalized;
        invalidateText();
    }

    void TextWidget::onTextRevisionChanged()
    {
        if ( _bLocalized && _text.empty() == false )
            invalidateText();
    }

    void TextWidget::resolveDisplayText( const LocalizationManager* pLocalization, uint32 textRevision ) const
    {
        if ( _bDisplayValid && _displayRevision == textRevision )
            return;
        const bool  bResolve  = _bLocalized && pLocalization != nullptr && _text.empty() == false;
        const utf8* pResolved = bResolve ? pLocalization->getStringByText( _text, _text.c_str() ) : _text.c_str();
        if ( _bDisplayValid == false || _displayText != pResolved )
        {
            _displayText  = pResolved;
            _bLayoutValid = false;
            _bRichParsed  = false;
        }
        _displayRevision = textRevision;
        _bDisplayValid   = true;
    }

    void TextWidget::invalidateText()
    {
        _bDisplayValid = false;
        _bLayoutValid  = false;
        _bRichParsed   = false;
        invalidate( WidgetDirty::kLayout | WidgetDirty::kPaint );
    }

    TextLayoutStyle TextWidget::makeLayoutStyle( float32 textScale ) const
    {
        TextLayoutStyle        style  = _style;
        const UiComputedStyle* pStyle = getComputedStyle();
        if ( pStyle != nullptr && pStyle->has( UiStyleField::Font ) )
            style._font = pStyle->_value._font;
        if ( pStyle != nullptr && pStyle->has( UiStyleField::FontSize ) )
            style._fontSize = pStyle->_value._fontSize;
        style._fontSize           = UiScaleUtil::computeScaledFontSize( style._fontSize, textScale );
        style._paragraphDirection = isRightToLeft() ? TextDirection::RightToLeft : TextDirection::LeftToRight;
        return style;
    }

    string_view TextWidget::getPlainText() const
    {
        if ( _bRichText == false )
            return _displayText;
        if ( _bRichParsed == false )
        {
            RichTextParser::parse( _displayText, _richText );
            _bRichParsed = true;
        }
        return _richText._plainText;
    }

    const vector<RichTextSpan>* TextWidget::getSpans() const
    {
        if ( _bRichText == false )
            return nullptr;
        (void)getPlainText();
        return &_richText._listSpan;
    }

    float2 TextWidget::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        if ( context._pTextLayout == nullptr || _text.empty() )
            return float2{};
        resolveDisplayText( context._pLocalization, context._textRevision );
        // 줄 바꿈이면 가용 너비 안에서 잰다(무한이면 한 줄). 줄 바꿈이 아니면 늘 한 줄이다.
        const bool    bBounded = UiLayoutPass::isUnbounded( availableSize._x ) == false && availableSize._x > 0.0f;
        const float32 maxWidth = _style._bWrap && bBounded ? availableSize._x : 0.0f;
        return context._pTextLayout->measure( getPlainText(), makeLayoutStyle( context._textScale ), maxWidth, getSpans() );
    }

    void TextWidget::paint( CanvasPainter& painter, const UiPaintContext& context ) const
    {
        if ( context._pTextLayout == nullptr || context._pGlyphCache == nullptr || _text.empty() )
            return;
        resolveDisplayText( context._pLocalization, context._textRevision );
        const TextLayoutStyle  style  = makeLayoutStyle( context._textScale );
        const float32          width  = getGeometry()._size._x;
        const bool             bRtl   = style._paragraphDirection == TextDirection::RightToLeft;
        const UiComputedStyle* pStyle = getComputedStyle();
        if ( _bLayoutValid == false || _layoutWidth != width || _layoutFontSize != style._fontSize || _bLayoutRtl != bRtl || _pLayoutStyle != pStyle )
        {
            // 픽셀 맞춤(UiLayoutPass)은 양 끝을 반올림해 원하는 너비에 놓인 글을 물리 픽셀 하나까지 줄인다 — 그만큼은 넘쳐도 한 줄로 둔다
            // (상자 줄의 단추 글이 끝 글자를 다음 줄로 넘기지 않게).
            const float32 snapSlack = width > 0.0f && context._uiScale > 0.0f ? 1.0f / context._uiScale : 0.0f;
            context._pTextLayout->layout( getPlainText(), style, width + snapSlack, _layoutCache, getSpans() );
            _layoutWidth    = width;
            _layoutFontSize = style._fontSize;
            _bLayoutRtl     = bRtl;
            _pLayoutStyle   = pStyle;
            _bLayoutValid   = true;
        }

        // 글 칸은 계산된 스타일이 정했으면(물려받은 것 포함) 그것, 아니면 자기 칸이다.
        const bool       bHasStyle = pStyle != nullptr;
        const float4     color     = bHasStyle && pStyle->has( UiStyleField::TextColor ) ? pStyle->_value._textColor : _color;
        CanvasGlyphStyle glyphStyle{};
        glyphStyle._outlineColor = bHasStyle && pStyle->has( UiStyleField::TextOutlineColor ) ? pStyle->_value._textOutlineColor : _outlineColor;
        glyphStyle._outlineWidth = bHasStyle && pStyle->has( UiStyleField::TextOutlineWidth ) ? pStyle->_value._textOutlineWidth : _outlineWidth;
        for ( const LaidOutGlyph& glyph : _layoutCache._listGlyph )
        {
            glyphStyle._fontSize    = glyph._fontSize;
            glyphStyle._color       = glyph._colorRgba == TextWidgetInternal::kWidgetColor ? color : TextWidgetInternal::unpackColor( glyph._colorRgba, color._w );
            glyphStyle._bFauxBold   = glyph._bFauxBold;
            glyphStyle._bFauxItalic = glyph._bFauxItalic;
            (void)painter.drawGlyph( glyph._origin, glyph._face, glyph._glyphIndex, glyphStyle, *context._pGlyphCache, context._frameIndex );
        }
    }
} // namespace sw
