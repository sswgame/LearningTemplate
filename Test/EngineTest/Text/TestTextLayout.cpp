#include "pch.h"

#include "Engine/Text/TextLayout.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// TextLayoutTest — 줄 바꿈(UAX #14 단순판) · 정렬 · 줄임표 · 측정. 가짜 래스터라이저 — 글자 0.5 em, 공백 0.25 em, 크기 10 이면 글자 5 · 공백 2.5,
// 상승 0.8 · 하강 -0.2 라 줄 높이 10(nogpu).

namespace
{
    /** @brief 가짜 글꼴 시스템 + 배치기입니다. 기본 가족 "Latin"(라틴 · 한글 · 한자 · 기호 모두), 큰 가족 "Tall"(한글만, 상승 1.0). */
    struct TextLayoutTestFixture
    {
        static constexpr const utf8* kLatinPath = "test/fonts/latin.ttf";
        static constexpr const utf8* kTallPath  = "test/fonts/tall.ttf";

        sw::test::FakeFontSystemFixture _fonts;
        sw::TextLayoutEngine            _layout;
        bool                            _bInitialized;

        TextLayoutTestFixture()
            : _fonts{}
            , _layout{ *_fonts._fontSystem }
            , _bInitialized{ false }
        {
            sw::test::FakeFontFaceConfig tall{};
            tall.addRange( 0xAC00u, 0xD7A3u );
            tall._ascender = 1.0f;
            _fonts._pRasterizer->setFaceConfig( kTallPath, tall );
            sw::FontCatalogDesc catalog{};
            catalog._defaultFamily = "Latin";
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Latin", kLatinPath );
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Tall", kTallPath );
            _bInitialized = _fonts.initialize( catalog );
        }

        static sw::TextLayoutStyle makeStyle()
        {
            sw::TextLayoutStyle style{};
            style._fontSize = 10.0f;
            return style;
        }
    };
} // namespace

/** @brief [TextLayoutTest] 공백에서 끊고 끝 공백은 너비에서 뺀다 — "aaaa bbbb" 를 너비 30 에 두 줄 */
SW_TEST_CASE( TextLayoutTest, WrapsAtSpacesAndTrimsTrailingSpace )
{
    TextLayoutTestFixture fixture;
    SW_ASSERT_TRUE( fixture._bInitialized );
    sw::TextLayoutResult result{};
    fixture._layout.layout( "aaaa bbbb", TextLayoutTestFixture::makeStyle(), 30.0f, result );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( result._listLine.size() ) );
    SW_EXPECT_NEAR_EQUAL( 20.0f, result._listLine[0]._width, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, result._listLine[1]._width, 1e-4f );
    SW_EXPECT_EQUAL( 0u, result._listLine[0]._firstByte );
    SW_EXPECT_EQUAL( 5u, result._listLine[0]._byteCount );
    SW_EXPECT_EQUAL( 5u, result._listLine[1]._firstByte );
    SW_EXPECT_EQUAL( 5u, result._listLine[0]._glyphCount ); // 끝 공백 글리프는 남는다
    SW_EXPECT_NEAR_EQUAL( 10.0f, result._listLine[0]._height, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 8.0f, result._listLine[0]._baseline, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 18.0f, result._listLine[1]._baseline, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, result._size._y, 1e-4f );
}

/** @brief [TextLayoutTest] KeepAll 은 한글 낱말을 통째로 넘기고(공백에서만), Normal 은 음절 사이에서도 끊어 줄을 채운다 — "가나 다라마" 를 너비 20 에 */
SW_TEST_CASE( TextLayoutTest, KeepAllKeepsHangulWordsWhole )
{
    TextLayoutTestFixture fixture;
    SW_ASSERT_TRUE( fixture._bInitialized );
    const sw::string     text  = "\xEA\xB0\x80\xEB\x82\x98 \xEB\x8B\xA4\xEB\x9D\xBC\xEB\xA7\x88"; // "가나 다라마"
    sw::TextLayoutStyle  style = TextLayoutTestFixture::makeStyle();
    sw::TextLayoutResult keepAll{};
    style._wordBreak = sw::TextWordBreak::KeepAll;
    fixture._layout.layout( text, style, 20.0f, keepAll );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( keepAll._listLine.size() ) );
    SW_EXPECT_EQUAL( 7u, keepAll._listLine[0]._byteCount ); // "가나 "
    SW_EXPECT_NEAR_EQUAL( 10.0f, keepAll._listLine[0]._width, 1e-4f );

    sw::TextLayoutResult normal{};
    style._wordBreak = sw::TextWordBreak::Normal;
    fixture._layout.layout( text, style, 20.0f, normal );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( normal._listLine.size() ) );
    SW_EXPECT_EQUAL( 10u, normal._listLine[0]._byteCount ); // "가나 다"
    SW_EXPECT_NEAR_EQUAL( 17.5f, normal._listLine[0]._width, 1e-4f );
}

