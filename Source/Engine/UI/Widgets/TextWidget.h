/**
 * @file TextWidget.h
 * @brief 텍스트 한 줄이나 한 문단을 그리는 위젯입니다(UMG 의 TextBlock, 유니티의 Label, Godot 의 Label 에 해당).
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
#include "Engine/UI/Base/Widget.h"

namespace sw
{
    struct UIActionGlyphSource;

    class LocalizationManager;
} // namespace sw

namespace sw
{
    /**
     * @class TextWidget
     * @brief 텍스트를 배치(`TextLayoutEngine`)해 글리프 사각형으로 칠합니다.
     * @details 원하는 크기는 측정으로 구한다(글자 배율 `gv_uiTextScale` 을 곱한 크기이며 하한은 `UIScaleUtil::kMinScaledFontSize`, 줄 바꿈이면 가용 너비 안에서).
     *          칠할 때는 위젯 너비로 배치하고 결과를 캐시한다(텍스트, 스타일, 너비, 글자 배율, 방향이 같으면 다시 배치하지 않는다). 텍스트나 스타일이 바뀌면 `kLayout`, 색만 바뀌면 `kPaint` 이다.
     *          문단 방향은 이 위젯의 흐름 방향(`isRightToLeft`)이다. `_bRichText` 면 `[b]`, `[i]`, `[color=]`, `[size=]` 표기를 읽는다(`RichTextParser`).
     *          글리프 아틀라스를 쓰므로 페이지가 비워지면(세대가 오르면) 다시 칠한다.
     *          `_text` 는 현지화 키이거나 텍스트 그대로이고, 보이는 텍스트는 측정과 칠하기 때 컨텍스트의 문화권으로 해석해 텍스트 버전과 함께 캐시한다.
     *          언어가 바뀌면 `UISystem` 이 `onTextRevisionChanged` 로 알려 다시 해석하고 다시 측정한다(언리얼 FText 의 TextRevision 에 해당).
     */
    REFLECT( Category = "UI", DisplayName = "Text", Tooltip = "Draws a line or paragraph of text" )
    class SW_API TextWidget : public Widget
    {
    public:
        REFLECT_BODY();

        TextWidget();
        ~TextWidget() override;

        const TypeInfo* getTypeInfo() const override;
        /** @brief 바인딩이 쓴 필드에 맞춰 무효화합니다(색과 외곽선은 그리기만, 텍스트와 스타일은 배치 캐시를 버리고 레이아웃). */
        void onBoundPropertyChanged( const PropertyInfo& property ) override;

        /**
         * @brief 텍스트를 바꿉니다. 현지화 키이거나 텍스트 그대로여야 합니다(`Menu.Pause.Title`, `Paused`). 바뀌면 kLayout 입니다.
         * @details 보이는 텍스트는 `LocalizationManager::getStringByText( 키, 키 )` 로 해석한다. 테이블에 없으면 텍스트 그대로 쓴다. 코드는 키를 넣는다
         *          (`SW_LOCTEXT` 로 미리 해석한 텍스트를 넣으면 언어를 바꿔도 그대로다). 사용자가 입력한 텍스트처럼 해석하면 안 되는 텍스트는 `setLocalized( false )` 를 쓴다.
         */
        void          setText( string_view text );
        const string& getText() const { return _text; }
        /** @brief 마지막 측정이나 칠하기가 해석한 보이는 텍스트입니다(해석 전이면 빈 문자열). */
        const string& getDisplayText() const { return _displayText; }
        /** @brief 텍스트를 현지화 키로 해석할지 정합니다(기본 true). 바뀌면 kLayout 입니다. */
        void setLocalized( bool bLocalized );
        bool isLocalized() const { return _bLocalized; }
        void onTextRevisionChanged() override;
        void onInputGlyphsChanged() override;
        /** @brief 배치 스타일을 바꿉니다. 바뀌면 kLayout 입니다. */
        void                   setTextStyle( const TextLayoutStyle& style );
        const TextLayoutStyle& getTextStyle() const { return _style; }
        /** @brief 텍스트 색(직선 RGBA)을 바꿉니다. 바뀌면 kPaint 만 합니다. */
        void          setColor( const float4& color );
        const float4& getColor() const { return _color; }
        /** @brief 외곽선(색, 두께는 UI 단위)을 바꿉니다. kPaint 만 합니다. */
        void setOutline( const float4& color, float32 width );
        /** @brief 리치 텍스트 표기를 읽을지 바꿉니다. kLayout 입니다. */
        void setRichText( bool bRichText );
        bool isRichText() const { return _bRichText; }

        bool usesGlyphAtlas() const override { return true; }
        /** @brief 글 색 · 외곽선 · 글자 크기를 자기 값으로 답합니다(스타일 전환의 "한쪽만 정한 필드"). */
        [[nodiscard]] bool tryGetOwnStyleField( UIStyleField field, float32 ( &outArr )[4] ) const override;

        /** @brief 마지막 칠하기의 배치 결과입니다(테스트와 커서 위치용). */
        const TextLayoutResult& getLastLayout() const { return _layoutCache; }

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   paint( CanvasPainter& painter, const UIPaintContext& context ) const override;

    private:
        /** @brief 글자 배율과 이 위젯의 흐름 방향을 적용한 배치 스타일입니다. */
        TextLayoutStyle makeLayoutStyle( float32 textScale ) const;
        /** @brief 배치할 평문입니다. 리치 텍스트면 표기를 뺀 텍스트이며, 파싱은 텍스트가 바뀔 때 한 번만 합니다. */
        string_view getPlainText() const;
        /** @brief 리치 텍스트 구간입니다(리치 텍스트가 아니면 nullptr). */
        const vector<RichTextSpan>* getSpans() const;
        /** @brief 텍스트나 스타일이 바뀌었을 때 호출합니다. 해석 결과, 배치 캐시, 리치 텍스트 파싱을 버리고 kLayout 으로 무효화합니다. */
        void invalidateText();
        /**
         * @brief 보이는 텍스트를 문화권 @p pLocalization 의 버전 @p textRevision 으로 해석하고, 액션 태그(`[action=이름]`)를 @p pActionGlyphs 의 글리프로 바꿉니다.
         *        같은 버전이면 캐시를 쓰고, 바뀌면 배치와 리치 텍스트 캐시를 버립니다.
         */
        void resolveDisplayText( const LocalizationManager* pLocalization, uint32 textRevision, const UIActionGlyphSource* pActionGlyphs ) const;

    private:
        PROPERTY( DisplayName = "Text", Meta = "Localizable", Tooltip = "Localization key, or the text itself when the tables have no such key" )
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
        PROPERTY( DisplayName = "Localized", Tooltip = "Resolve the text as a localization key (off for text the player typed)" )
        bool _bLocalized;

        mutable string _displayText;     ///< 해석한 보이는 텍스트입니다(`_bDisplayValid` 일 때 유효).
        mutable uint32 _displayRevision; ///< 그 해석의 텍스트 버전입니다.
        mutable bool   _bDisplayValid;   ///< `_displayText` 가 지금 텍스트와 버전의 것인지 나타냅니다.
        mutable bool   _bHasActionTag;   ///< 해석한 텍스트에 액션 태그가 있었는지 나타냅니다(입력 장치가 바뀌면 다시 해석합니다).

        mutable RichTextParseResult _richText;       ///< 리치 텍스트 파싱 결과입니다(`_bRichParsed` 일 때 유효).
        mutable TextLayoutResult    _layoutCache;    ///< 마지막 칠하기의 배치입니다.
        mutable float32             _layoutWidth;    ///< 그 배치의 너비입니다.
        mutable float32             _layoutFontSize; ///< 그 배치의 폰트 크기입니다(글자 배율 포함).
        mutable const void*         _pLayoutStyle;   ///< 그 배치를 만든 계산된 스타일입니다(바뀌면 폰트가 바뀌므로 다시 배치합니다).
        mutable bool                _bLayoutValid;   ///< 배치 캐시가 지금 텍스트와 스타일의 것인지 나타냅니다.
        mutable bool                _bLayoutRtl;     ///< 그 배치의 문단 방향입니다.
        mutable bool                _bRichParsed;    ///< `_richText` 가 지금 텍스트의 것인지 나타냅니다.
    };
} // namespace sw
