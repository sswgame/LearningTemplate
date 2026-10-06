/**
 * @file TextWidget.h
 * @brief 글 한 줄 · 한 문단을 그리는 위젯입니다(UMG TextBlock · 유니티 Label · Godot Label).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Text/RichTextParser.h"
#include "Engine/Text/TextLayout.h"
#include "Engine/UI/Core/Widget.h"

namespace sw
{
    /**
     * @class TextWidget
     * @brief 글을 배치(`TextLayoutEngine`)해 글리프 사각형으로 칠합니다.
     * @details 원하는 크기 = 측정(글자 배율 `gv_uiTextScale` 을 곱한 크기, 줄 바꿈이면 가용 너비 안). 칠하기는 위젯 너비로 배치하고 결과를 캐시합니다
     *          (글 · 스타일 · 너비 · 글자 배율 · 방향이 같으면 다시 배치하지 않는다). 글 · 스타일이 바뀌면 `kLayout`, 색만 바뀌면 `kPaint` 입니다.
     *          문단 방향은 이 위젯의 흐름 방향(`isRightToLeft`)입니다. `_bRichText` 면 `[b]` · `[i]` · `[color=]` · `[size=]` 표기를 읽습니다(`RichTextParser`).
     *          글리프 아틀라스를 쓰므로 페이지가 비워지면(세대가 오르면) 다시 칠합니다.
     */
    REFLECT( Category = "UI", DisplayName = "Text", Tooltip = "Draws a line or paragraph of text" )
    class SW_API TextWidget : public Widget
    {
    public:
        REFLECT_BODY();

        TextWidget();
        ~TextWidget() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 글을 바꿉니다(현지화 키 풀기는 6-2). 바뀌면 kLayout. */
        void          setText( string_view text );
        const string& getText() const { return _text; }
        /** @brief 배치 스타일을 바꿉니다. 바뀌면 kLayout. */
        void                   setTextStyle( const TextLayoutStyle& style );
        const TextLayoutStyle& getTextStyle() const { return _style; }
        /** @brief 글 색(곧은 RGBA)을 바꿉니다. 바뀌면 kPaint 만. */
        void          setColor( const float4& color );
        const float4& getColor() const { return _color; }
        /** @brief 외곽선(색 · 두께 UI 단위)을 바꿉니다. kPaint 만. */
        void setOutline( const float4& color, float32 width );
        /** @brief 리치 텍스트 표기를 읽는가를 바꿉니다. kLayout. */
        void setRichText( bool bRichText );
        bool isRichText() const { return _bRichText; }

        bool usesGlyphAtlas() const override { return true; }

        /** @brief 마지막 칠하기의 배치 결과입니다(시험 · 커서 위치). */
        const TextLayoutResult& getLastLayout() const { return _layoutCache; }

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   paint( CanvasPainter& painter, const UiPaintContext& context ) const override;

    private:
        /** @brief 글자 배율 · 이 위젯의 흐름 방향을 얹은 배치 스타일입니다. */
        TextLayoutStyle makeLayoutStyle( float32 textScale ) const;
        /** @brief 배치할 평문입니다 — 리치 텍스트면 표기를 뺀 글(파싱은 글이 바뀔 때 한 번). */
        string_view getPlainText() const;
        /** @brief 리치 텍스트 구간입니다(리치 텍스트가 아니면 nullptr). */
        const vector<RichTextSpan>* getSpans() const;
        /** @brief 글 · 스타일이 바뀌었다 — 배치 캐시 · 리치 텍스트 파싱을 버리고 kLayout. */
        void invalidateText();

    private:
        PROPERTY( DisplayName = "Text", Meta = "Localizable", Tooltip = "Text, or a localization key once text binding (6-2) resolves it" )
        string _text;
        PROPERTY( DisplayName = "Style" )
        TextLayoutStyle _style;
        PROPERTY( DisplayName = "Color" )
        float4 _color;
        PROPERTY( DisplayName = "Outline Color" )
        float4 _outlineColor;
        PROPERTY( DisplayName = "Outline Width", Min = 0.0, Meta = "Units=ui" )
        float32 _outlineWidth;
        PROPERTY( DisplayName = "Rich Text", Tooltip = "Read [b] [i] [color=] [size=] markup" )
        bool _bRichText;

        mutable RichTextParseResult _richText;       ///< 리치 텍스트 파싱 결과(`_bRichParsed` 일 때 유효)
        mutable TextLayoutResult    _layoutCache;    ///< 마지막 칠하기의 배치
        mutable float32             _layoutWidth;    ///< 그 배치의 너비
        mutable float32             _layoutFontSize; ///< 그 배치의 글꼴 크기(글자 배율 포함)
        mutable const void*         _pLayoutStyle;   ///< 그 배치를 만든 계산된 스타일(바뀌면 다시 배치 — 글꼴이 바뀐다)
        mutable bool                _bLayoutValid;   ///< 배치 캐시가 지금 글 · 스타일의 것이다
        mutable bool                _bLayoutRtl;     ///< 그 배치의 문단 방향
        mutable bool                _bRichParsed;    ///< `_richText` 가 지금 글의 것이다
    };
} // namespace sw