/** @brief [TextLayoutTest] 기회가 없는 긴 낱말은 그 자리에서 끊는다 — "aaaaaaaaaa" 를 너비 20 에 넷 · 넷 · 둘 */
SW_TEST_CASE( TextLayoutTest, LongWordBreaksAnywhere )
{
    TextLayoutTestFixture fixture;
    SW_ASSERT_TRUE( fixture._bInitialized );
    sw::TextLayoutResult result{};
    fixture._layout.layout( "aaaaaaaaaa", TextLayoutTestFixture::makeStyle(), 20.0f, result );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( result._listLine.size() ) );
    SW_EXPECT_EQUAL( 4u, result._listLine[0]._glyphCount );
    SW_EXPECT_EQUAL( 4u, result._listLine[1]._glyphCount );
    SW_EXPECT_EQUAL( 2u, result._listLine[2]._glyphCount );
}

/** @brief [TextLayoutTest] 닫는 부호는 줄 머리로 가지 않는다 — 한중일 쉼표 앞 기회는 없고, 강제로 끊을 때도 한 글자 앞으로 물린다 */
SW_TEST_CASE( TextLayoutTest, NoBreakBeforeClosingPunctuation )
{
    TextLayoutTestFixture fixture;
    SW_ASSERT_TRUE( fixture._bInitialized );
    sw::TextLayoutStyle style = TextLayoutTestFixture::makeStyle();
    style._wordBreak          = sw::TextWordBreak::Normal;
    // "가나다、라" 너비 17.5 — 다 뒤(、 앞)는 기회가 아니므로 나 뒤에서 끊는다.
    sw::TextLayoutResult cjk{};
    fixture._layout.layout( "\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4\xE3\x80\x81\xEB\x9D\xBC", style, 17.5f, cjk );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( cjk._listLine.size() ) );
    SW_EXPECT_EQUAL( 2u, cjk._listLine[0]._glyphCount );
    SW_EXPECT_EQUAL( 6u, cjk._listLine[1]._firstByte );

    // "aaa, bbb" 너비 17.5 — 기회가 없어 강제로 끊지만 쉼표 앞이 아니라 한 글자 앞에서.
    sw::TextLayoutResult latin{};
    fixture._layout.layout( "aaa, bbb", TextLayoutTestFixture::makeStyle(), 17.5f, latin );
    SW_ASSERT_TRUE( latin._listLine.size() >= 2 );
    SW_EXPECT_EQUAL( 2u, latin._listLine[0]._glyphCount );
    SW_EXPECT_EQUAL( 2u, latin._listLine[1]._firstByte );
}

/** @brief [TextLayoutTest] \n 은 반드시 끊는다 — 줄 바꿈을 끈(_bWrap = false) 글에서도 */
SW_TEST_CASE( TextLayoutTest, MandatoryNewline )
{
    TextLayoutTestFixture fixture;
    SW_ASSERT_TRUE( fixture._bInitialized );
    sw::TextLayoutStyle style = TextLayoutTestFixture::makeStyle();
    style._bWrap              = false;
    sw::TextLayoutResult result{};
    fixture._layout.layout( "a\nb", style, 0.0f, result );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( result._listLine.size() ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( result._listGlyph.size() ) ); // 줄 바꿈 문자는 그리지 않는다
    SW_EXPECT_NEAR_EQUAL( 5.0f, result._listLine[0]._width, 1e-4f );
    SW_EXPECT_EQUAL( 2u, result._listLine[1]._firstByte );
    SW_EXPECT_NEAR_EQUAL( 0.0f, result._listGlyph[1]._origin._x, 1e-4f );
}

/** @brief [TextLayoutTest] 줄임표 — 줄 수를 넘으면 마지막 줄 끝을 "…" 가 들어갈 만큼 빼고 붙인다 */
SW_TEST_CASE( TextLayoutTest, EllipsisTruncatesLastLine )
{
    TextLayoutTestFixture fixture;
    SW_ASSERT_TRUE( fixture._bInitialized );
    sw::TextLayoutStyle style = TextLayoutTestFixture::makeStyle();
    style._maxLines           = 1;
    style._overflow           = sw::TextOverflow::Ellipsis;
    sw::TextLayoutResult result{};
    fixture._layout.layout( "aaaaaaaa", style, 20.0f, result );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( result._listLine.size() ) );
    SW_EXPECT_EQUAL( SW_TRUE, result._bTruncated );
    SW_ASSERT_TRUE( result._listGlyph.size() <= 4 && result._listGlyph.empty() == false );
    SW_EXPECT_EQUAL( 0x2026u, result._listGlyph.back()._glyphIndex ); // 가짜는 글리프 번호 = 코드 포인트
    SW_EXPECT_TRUE( result._listLine[0]._width <= 20.0f + 1e-4f );

    style._overflow = sw::TextOverflow::Clip;
    fixture._layout.layout( "aaaaaaaa", style, 20.0f, result );
    SW_EXPECT_EQUAL( SW_TRUE, result._bTruncated );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( result._listGlyph.size() ) );
}

