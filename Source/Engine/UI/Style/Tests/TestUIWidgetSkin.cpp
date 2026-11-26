// UIWidgetSkinTest — 엔진 위젯 기본 겉모습은 스타일 시트에서 온다: 시트를 바꾸면 위젯이 따라 바뀌고, 칸이 하나라도 빠지면 오류다. 위젯만 만든다(nogpu).
#include "pch.h"

#include "Core/Container/pair.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/Style/UIWidgetSkin.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/CheckBoxWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"

namespace sw
{
    /** @brief 안을 보는 테스트 창구입니다(`friend`) — 다른 겉모습을 걸고 사라질 때 되돌립니다. */
    struct UIWidgetSkinTestAccess
    {
        explicit UIWidgetSkinTestAccess( UIWidgetSkin skin )
            : _service{ engine::getSubsystem<UIWidgetSkinService>() }
            , _previous{ _service._skin }
        {
            _service._skin = make_shared<UIWidgetSkin>( std::move( skin ) );
        }
        ~UIWidgetSkinTestAccess() { _service._skin = _previous; }
        UIWidgetSkinTestAccess( const UIWidgetSkinTestAccess& )            = delete;
        UIWidgetSkinTestAccess& operator=( const UIWidgetSkinTestAccess& ) = delete;

    private:
        UIWidgetSkinService&           _service;
        shared_ptr<const UIWidgetSkin> _previous;
    };
} // namespace sw

using namespace sw;

namespace
{
    struct UIWidgetSkinTestUtil
    {
        /** @brief 엔진 기본 시트의 글입니다. */
        static string readSheetText()
        {
            string text;
            SW_EXPECT_TRUE( AssetIO::readTextAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._uiWidgetSkin.get() }, text ) );
            return text;
        }

        static expected<UIWidgetSkin> build( const string& text )
        {
            UIStyleSheetAsset sheet;
            if ( expected<void> parsed = UIStyleSheetAsset::parse( text, "skin.uistyle.xml", ResourceContext::forDocument(), sheet ); parsed.has_value() == false )
                return sw::unexpected( parsed.error() );
            return UIWidgetSkin::build( sheet );
        }

        /** @brief 시트 글에서 @p from 을 @p to 로 바꾼 글입니다. @p from 은 시트에 있어야 합니다. */
        static string replaceOnce( const string& text, string_view from, string_view to )
        {
            const size_t position = text.find( from );
            SW_EXPECT_TRUE( position != string::npos );
            if ( position == string::npos )
                return text;
            string result = text.substr( 0, position );
            result.append( to ).append( string_view( text ).substr( position + from.size() ) );
            return result;
        }
    };
    using Util = UIWidgetSkinTestUtil;
} // namespace

/** @brief [UIWidgetSkinTest] 시트 값을 바꾸면 새로 만든 위젯의 배경 · 안쪽 여백 · 색이 따라 바뀐다 */
SW_TEST_CASE( UIWidgetSkinTest, WidgetsFollowTheSheet )
{
    const string           text = Util::replaceOnce( Util::readSheetText(), "_backgroundColor=\"0.16,0.176,0.216,0.95\" _cornerRadius=\"6,6,6,6\" _padding=\"16,8,16,8\"",
                                                     "_backgroundColor=\"0.5,0.25,0.125,1\" _cornerRadius=\"2,2,2,2\" _padding=\"1,2,3,4\"" );
    expected<UIWidgetSkin> skin = Util::build( Util::replaceOnce( text, "0.95,0.75,0.2,1\" _cornerRadius=\"2,2,2,2\"", "0,1,0,1\" _cornerRadius=\"2,2,2,2\"" ) );
    SW_REQUIRE_TRUE( skin.has_value() );
    SW_EXPECT_TRUE( ( skin->_checkBox._mark._color == float4{ 0.0f, 1.0f, 0.0f, 1.0f } ) );
    const float4 oldPadding = ButtonWidget{}.getContentPadding();
    {
        const UIWidgetSkinTestAccess applied{ std::move( *skin ) };
        const ButtonWidget           button;
        SW_EXPECT_TRUE( ( button.getBackground()._color == float4{ 0.5f, 0.25f, 0.125f, 1.0f } ) );
        SW_EXPECT_TRUE( ( button.getBackground()._cornerRadius == float4{ 2.0f, 2.0f, 2.0f, 2.0f } ) );
        SW_EXPECT_TRUE( ( button.getContentPadding() == float4{ 1.0f, 2.0f, 3.0f, 4.0f } ) );
        SW_EXPECT_TRUE( ( CheckBoxWidget{}.getContentPadding() == float4{} ) ); // 버튼의 것을 물려받지 않는다
    }
    SW_EXPECT_TRUE( ButtonWidget{}.getContentPadding() == oldPadding );
}

/** @brief [UIWidgetSkinTest] 상태 규칙이 적지 않은 칸은 바탕 규칙의 것이고, 상태의 배경색은 자기 규칙이 적은 것이다 */
SW_TEST_CASE( UIWidgetSkinTest, StatesInheritTheBaseRule )
{
    const expected<UIWidgetSkin> skin = Util::build( Util::readSheetText() );
    SW_REQUIRE_TRUE( skin.has_value() );
    const UIWidgetSkin::StateBrushes& button = skin->_button._brush;
    SW_EXPECT_TRUE( button._hovered._cornerRadius == button._normal._cornerRadius );
    SW_EXPECT_FALSE( button._hovered._color == button._normal._color );
    SW_EXPECT_FALSE( button._pressed._color == button._hovered._color );
    SW_EXPECT_TRUE( button._disabled._color._w < button._normal._color._w );
}

/** @brief [UIWidgetSkinTest] 칸이 빠지면(바탕 · 상태 · 부품) 그 칸의 이름을 적은 오류다 — 다른 값으로 대신하지 않는다 */
SW_TEST_CASE( UIWidgetSkinTest, MissingValuesAreErrors )
{
    const string                         text      = Util::readSheetText();
    const pair<string_view, string_view> arrCase[] = {
        {           "<Rule _selector=\"ButtonWidget:hover\" _backgroundColor=\"0.24,0.264,0.324,0.95\" />", "ButtonWidget:hover _backgroundColor"},
        {                                    "<Rule _selector=\"RotatorWidget\" _padding=\"32,8,32,8\" />",              "RotatorWidget _padding"},
        {"<Rule _selector=\"SliderWidget.thumb\" _backgroundColor=\"0.95,0.95,0.98,1\" _size=\"16,16\" />", "SliderWidget.thumb _backgroundColor"},
        {                                                                                 " _size=\"2,0\"",         "TextInputWidget.caret _size"},
    };
    for ( const pair<string_view, string_view>& row : arrCase )
    {
        const expected<UIWidgetSkin> skin = Util::build( Util::replaceOnce( text, row.first, "" ) );
        SW_EXPECT_FALSE( skin.has_value() );
        if ( skin.has_value() == false )
            SW_EXPECT_EQUAL( string( row.second ), string( skin.error().detail() ) );
    }
}