/** @brief [TextLayoutTest] 정렬 — 너비 40 에 "aa"(10) 의 첫 원점이 가운데 15, 끝 30 */
SW_TEST_CASE( TextLayoutTest, AlignmentCenterAndEnd )
{
    TextLayoutTestFixture fixture;
    SW_ASSERT_TRUE( fixture._bInitialized );
    sw::TextLayoutStyle  style = TextLayoutTestFixture::makeStyle();
    sw::TextLayoutResult result{};
    style._alignment = sw::TextAlignment::Center;
    fixture._layout.layout( "aa", style, 40.0f, result );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( result._listGlyph.size() ) );
    SW_EXPECT_NEAR_EQUAL( 15.0f, result._listGlyph[0]._origin._x, 1e-4f );
    style._alignment = sw::TextAlignment::End;
    fixture._layout.layout( "aa", style, 40.0f, result );
    SW_EXPECT_NEAR_EQUAL( 30.0f, result._listGlyph[0]._origin._x, 1e-4f );
    style._alignment = sw::TextAlignment::Start;
    fixture._layout.layout( "aa", style, 40.0f, result );
    SW_EXPECT_NEAR_EQUAL( 0.0f, result._listGlyph[0]._origin._x, 1e-4f );
}

/** @brief [TextLayoutTest] 면이 섞인 줄의 높이는 큰 쪽 메트릭이다 — 라틴(상승 0.8) + 큰 한글 면(상승 1.0) 줄은 12, 라틴만은 10 */
SW_TEST_CASE( TextLayoutTest, MixedFaceLineUsesTallestMetrics )
{
    TextLayoutTestFixture fixture;
    SW_ASSERT_TRUE( fixture._bInitialized );
    sw::TextLayoutStyle style = TextLayoutTestFixture::makeStyle();
    style._font._family       = "Tall"; // 사슬 = [Tall(한글만), Latin]
    sw::TextLayoutResult latinOnly{};
    fixture._layout.layout( "ab", style, 0.0f, latinOnly );
    sw::TextLayoutResult mixed{};
    fixture._layout.layout( "a\xEA\xB0\x80", style, 0.0f, mixed ); // "a가"
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( mixed._listLine.size() ) );
    SW_EXPECT_NEAR_EQUAL( 12.0f, mixed._listLine[0]._height, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, mixed._listLine[0]._baseline, 1e-4f );
    SW_EXPECT_NOT_EQUAL( mixed._listGlyph[0]._face, mixed._listGlyph[1]._face );

    style._font._family = "Latin";
    fixture._layout.layout( "ab", style, 0.0f, latinOnly );
    SW_EXPECT_NEAR_EQUAL( 10.0f, latinOnly._listLine[0]._height, 1e-4f );
}

/** @brief [TextLayoutTest] 측정은 배치의 크기와 같다 — 캐시 적중 뒤에도, 너비가 달라지면 다시 잰다 */
SW_TEST_CASE( TextLayoutTest, MeasureMatchesLayoutSize )
{
    TextLayoutTestFixture fixture;
    SW_ASSERT_TRUE( fixture._bInitialized );
    const sw::TextLayoutStyle style = TextLayoutTestFixture::makeStyle();
    sw::TextLayoutResult      result{};
    fixture._layout.layout( "aaaa bbbb cc", style, 30.0f, result );
    const sw::float2 first  = fixture._layout.measure( "aaaa bbbb cc", style, 30.0f );
    const sw::float2 cached = fixture._layout.measure( "aaaa bbbb cc", style, 30.0f );
    SW_EXPECT_NEAR_EQUAL( result._size._x, first._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( result._size._y, first._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( first._x, cached._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( first._y, cached._y, 1e-4f );
    const sw::float2 wide = fixture._layout.measure( "aaaa bbbb cc", style, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, wide._y, 1e-4f ); // 한 줄
    SW_EXPECT_NEAR_EQUAL( 0.0f, fixture._layout.measure( "", style, 30.0f )._x, 1e-4f );
}
