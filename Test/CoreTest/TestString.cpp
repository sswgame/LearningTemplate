#include "pch.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_String — Util·해시·스플리터·빌더
// ------------------------------------------------------------------------------
/**
 * @brief [Core_String] StringUtil 기본
 */

SW_TEST_CASE( Core_String, StringUtilBasic )
{
    SW_EXPECT_TRUE( sw::StringUtil::isNullOrEmpty( static_cast<const utf8*>( nullptr ) ) );
    SW_EXPECT_TRUE( sw::StringUtil::isNullOrEmpty( "" ) );
    SW_EXPECT_FALSE( sw::StringUtil::isNullOrEmpty( "Hello" ) );

    sw::string text    = "  Hello World!  ";
    sw::string trimmed = sw::StringUtil::trim( text.c_str() );
    SW_EXPECT_EQUAL( sw::string( "Hello World!" ), trimmed );

    sw::string upper = sw::StringUtil::toUpper( "hello" );
    SW_EXPECT_EQUAL( sw::string( "HELLO" ), upper );

    sw::string lower = sw::StringUtil::toLower( "WORLD" );
    SW_EXPECT_EQUAL( sw::string( "world" ), lower );

    SW_EXPECT_TRUE( sw::StringUtil::equals( "RenderPass", "renderpass", true ) );
    SW_EXPECT_FALSE( sw::StringUtil::equals( "RenderPass", "renderpass", false ) );
    SW_EXPECT_TRUE( sw::StringUtil::equals( "HP", "hp", true ) );
    SW_EXPECT_FALSE( sw::StringUtil::equals( "Hero", "hero!", true ) );
    SW_EXPECT_TRUE( sw::StringUtil::equals( static_cast<const utf8*>( "Scene" ), "scene", true ) );
    SW_EXPECT_FALSE( sw::StringUtil::equals( static_cast<const utf8*>( nullptr ), "x", true ) );
    SW_EXPECT_EQUAL( 0, sw::StringUtil::compare( "abc", "abc" ) );
    SW_EXPECT_TRUE( sw::StringUtil::compare( "ABC", "abc", true ) == 0 );
    SW_EXPECT_TRUE( sw::StringUtil::compare( "abc", "def" ) < 0 );
    SW_EXPECT_TRUE( sw::StringUtil::compare( "def", "abc" ) > 0 );

    const sw::string_splitter parts{ "apple,banana,orange", { "," } };
    SW_EXPECT_EQUAL( 3u, parts.getCount() );
    if ( parts.getCount() == 3 )
    {
        SW_EXPECT_EQUAL( sw::string( "apple" ), sw::string( parts.getSplitList()[0] ) );
        SW_EXPECT_EQUAL( sw::string( "banana" ), sw::string( parts.getSplitList()[1] ) );
        SW_EXPECT_EQUAL( sw::string( "orange" ), sw::string( parts.getSplitList()[2] ) );
    }
}

/**
 * @brief [Core_String] 공통 접두·접미를 뺀 변경 구간
 */
SW_TEST_CASE( Core_String, StringChangeSpanStoresOnlyChangedMiddle )
{
    const sw::string           before = R"({"albedo":"white","roughness":0.5,"name":"Mat"})";
    const sw::string           after  = R"({"albedo":"white","roughness":0.8,"name":"Mat"})";
    const sw::StringChangeSpan span   = sw::StringUtil::makeChangeSpan( before, after );
    SW_EXPECT_TRUE( span._removed == "5" );
    SW_EXPECT_TRUE( span._added == "8" );
    SW_EXPECT_TRUE( span._prefixLength + span._suffixLength + span._removed.size() == before.size() );
    SW_EXPECT_TRUE( sw::StringUtil::reconstructBefore( span, after ) == before );
    SW_EXPECT_TRUE( sw::StringUtil::reconstructAfter( span, before ) == after );
}

/**
 * @brief [Core_String] 첫 편집 스팬이 이후 after에서도 역변환된다
 */
SW_TEST_CASE( Core_String, StringChangeSpanFirstEditReversesLaterAfter )
{
    const sw::string           before0   = R"({"field":"x"})";
    const sw::string           after1    = R"({"field":"xy"})";
    const sw::string           afterN    = R"({"field":"xyz"})";
    const sw::StringChangeSpan firstSpan = sw::StringUtil::makeChangeSpan( before0, after1 );
    SW_EXPECT_TRUE( sw::StringUtil::reconstructBefore( firstSpan, afterN ) == before0 );
}

/**
 * @brief [Core_String] hashed_string
 */
SW_TEST_CASE( Core_String, HashedString )
{
    sw::hashed_string defaultStr;
    sw::hashed_string str1( "TestKey" );
    sw::hashed_string str2( "testkey" );
    sw::hashed_string str3( "TESTKEY" );
    sw::hashed_string str4( "OtherKey" );

    SW_EXPECT_TRUE( defaultStr.empty() );
    SW_EXPECT_FALSE( str1.empty() );
    SW_EXPECT_TRUE( str1 == str2 );
    SW_EXPECT_TRUE( str1 == str3 );
    SW_EXPECT_EQUAL( str1.getIndex(), str2.getIndex() );
    SW_EXPECT_EQUAL( str1.getIndex(), str3.getIndex() );
    SW_EXPECT_TRUE( str1 != str4 );
}

/**
 * @brief [Core_String] string_splitter
 */
SW_TEST_CASE( Core_String, StringSplitter )
{
    // 1) 기존 initializer_list 호환성
    {
        sw::string_splitter                 splitter( "one|two|three", { "|" } );
        const sw::vector<std::string_view>& tokens = splitter.getSplitList();

        SW_EXPECT_EQUAL( 3u, splitter.getCount() );
        SW_EXPECT_FALSE( splitter.empty() );
        if ( tokens.size() == 3 )
        {
            SW_EXPECT_EQUAL( std::string_view( "one" ), tokens[0] );
            SW_EXPECT_EQUAL( std::string_view( "two" ), tokens[1] );
            SW_EXPECT_EQUAL( std::string_view( "three" ), tokens[2] );
        }
    }

    // 2) 단일 문자(char) SIMD 패스트 패스 생성자 및 인덱싱
    {
        sw::string_splitter splitter( "apple/banana/cherry", '/' );
        SW_EXPECT_EQUAL( 3u, splitter.getCount() );
        SW_EXPECT_EQUAL( std::string_view( "apple" ), splitter[0] );
        SW_EXPECT_EQUAL( std::string_view( "banana" ), splitter[1] );
        SW_EXPECT_EQUAL( std::string_view( "cherry" ), splitter[2] );
    }

    // 3) 단일 문자열 뷰(string_view) 생성자
    {
        sw::string_splitter splitter( "root::sub::leaf", "::" );
        SW_EXPECT_EQUAL( 3u, splitter.getCount() );
        SW_EXPECT_EQUAL( std::string_view( "root" ), splitter[0] );
        SW_EXPECT_EQUAL( std::string_view( "sub" ), splitter[1] );
        SW_EXPECT_EQUAL( std::string_view( "leaf" ), splitter[2] );
    }

    // 4) 다중 단일 문자 구분자 find_first_of 패스트 패스
    {
        sw::string_splitter splitter( "folder/sub\\file.txt", { "/", "\\" } );
        SW_EXPECT_EQUAL( 3u, splitter.getCount() );
        SW_EXPECT_EQUAL( std::string_view( "folder" ), splitter[0] );
        SW_EXPECT_EQUAL( std::string_view( "sub" ), splitter[1] );
        SW_EXPECT_EQUAL( std::string_view( "file.txt" ), splitter[2] );
    }

    // 5) Range-based for 루프 순회 지원
    {
        sw::string_splitter          splitter( "x,y,z", ',' );
        sw::vector<std::string_view> listResult;
        for ( const std::string_view token : splitter )
            listResult.push_back( token );

        SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listResult.size() ) );
        if ( listResult.size() == 3 )
        {
            SW_EXPECT_EQUAL( std::string_view( "x" ), listResult[0] );
            SW_EXPECT_EQUAL( std::string_view( "y" ), listResult[1] );
            SW_EXPECT_EQUAL( std::string_view( "z" ), listResult[2] );
        }
    }

    // 6) 이터레이터 자체의 인덱스(it.getIndex()) 및 오프셋(it.getOffset()) 제어
    {
        sw::string_splitter    splitter( "10;20;30", ';' );
        size_t                 expectedIndex     = 0;
        const std::string_view expectedTokens[]  = { "10", "20", "30" };
        const size_t           expectedOffsets[] = { 0, 3, 6 };

        for ( auto it = splitter.begin(); it != splitter.end(); ++it )
        {
            SW_EXPECT_EQUAL( expectedIndex, it.getIndex() );
            SW_EXPECT_EQUAL( expectedOffsets[expectedIndex], it.getOffset() );
            SW_EXPECT_EQUAL( expectedTokens[expectedIndex], *it );
            ++expectedIndex;
        }
        SW_EXPECT_EQUAL( 3u, expectedIndex );
    }

    // 7) 독립 string_split_iterator 순회 (힙 할당 0회)
    {
        sw::string_split_iterator it( "red,green,blue", ',' );
        sw::string_split_iterator itEnd;

        SW_EXPECT_EQUAL( 0u, it.getIndex() );
        SW_EXPECT_EQUAL( std::string_view( "red" ), *it );
        ++it;
        SW_EXPECT_EQUAL( 1u, it.getIndex() );
        SW_EXPECT_EQUAL( std::string_view( "green" ), *it );
        ++it;
        SW_EXPECT_EQUAL( 2u, it.getIndex() );
        SW_EXPECT_EQUAL( std::string_view( "blue" ), *it );
        ++it;
        SW_EXPECT_TRUE( it == itEnd );
    }

    // 8) 빈 문자열 처리
    {
        sw::string_splitter emptySplitter( "", '/' );
        SW_EXPECT_EQUAL( 0u, emptySplitter.getCount() );
        SW_EXPECT_TRUE( emptySplitter.empty() );
    }
}

/**
 * @brief [Core_String] FixedString 동작
 */
SW_TEST_CASE( Core_String, FixedStringOperations )
{
    sw::fixed_string<64> fs( "Hello" );
    SW_EXPECT_EQUAL( 5u, fs.size() );
    SW_EXPECT_FALSE( fs.empty() );
    SW_EXPECT_EQUAL( sw::string( "Hello" ), sw::string( fs.c_str() ) );

    fs.append( " World" );
    SW_EXPECT_EQUAL( sw::string( "Hello World" ), sw::string( fs.c_str() ) );

    fs.push_back( '!' );
    SW_EXPECT_EQUAL( sw::string( "Hello World!" ), sw::string( fs.c_str() ) );

    sw::fixed_string<64> sub = fs.substr( 0, 5 );
    SW_EXPECT_EQUAL( sw::string( "Hello" ), sw::string( sub.c_str() ) );

    uint32 foundPos = fs.find( "World" );
    SW_EXPECT_EQUAL( 6u, foundPos );

    fs.erase( 5, 6 );
    SW_EXPECT_EQUAL( sw::string( "Hello!" ), sw::string( fs.c_str() ) );
}

/**
 * @brief [Core_String] FixedString 전체 커버리지
 */
SW_TEST_CASE( Core_String, FixedStringFullCoverage )
{
    sw::fixed_string<32> fs( "Engine" );
    SW_EXPECT_EQUAL( 'E', fs.front() );
    SW_EXPECT_EQUAL( 'e', fs.back() );
    SW_EXPECT_EQUAL( 'g', fs[2] );
    SW_EXPECT_EQUAL( 'n', fs.at( 1 ) );

    fs.insert( 0, "Core" );
    SW_EXPECT_EQUAL( sw::string( "CoreEngine" ), sw::string( fs.c_str() ) );

    fs.pop_back();
    SW_EXPECT_EQUAL( sw::string( "CoreEngin" ), sw::string( fs.c_str() ) );

    fs += "e";
    SW_EXPECT_EQUAL( sw::string( "CoreEngine" ), sw::string( fs.c_str() ) );

    sw::fixed_string<32> fs2( "CoreEngine" );
    sw::fixed_string<32> fs3( "OtherEngine" );

    SW_EXPECT_EQUAL( 0, fs.compare( fs2 ) );
    SW_EXPECT_TRUE( fs.compare( fs3 ) != 0 );

    sw::fixed_wstring<32> wfs( L"WideString" );
    SW_EXPECT_EQUAL( 10u, wfs.size() );
    SW_EXPECT_FALSE( wfs.empty() );
}

/**
 * @brief [Core_String] 용량을 넘는 입력은 잘리고 버퍼 밖은 건드리지 않는다
 * @details 예전에는 단정으로 알리기만 하고 **원래 길이 그대로 복사**해서 `_arrData` 뒤(여기서는
 *          `_canary`)를 덮어썼다. 단정은 실행을 멈추지 않고 Shipping 에서는 사라지므로 그대로
 *          스택 오버플로였다. 이 테스트는 잘리는지(size)와 이웃을 안 건드리는지(canary)를 함께 본다.
 */
SW_TEST_CASE( Core_String, FixedStringTruncatesInsteadOfOverflowing )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing fixed_string capacity overflow truncation" );

    /** @brief 문자열 바로 뒤에 감시값을 두어 버퍼 밖 쓰기를 잡는다. */
    struct Guarded
    {
        sw::fixed_string<8> _text;
        uint64              _canary;
    };

    static constexpr uint64 kCanary = 0xA5A5A5A5A5A5A5A5ull;
    const sw::string        longText( 64, 'x' );

    // 1) C 문자열 대입
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = longText.c_str();

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( sw::string( "xxxxxxxx" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 2) std::basic_string 대입
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = longText;

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 3) 뒤에 붙이기 — 남은 자리만큼만 들어간다
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = "abc";
        guarded._text.append( "0123456789" );

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( sw::string( "abc01234" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 4) 꽉 찬 뒤의 push_back 은 버려진다 (예전에는 _arrData[N + 1] 을 썼다)
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = "01234567";
        guarded._text.push_back( '!' );

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( sw::string( "01234567" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 5) 가운데 삽입 — 꼬리는 지키고 삽입분만 자른다
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = "abcd";
        guarded._text.insert( 2, "0123456789" );

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( sw::string( "ab0123cd" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 6) 자기 대입 — 두 경로 모두 자기 버퍼를 자기에게 복사하지 않는다
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = "abcd";

        guarded._text = guarded._text; // 같은 타입 대입: this != &rhs 가드
        SW_EXPECT_EQUAL( sw::string( "abcd" ), sw::string( guarded._text.c_str() ) );

        guarded._text = guarded._text.c_str(); // 포인터 별칭 대입: pStr == _arrData 가드
        SW_EXPECT_EQUAL( sw::string( "abcd" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( 4u, guarded._text.size() );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 7) 문자 채우기 생성자 · 더 큰 용량에서 좁혀 담기
    {
        const sw::fixed_string<8>  filled( 64u, 'y' );
        const sw::fixed_string<64> big( longText.c_str() );
        const sw::fixed_string<8>  narrowed( big );

        SW_EXPECT_EQUAL( 8u, filled.size() );
        SW_EXPECT_EQUAL( sw::string( "yyyyyyyy" ), sw::string( filled.c_str() ) );
        SW_EXPECT_EQUAL( 64u, big.size() );
        SW_EXPECT_EQUAL( 8u, narrowed.size() );
        SW_EXPECT_EQUAL( sw::string( "xxxxxxxx" ), sw::string( narrowed.c_str() ) );
    }
}

/**
 * @brief [Core_String] 포맷 문자열 유틸
 */
SW_TEST_CASE( Core_String, FormatStringUtility )
{
    utf8 buffer[sw::constant::kMaxBuffer256] = {};

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Value: %#", 42 );
    SW_EXPECT_EQUAL( sw::string( "Value: 42" ), sw::string( buffer ) );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Name: %#, Count: %#, Size: %#", "Subsystem", -10, 256u );
    SW_EXPECT_EQUAL( sw::string( "Name: Subsystem, Count: -10, Size: 256" ), sw::string( buffer ) );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Hex: 0x%#, HEX: 0x%#", sw::Fmt( 255, sw::Format().hex() ), sw::Fmt( 255, sw::Format().hexUpper() ) );
    SW_EXPECT_EQUAL( sw::string( "Hex: 0xff, HEX: 0xFF" ), sw::string( buffer ) );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Progress: %#%% complete", 100 );
    SW_EXPECT_EQUAL( sw::string( "Progress: 100% complete" ), sw::string( buffer ) );
}

// ------------------------------------------------------------------------------
// 2) StringBuilder — append/format·용량 성장
// ------------------------------------------------------------------------------
/**
 * @brief [Core_String] StringBuilder append/format
 */
SW_TEST_CASE( Core_String, StringBuilderAppendAndFormat )
{
    sw::StringBuilder<64> sb;
    sb.append( "Hello" );
    sb.append( ' ' );
    sb.append( 42 );
    SW_EXPECT_STREQ( "Hello 42", sb.c_str() );
    SW_EXPECT_EQUAL( 8u, sb.size() );

    sb.appendFormat( " x=%#", 7 );
    SW_EXPECT_STREQ( "Hello 42 x=7", sb.c_str() );

    sb.clear();
    SW_EXPECT_EQUAL( 0u, sb.size() );
    SW_EXPECT_STREQ( "", sb.c_str() );
    SW_EXPECT_EMPTY( sb.view() );
}

/**
 * @brief [Core_String] StringBuilder 정적 용량 초과 성장
 */
SW_TEST_CASE( Core_String, StringBuilderGrowsBeyondStaticCapacity )
{
    sw::StringBuilder<8> sb;
    const uint32         initialCapacity = sb.capacity();
    SW_EXPECT_EQUAL( 8u, initialCapacity );

    sb.append( "0123456789ABCDEF" );
    SW_EXPECT_TRUE_MSG( sb.capacity() > initialCapacity, "StringBuilder should grow when content exceeds static capacity" );
    SW_EXPECT_STREQ( "0123456789ABCDEF", sb.c_str() );
    SW_EXPECT_EQUAL( 16u, sb.size() );

    // 이동 생성자 검증 (힙 버퍼 이동)
    sw::StringBuilder<8> movedSb{ std::move( sb ) };
    SW_EXPECT_STREQ( "0123456789ABCDEF", movedSb.c_str() );
    SW_EXPECT_EQUAL( 16u, movedSb.size() );
}

/**
 * @brief [Core_String] fixed_string string_view 및 hash 지원 검증
 */
SW_TEST_CASE( Core_String, FixedStringModernFeatures )
{
    std::string_view     sv = "ModernCpp";
    sw::fixed_string<32> fs{ sv };
    SW_EXPECT_STREQ( "ModernCpp", fs.c_str() );
    SW_EXPECT_EQUAL( 9u, fs.size() );
    SW_EXPECT_EQUAL( sv, fs.view() );

    SW_EXPECT_TRUE( fs.equals( "MODERNCPP", true ) );
    SW_EXPECT_FALSE( fs.equals( "MODERNCPP", false ) );
    SW_EXPECT_TRUE( fs.equals( sw::fixed_string<32>( "moderncpp" ), true ) );
    SW_EXPECT_FALSE( fs.equals( "Other", true ) );

    std::hash<sw::fixed_string<32>> hasher;
    size_t                          h1 = hasher( fs );
    size_t                          h2 = hasher( sw::fixed_string<32>( "ModernCpp" ) );
    SW_EXPECT_EQUAL( h1, h2 );
}

/**
 * @brief [Core_String] UTF-8 및 UTF-16 유니코드 상호 인코딩/디코딩 라운드트립 검증
 */
SW_TEST_CASE( Core_String, UnicodeConversionRoundTrip )
{
    // 한글, 이모지, 특수문자
    const utf8*       kOriginalUtf8 = "안녕하세요 Engine 🚀 (SW_Engine)";
    const sw::wstring utf16Str      = sw::StringUtil::utf8ToUtf16( kOriginalUtf8 );
    SW_EXPECT_FALSE( utf16Str.empty() );

    const sw::string roundTripUtf8 = sw::StringUtil::utf16ToUtf8( utf16Str.c_str() );
    SW_EXPECT_STREQ( kOriginalUtf8, roundTripUtf8.c_str() );
}

/**
 * @brief [Core_String] StringBuilder 복합 다중 appendFormat 및 무할당 성능 검증
 */
SW_TEST_CASE( Core_String, StringBuilderComplexFormatting )
{
    sw::StringBuilder<256> sb;
    sb.append( "Entity[" ).append( 42 ).append( "]: pos=(" );
    sb.append( 12.5f ).append( ", " ).append( -34.75f ).append( ")" );
    sb.append( "\n" );

    SW_EXPECT_STREQ( "Entity[42]: pos=(12.5, -34.75)\n", sb.c_str() );
}

/**
 * @brief [Core_String] StringUtil::isValidUTF8 종합 유효성 및 경계/오류 시퀀스 검증
 */
SW_TEST_CASE( Core_String, StringUtilUtf8Validation )
{
    // 1) Null 및 빈 문자열
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( nullptr ) );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "" ) );

    // 2) 순수 ASCII (8바이트 미만 및 8바이트 이상 SWAR 경로)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "A" ) );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "Short" ) );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "Exactly8" ) );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "This is a int32 ASCII sentence for SWAR fast path testing." ) );

    // 3) 유효한 2바이트, 3바이트, 4바이트 UTF-8
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xC2\xA9" ) );               // © (U+00A9)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xC3\xA9" ) );               // é (U+00E9)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xE2\x82\xAC" ) );           // € (U+20AC)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "안녕하세요 엔진 테스트" ) ); // 한글 3바이트
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xF0\x9F\x9A\x80" ) );       // 🚀 (U+1F680)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xF0\x9F\x98\x80" ) );       // 😀 (U+1F600)

    // 4) 불완전/잘린 시퀀스 (Truncated sequences)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xC2" ) );         // 2바이트 리드 바이트만 존재
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xE2\x82" ) );     // 3바이트 중 2바이트만 존재
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xF0\x9F\x9A" ) ); // 4바이트 중 3바이트만 존재

    // 5) 비정상 후속 바이트 (Invalid continuation bytes)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xC2\x20" ) );         // 후속 바이트가 공백 (0x20 != 0x80..0xBF)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xE2\x82\x20" ) );     // 3번째 바이트 비정상
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xF0\x9F\x9A\xC0" ) ); // 4번째 바이트 비정상

    // 6) Overlong 인코딩 (보안 취약점 방지 검증)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xC0\xAF" ) );         // Overlong 2바이트 '/'
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xC1\xBF" ) );         // Overlong 2바이트
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xE0\x80\xAF" ) );     // Overlong 3바이트
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xF0\x80\x80\xAF" ) ); // Overlong 4바이트

    // 7) UTF-16 Surrogate 영역 (U+D800 ~ U+DFFF 금지)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xED\xA0\x80" ) ); // U+D800
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xED\xBF\xBF" ) ); // U+DFFF

    // 8) 최대 유니코드 초과 (> U+10FFFF)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xF4\x90\x80\x80" ) ); // U+110000
    SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xF7\xBF\xBF\xBF" ) ); // 유효 범위 초과
}

/**
 * @brief [Core_String] StringUtil 해시 일관성, CRC32 및 공백 트림 유틸리티 검증
 */
SW_TEST_CASE( Core_String, StringUtilHashingAndTransform )
{
    // 64비트 / 32비트 FNV1a 해시 일관성
    const uint64 h64_1 = sw::StringUtil::computeHash64( std::string_view( "EngineResourcePath" ) );
    const uint64 h64_2 = sw::StringUtil::computeHash64( std::string_view( "EngineResourcePath" ) );
    const uint64 h64_3 = sw::StringUtil::computeHash64( std::string_view( "engineResourcePath" ), false );

    SW_EXPECT_EQUAL( h64_1, h64_2 );
    SW_EXPECT_TRUE( h64_1 != h64_3 );

    const uint32 h32_1 = sw::StringUtil::computeHash32( std::string_view( "EngineResourcePath" ) );
    const uint32 h32_2 = sw::StringUtil::computeHash32( std::string_view( "EngineResourcePath" ) );
    SW_EXPECT_EQUAL( h32_1, h32_2 );

    // CRC32 체크섬 계산 검증
    const uint32 crc1 = sw::StringUtil::computeCrc32( "123456789", 9 );
    const uint32 crc2 = sw::StringUtil::computeCrc32( "123456789", 9 );
    SW_EXPECT_EQUAL( 0xCBF43926u, crc1 ); // 표준 IEEE 802.3 CRC32("123456789") = 0xCBF43926
    SW_EXPECT_EQUAL( crc1, crc2 );

    // trimStart & trimEnd
    SW_EXPECT_EQUAL( sw::string( "Hello  " ), sw::StringUtil::trimStart( "  Hello  " ) );
    SW_EXPECT_EQUAL( sw::string( "  Hello" ), sw::StringUtil::trimEnd( "  Hello  " ) );
}

/**
 * @brief [Core_String] FixedString 추가 고급 연산 (반복자, 비우기, 검색)
 */
SW_TEST_CASE( Core_String, FixedStringExtendedOperations )
{
    sw::fixed_string<32> str( "Antigravity" );

    // 범위 기반 for 루프 반복자 순회 검증
    size_t charCount{ 0 };
    for ( const utf8 ch : str )
    {
        if ( ch != '\0' )
            ++charCount;
    }
    SW_EXPECT_EQUAL( 11u, charCount );

    // find 및 substr
    SW_EXPECT_EQUAL( 0u, str.find( "Anti" ) );
    SW_EXPECT_EQUAL( 4u, str.find( "grav" ) );
    SW_EXPECT_TRUE( str.substr( 0, 4 ) == "Anti" );
    SW_EXPECT_TRUE( str.substr( 4 ) == "gravity" );
    SW_EXPECT_EQUAL( sw::fixed_string<32>::npos, str.find( "NotFound" ) );

    // clear
    str.clear();
    SW_EXPECT_TRUE( str.empty() );
    SW_EXPECT_EQUAL( 0u, str.size() );
    SW_EXPECT_STREQ( "", str.c_str() );
}

/**
 * @brief [Core_String] fixed_string formatstring 및 data() 수정 후 자동 sync_size 검증
 */
SW_TEST_CASE( Core_String, FixedStringFormatAndAutoSync )
{
    // 1) formatstring 동작 및 자동 길이 동기화
    sw::fixed_string<sw::constant::kMaxBuffer64> strFmt;
    sw::formatstring( strFmt.data(), strFmt.capacity(), "Item #%#: %# (%#)", 42, "Potion", sw::Fmt( 12.5, sw::Format( 2 ) ) );
    SW_EXPECT_FALSE( strFmt.empty() );
    SW_EXPECT_STREQ( "Item #42: Potion (12.50)", strFmt.c_str() );
    SW_EXPECT_EQUAL( static_cast<uint32>( strlen( "Item #42: Potion (12.50)" ) ), strFmt.size() );
    SW_EXPECT_EQUAL( strFmt.size(), strFmt.length() );
    SW_EXPECT_EQUAL( std::string_view( "Item #42: Potion (12.50)" ), strFmt.view() );

    // 2) data() 버퍼에 직접 C-API 스타일로 작성했을 때 자동 sync_size 동작
    sw::fixed_string<sw::constant::kMaxBuffer32> rawBuf;
    SW_EXPECT_TRUE( rawBuf.empty() );
    SW_EXPECT_EQUAL( 0u, rawBuf.size() );

    // data()에 strcpy (ImGui::InputText 동작 모사)
    sw::StringUtil::strncpy( rawBuf.data(), "HeroPlayer", rawBuf.capacity() );

    // sync_size()를 명시적으로 호출하지 않아도 empty(), size(), length(), view(), basic_string 변환 자동 동기화
    SW_EXPECT_FALSE( rawBuf.empty() );
    SW_EXPECT_EQUAL( 10u, rawBuf.size() );
    SW_EXPECT_EQUAL( 10u, rawBuf.length() );
    SW_EXPECT_STREQ( "HeroPlayer", rawBuf.c_str() );
    SW_EXPECT_EQUAL( std::string_view( "HeroPlayer" ), rawBuf.view() );

    const std::string stdStr = rawBuf;
    SW_EXPECT_EQUAL( std::string( "HeroPlayer" ), stdStr );
    const sw::string swStr{ rawBuf.c_str() };
    SW_EXPECT_EQUAL( sw::string( "HeroPlayer" ), swStr );

    // Range-based for loop 자동 동기화
    size_t iteratedCount = 0;
    for ( const utf8 ch : rawBuf )
    {
        if ( ch != '\0' )
            ++iteratedCount;
    }
    SW_EXPECT_EQUAL( 10u, iteratedCount );
}

/**
 * @brief [Core_String] FileUtil::skipUtf8Bom 및 BOM 포함 텍스트 파일 읽기 검증
 */
SW_TEST_CASE( Core_String, Utf8BomHandling )
{
    // 1) string_view 기반 BOM 스킵 검증
    const utf8* pWithBom    = "\xEF\xBB\xBFHello UTF-8 BOM!";
    const utf8* pWithoutBom = "Hello Without BOM!";

    SW_EXPECT_EQUAL( std::string_view( "Hello UTF-8 BOM!" ), sw::FileUtil::skipUtf8Bom( pWithBom ) );
    SW_EXPECT_EQUAL( std::string_view( pWithoutBom ), sw::FileUtil::skipUtf8Bom( pWithoutBom ) );

    // 2) 포인터 및 크기 기반 BOM 스킵 검증
    const uint8* pBytes = reinterpret_cast<const uint8*>( pWithBom );
    size_t       size   = strlen( pWithBom );
    sw::FileUtil::skipUtf8Bom( pBytes, size );
    SW_EXPECT_EQUAL( strlen( "Hello UTF-8 BOM!" ), size );
    SW_EXPECT_EQUAL( 'H', static_cast<utf8>( pBytes[0] ) );

    // 3) 파일 I/O 자동 BOM 제거 검증
    const sw::string tempDir     = sw::FileUtil::getTempDirectory();
    const sw::string bomFilePath = sw::FileUtil::joinPath( tempDir, "test_bom.txt" );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( bomFilePath, reinterpret_cast<const uint8*>( pWithBom ), strlen( pWithBom ) ) );

    sw::string readText;
    SW_EXPECT_TRUE( sw::FileUtil::readTextFile( bomFilePath, readText ) );
    SW_EXPECT_STREQ( "Hello UTF-8 BOM!", readText.c_str() );

    sw::FileUtil::removeFile( bomFilePath );
}

/**
 * @brief [Core_String] FixedString 다양한 생성자, 대입 및 assign 동작 검증
 */
SW_TEST_CASE( Core_String, FixedStringConstructorsAndAssignments )
{
    // 1) 기본 생성자
    sw::fixed_string<32> defaultStr;
    SW_EXPECT_TRUE( defaultStr.empty() );
    SW_EXPECT_EQUAL( 0u, defaultStr.size() );
    SW_EXPECT_EQUAL( 0u, defaultStr.length() );
    SW_EXPECT_EQUAL( 32u, defaultStr.capacity() );
    SW_EXPECT_EQUAL( 32u, defaultStr.max_size() );
    SW_EXPECT_STREQ( "", defaultStr.c_str() );

    // 2) nullptr 생성자 (안전하게 빈 문자열로 초기화)
    sw::fixed_string<32> nullStr( static_cast<const utf8*>( nullptr ) );
    SW_EXPECT_TRUE( nullStr.empty() );
    SW_EXPECT_EQUAL( 0u, nullStr.size() );

    // 3) 채우기(fill) 생성자
    sw::fixed_string<32> fillStr( 5u, 'X' );
    SW_EXPECT_FALSE( fillStr.empty() );
    SW_EXPECT_EQUAL( 5u, fillStr.size() );
    SW_EXPECT_STREQ( "XXXXX", fillStr.c_str() );

    // 4) std::string 및 std::string_view 생성자
    const std::string      stdStrSource = "FromStdString";
    const std::string_view svSource     = "FromView";
    sw::fixed_string<32>   fromStd( stdStrSource );
    sw::fixed_string<32>   fromSv( svSource );
    SW_EXPECT_STREQ( "FromStdString", fromStd.c_str() );
    SW_EXPECT_STREQ( "FromView", fromSv.c_str() );

    // 5) 복사 생성자 및 이동 생성자
    sw::fixed_string<32> copyStr( fromStd );
    SW_EXPECT_STREQ( "FromStdString", copyStr.c_str() );
    sw::fixed_string<32> moveStr( std::move( copyStr ) );
    SW_EXPECT_STREQ( "FromStdString", moveStr.c_str() );

    // 6) 대입 연산자들 (const char*, nullptr, std::string, std::string_view, fixed_string)
    sw::fixed_string<32> assignTarget;
    assignTarget = "AssignedCStr";
    SW_EXPECT_STREQ( "AssignedCStr", assignTarget.c_str() );

    assignTarget = static_cast<const utf8*>( nullptr );
    SW_EXPECT_TRUE( assignTarget.empty() );
    SW_EXPECT_STREQ( "", assignTarget.c_str() );

    assignTarget = stdStrSource;
    SW_EXPECT_STREQ( "FromStdString", assignTarget.c_str() );

    assignTarget = svSource;
    SW_EXPECT_STREQ( "FromView", assignTarget.c_str() );

    assignTarget = fillStr;
    SW_EXPECT_STREQ( "XXXXX", assignTarget.c_str() );

    // 7) assign() 멤버 함수들
    assignTarget.assign( "NewAssign" );
    SW_EXPECT_STREQ( "NewAssign", assignTarget.c_str() );
    assignTarget.assign( fromStd );
    SW_EXPECT_STREQ( "FromStdString", assignTarget.c_str() );
}

/**
 * @brief [Core_String] FixedString 비교 및 연산자 (==, !=, <, <=, >, >=, +, +=, <<, >>)
 */
SW_TEST_CASE( Core_String, FixedStringComparisonAndOperators )
{
    sw::fixed_string<32> strA( "Alpha" );
    sw::fixed_string<32> strA2( "Alpha" );
    sw::fixed_string<32> strB( "Beta" );

    // 비교 연산자 (fixed_string vs fixed_string)
    SW_EXPECT_TRUE( strA == strA2 );
    SW_EXPECT_FALSE( strA != strA2 );
    SW_EXPECT_TRUE( strA != strB );
    SW_EXPECT_TRUE( strA < strB );
    SW_EXPECT_TRUE( strA <= strB );
    SW_EXPECT_TRUE( strA <= strA2 );
    SW_EXPECT_TRUE( strB > strA );
    SW_EXPECT_TRUE( strB >= strA );
    SW_EXPECT_TRUE( strA2 >= strA );

    // 비교 연산자 (fixed_string vs const char*)
    SW_EXPECT_TRUE( strA == "Alpha" );
    SW_EXPECT_FALSE( strA == "Beta" );
    SW_EXPECT_TRUE( strA != "Beta" );
    SW_EXPECT_FALSE( strA != "Alpha" );

    // operator+ 및 operator+=
    sw::fixed_string<64> sum1 = strA + strB;
    SW_EXPECT_STREQ( "AlphaBeta", sum1.c_str() );

    sw::fixed_string<64> sum2 = strA + "Gamma";
    SW_EXPECT_STREQ( "AlphaGamma", sum2.c_str() );

    sw::fixed_string<64> sum3 = "Prefix" + strA;
    SW_EXPECT_STREQ( "PrefixAlpha", sum3.c_str() );

    sw::fixed_string<64> mutStr( "Base" );
    mutStr += "_";
    mutStr += strA;
    mutStr += '!';
    SW_EXPECT_STREQ( "Base_Alpha!", mutStr.c_str() );

    // stream << 및 >> 연산자
    std::ostringstream outStream;
    outStream << strA;
    SW_EXPECT_EQUAL( std::string( "Alpha" ), outStream.str() );

    std::istringstream   inStream( "StreamedContent" );
    sw::fixed_string<32> streamTarget;
    inStream >> streamTarget;
    SW_EXPECT_STREQ( "StreamedContent", streamTarget.c_str() );
}

/**
 * @brief [Core_String] FixedString 최대 용량(N) 경계 조건 및 널 종단 무결성
 */
SW_TEST_CASE( Core_String, FixedStringBoundaryAndMaxCapacity )
{
    // 정확히 용량 16 문자를 채웠을 때 검증
    sw::fixed_string<16> maxStr( "0123456789ABCDEF" );
    SW_EXPECT_EQUAL( 16u, maxStr.size() );
    SW_EXPECT_EQUAL( 16u, maxStr.capacity() );
    SW_EXPECT_EQUAL( 16u, maxStr.max_size() );
    SW_EXPECT_STREQ( "0123456789ABCDEF", maxStr.c_str() );
    SW_EXPECT_EQUAL( '\0', maxStr.data()[16] ); // 16번 인덱스는 항상 널 종단

    // formatstring으로 용량(16) 내 작성 시 안전한 널 종단(15자 + '\0') 보장
    sw::fixed_string<16> fmtMax;
    sw::formatstring( fmtMax.data(), fmtMax.capacity(), "%#", "0123456789ABCDEF" );
    SW_EXPECT_EQUAL( 15u, fmtMax.size() );
    SW_EXPECT_STREQ( "0123456789ABCDE", fmtMax.c_str() );
    SW_EXPECT_EQUAL( '\0', fmtMax.data()[15] );
}

/**
 * @brief [Core_String] FixedString data() 버퍼 직접 변경 후 컨테이너 연산(insert, erase, append 등) 통합 검증
 */
SW_TEST_CASE( Core_String, FixedStringDirectMutationAndContainerOps )
{
    sw::fixed_string<64> str;
    // 1) data()로 직접 기록
    sw::StringUtil::strncpy( str.data(), "Player", str.capacity() );
    SW_EXPECT_EQUAL( 6u, str.size() );
    SW_EXPECT_STREQ( "Player", str.c_str() );

    // 2) data() 수정 후 push_back
    str.push_back( '1' );
    SW_EXPECT_EQUAL( 7u, str.size() );
    SW_EXPECT_STREQ( "Player1", str.c_str() );

    // 3) data() 수정 후 append
    str.append( "_Knight" );
    SW_EXPECT_EQUAL( 14u, str.size() );
    SW_EXPECT_STREQ( "Player1_Knight", str.c_str() );

    // 4) data() 수정 후 insert
    str.insert( 0, "Hero_" );
    SW_EXPECT_EQUAL( 19u, str.size() );
    SW_EXPECT_STREQ( "Hero_Player1_Knight", str.c_str() );

    // 5) data() 수정 후 erase
    str.erase( 0, 5 ); // "Hero_" 제거
    SW_EXPECT_EQUAL( 14u, str.size() );
    SW_EXPECT_STREQ( "Player1_Knight", str.c_str() );

    // 6) data() 수정 후 find 및 substr
    SW_EXPECT_EQUAL( 8u, str.find( "Knight" ) );
    sw::fixed_string<64> sub = str.substr( 8, 6 );
    SW_EXPECT_STREQ( "Knight", sub.c_str() );

    // 7) data() 수정 후 pop_back
    str.pop_back(); // 't' 제거
    SW_EXPECT_EQUAL( 13u, str.size() );
    SW_EXPECT_STREQ( "Player1_Knigh", str.c_str() );
}

/**
 * @brief [Core_String] fixed_wstring (UTF-16) 광범위 동작 검증
 */
SW_TEST_CASE( Core_String, FixedWStringOperations )
{
    sw::fixed_wstring<32> wstr( L"UnicodeString" );
    SW_EXPECT_FALSE( wstr.empty() );
    SW_EXPECT_EQUAL( 13u, wstr.size() );
    SW_EXPECT_EQUAL( 32u, wstr.capacity() );
    SW_EXPECT_TRUE( wstr.view() == std::wstring_view( L"UnicodeString" ) );

    wstr.append( L"_W" );
    SW_EXPECT_EQUAL( 15u, wstr.size() );

    wstr.push_back( L'!' );
    SW_EXPECT_EQUAL( 16u, wstr.size() );

    wstr.insert( 0, L"Pre_" );
    SW_EXPECT_EQUAL( 20u, wstr.size() );

    wstr.erase( 0, 4 );
    SW_EXPECT_EQUAL( 16u, wstr.size() );

    SW_EXPECT_EQUAL( 0u, wstr.find( L"Unicode" ) );
    sw::fixed_wstring<32> sub = wstr.substr( 0, 7 );
    SW_EXPECT_EQUAL( 7u, sub.size() );
    SW_EXPECT_TRUE( sub == sw::fixed_wstring<32>( L"Unicode" ) );

    // std::hash 특수화 검증
    std::hash<sw::fixed_wstring<32>> hasher;
    size_t                           h1 = hasher( wstr );
    size_t                           h2 = hasher( sw::fixed_wstring<32>( wstr.c_str() ) );
    SW_EXPECT_EQUAL( h1, h2 );

    wstr.clear();
    SW_EXPECT_TRUE( wstr.empty() );
    SW_EXPECT_EQUAL( 0u, wstr.size() );
}

/**
 * @brief [Core_String] FixedString의 std::unordered_map 및 std::unordered_set 연동 검증
 */
SW_TEST_CASE( Core_String, FixedStringUnorderedContainers )
{
    // 1) std::unordered_set
    std::unordered_set<sw::fixed_string<32>> uniqueSet;
    uniqueSet.insert( sw::fixed_string<32>( "Entity_A" ) );
    uniqueSet.insert( sw::fixed_string<32>( "Entity_B" ) );
    uniqueSet.insert( sw::fixed_string<32>( "Entity_A" ) ); // 중복

    SW_EXPECT_EQUAL( 2u, uniqueSet.size() );
    SW_EXPECT_TRUE( uniqueSet.find( sw::fixed_string<32>( "Entity_A" ) ) != uniqueSet.end() );
    SW_EXPECT_TRUE( uniqueSet.find( sw::fixed_string<32>( "Entity_C" ) ) == uniqueSet.end() );

    // 2) std::unordered_map
    std::unordered_map<sw::fixed_string<32>, int32> mapScore;
    mapScore[sw::fixed_string<32>( "Player1" )] = 100;
    mapScore[sw::fixed_string<32>( "Player2" )] = 250;

    SW_EXPECT_EQUAL( 2u, mapScore.size() );
    SW_EXPECT_EQUAL( 100, mapScore[sw::fixed_string<32>( "Player1" )] );
    SW_EXPECT_EQUAL( 250, mapScore[sw::fixed_string<32>( "Player2" )] );
}

/**
 * @brief [Core_String] FormatString floatToString 및 폴백 널 종단 문자 보장 검증
 */
SW_TEST_CASE( Core_String, FormatStringFloatFallbackNullTerminator )
{
    utf8 buf[sw::constant::kMaxBuffer64]{ 0 };
    sw::formatstring( buf, sizeof( buf ), "Value: %#", 123.456f );
    sw::string formattedFloat( buf );
    SW_EXPECT_FALSE( formattedFloat.empty() );
    SW_EXPECT_TRUE( formattedFloat.find( "123.45" ) != sw::string::npos );
    SW_EXPECT_EQUAL( '\0', buf[formattedFloat.size()] );
}

/**
 * @brief [Core_String] StringBuilder 경계 크기 appendFormat 포맷팅 및 재할당 안전성 검증
 */
SW_TEST_CASE( Core_String, StringBuilderBoundaryAvailableMinusOne )
{
    sw::StringBuilder<256> builder;
    builder.append( "1234567890" );
    builder.appendFormat( "_%#_%#", 100, 200 );
    SW_EXPECT_EQUAL( sw::string( "1234567890_100_200" ), sw::string( builder.c_str() ) );
}

/**
 * @brief [Core_String] basic_fixed_string C 문자열 좌측 덧셈 연산자 및 O(1) size() 일관성 검증
 */
SW_TEST_CASE( Core_String, FixedStringOperatorPlusWithCStringLhsAndSizeO1 )
{
    sw::fixed_string<32> rhs( "World" );
    SW_EXPECT_EQUAL( 5u, rhs.size() );

    // C 문자열 좌측 덧셈: "Hello " + rhs
    auto combined = "Hello " + rhs;
    SW_EXPECT_EQUAL( sw::string( "Hello World" ), sw::string( combined.c_str() ) );
    SW_EXPECT_EQUAL( 11u, combined.size() );
}

/**
 * @brief [Core_String] 표준 서식 지정자(정밀도·너비·플래그)를 formatstring 이 이해하는지
 * @details 예전에는 `%#` 과 맨 변환 문자(`%d`, `%f`)만 알아봤다. 그래서 `%.3f` 를 쓰면 `%.` 까지만
 *          플레이스홀더로 먹고 `3f` 가 글자로 남아 **조용히 틀린 출력**이 나왔다. 로그에서 소수점
 *          자릿수를 맞추려면 Fmt(v, Format().precision(3)) 를 써야 했는데, 그게 불편해서 서식을 넓혔다.
 */
SW_TEST_CASE( Core_String, FormatStringSupportsPrintfSpecifiers )
{
    utf8 buffer[128]{};

    // 정밀도 — 이게 예전에 깨지던 자리다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%.3f", 3.14159f );
    SW_EXPECT_STREQ( "3.142", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%.0f", 2.7f );
    SW_EXPECT_STREQ( "3", buffer );

    // 너비 — 오른쪽 정렬이 기본.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%5d]", 42 );
    SW_EXPECT_STREQ( "[   42]", buffer );

    // 왼쪽 정렬 플래그.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%-5d]", 42 );
    SW_EXPECT_STREQ( "[42   ]", buffer );

    // 0 채우기.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%05d]", 42 );
    SW_EXPECT_STREQ( "[00042]", buffer );

    // 부호 표시.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%+d", 42 );
    SW_EXPECT_STREQ( "+42", buffer );

    // 너비 + 정밀도 조합.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%8.2f]", 3.14159f );
    SW_EXPECT_STREQ( "[    3.14]", buffer );

    // 길이 수식어는 읽고 버린다 — 값의 타입은 인자가 정한다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%llu", static_cast<uint64>( 1234567890123ull ) );
    SW_EXPECT_STREQ( "1234567890123", buffer );

    // 16진수는 서식 앞에 옵션이 붙어도 유지된다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%08x", 255u );
    SW_EXPECT_STREQ( "000000ff", buffer );

    // 기존 %# 은 그대로 동작해야 한다 (하위호환).
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Value: %#", 42 );
    SW_EXPECT_STREQ( "Value: 42", buffer );

    // %% 는 리터럴 퍼센트.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%d%% done", 100 );
    SW_EXPECT_STREQ( "100% done", buffer );
}

/**
 * @brief [Core_String] 서식이 붙은 긴 문자열이 임시 버퍼 크기에서 잘리지 않는지
 * @details 값은 256바이트 스택 버퍼(kTempBufferSize)를 거쳐 문자열이 된다. 문자열 인자는 그 버퍼를
 *          건너뛰는 지름길이 있었는데 **서식 없는 경로에만** 있었다. 그래서 `%s` 는 멀쩡한데
 *          `%-20s` 처럼 폭을 주는 순간 256자에서 잘렸다 — 로그에서 긴 메시지의 꼬리가 사라지는,
 *          예전에 Vulkan 검증 메시지로 한 번 겪은 것과 같은 종류의 조용한 손실이다.
 */
SW_TEST_CASE( Core_String, FormatStringLongTextSurvivesWidthSpec )
{
    // 임시 버퍼(256)보다 확실히 긴 문자열.
    sw::string longText;
    longText.reserve( 600 );
    for ( uint32 index = 0; index < 60; ++index )
        longText += "0123456789";

    utf8 buffer[1024]{};

    // 폭 지정이 붙어도 전체가 나와야 한다 (폭보다 길면 패딩은 없다).
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%-20s", longText.c_str() );
    SW_EXPECT_EQUAL( longText.size(), sw::StringUtil::strlen( buffer ) );
    SW_EXPECT_STREQ( longText.c_str(), buffer );

    // 서식 없는 경로도 그대로여야 한다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#", longText.c_str() );
    SW_EXPECT_EQUAL( longText.size(), sw::StringUtil::strlen( buffer ) );

    // 짧은 문자열은 폭 맞춤이 실제로 적용된다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%-6s]", "ab" );
    SW_EXPECT_STREQ( "[ab    ]", buffer );
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%6s]", "ab" );
    SW_EXPECT_STREQ( "[    ab]", buffer );
}

/**
 * @brief [Core_String] `%#` 은 옵션이 붙지 않는 순수 자리표다 — 뒤 글자는 무조건 리터럴, 서식은 printf 형으로.
 * @details `#` 뒤를 서식으로 읽던 시절엔 `%#x%#`(가로x세로)가 가로를 16진수로 찍고(1280 → 500, 16곳),
 *          `%#s`(초) 가 단위 `s` 를 삼키고(5곳), `%#.txt` 가 `.tx` 를 잃어 로그 파일이 `.txt` 없이 남았다.
 *          세 번 다 사고였고 그 문법을 쓰려던 사람은 없었다. 여기서 새 규약을 고정한다.
 */
SW_TEST_CASE( Core_String, PlaceholderNeverTakesSpecifiers )
{
    utf8 buffer[128]{};

    // `%#` 뒤의 'x' 는 리터럴이다 — 치수 로그의 "가로x세로".
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#x%#", 1280, 720 );
    SW_EXPECT_STREQ( "1280x720", buffer );

    // 단위를 붙여 쓴 로그 — 'd', 's', 't' 가 서식으로 읽히지 않는다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#dB %#s %#time", 3, 2.5f, 7 );
    SW_EXPECT_STREQ( "3dB 2.500000s 7time", buffer );

    // 서식은 printf 형으로 — 너비·정렬·0채움·16진수·정밀도·부호. 타입은 인자가 정한다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%3d|%-5s|%08x|%.2f|%+d]", 7, "w", 255, 3.14159, 5 );
    SW_EXPECT_STREQ( "[  7|w    |000000ff|3.14|+5]", buffer );

    // 공백은 플래그로 받지 않는다 — 뒤 단어의 첫 글자를 먹지 않아야 한다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%# x %#", 1280, 720 );
    SW_EXPECT_STREQ( "1280 x 720", buffer );

    // 알아볼 수 없는 `%…` 는 리터럴이고 인자를 소비하지 않는다 — 뒤 인자가 밀리지 않아야 한다. `%%` 는 퍼센트.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "100% done, %q=%#, %%=%#, tail%", 1, 2 );
    SW_EXPECT_STREQ( "100% done, %q=1, %=2, tail%", buffer );

    // 인자 수 불일치는 Debug 에서 포맷을 훑는 자리의 단언(디버그 브레이크)이 잡는다 — 그래서 여기서 재현할 수 없다.
    // 세는 규칙만 고정한다: `%#`·printf 서식은 1, `%%`·모르는 `%…` 는 0. 리터럴은 컴파일 시점에도 셀 수 있다.
    static_assert( sw::FormatString::countPlaceholders( "a=%# b=%3d c=%.2f %% %q tail%" ) == 3, "%#, %3d, %.2f 만 자리표다" );
    static_assert( sw::FormatString::countPlaceholders( "no placeholders" ) == 0 );
    static_assert( sw::FormatString::countPlaceholders( "%#x%# %#s %#.txt" ) == 4 );
    SW_EXPECT_EQUAL( 2u, sw::FormatString::countPlaceholders( sw::string( "%#-%#" ) ) );

    // 널 포인터는 종류와 무관하게 (null) — `nullptr` 리터럴과 널 `const utf8*` 는 예전엔 string_view 지름길에서
    // strlen(nullptr) 로 죽었다(이 테스트가 처음 SEGFAULT 로 잡았다).
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%# %# %# %-8s|", nullptr, static_cast<const void*>( nullptr ),
                      static_cast<const utf8*>( nullptr ), static_cast<const utf8*>( nullptr ) );
    SW_EXPECT_STREQ( "(null) (null) (null) (null)  |", buffer );

    // printf 서식 + Fmt 값 — 변환은 Fmt 의 서식(16진수), 너비는 서식 문자열. 예전엔 "[unsupported type]" 이었다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%6d]", sw::Fmt( 255, sw::Format().hex() ) );
    SW_EXPECT_STREQ( "[    ff]", buffer );

    // 실제로 깨져 있던 문장들 — 첫 글자가 변환 문자인 단어가 뒤에 온다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%# Passed, %# Failed", 3, 0 );
    SW_EXPECT_STREQ( "3 Passed, 0 Failed", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%# of %# slots", 5, 8 );
    SW_EXPECT_STREQ( "5 of 8 slots", buffer );

    // 변환 문자도 플래그도 아닌 구분자는 리터럴로 남는다 — 치수 로그는 이 형태를 쓴다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#Ã%#", 1280, 720 );
    SW_EXPECT_STREQ( "1280Ã720", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "(%#, %#)", 1280, 720 );
    SW_EXPECT_STREQ( "(1280, 720)", buffer );

    // 의도된 16진수는 printf 형으로 쓴다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "crc=%x", 255 );
    SW_EXPECT_STREQ( "crc=ff", buffer );
}

/**
 * @brief [Core_String] stristr 은 널 종단자를 지나 읽지 않는다
 * @details `stristr` 은 `string_view( pStr, subLen )` 를 만들어 비교한다 — 남은 문자열이 검색어보다
 *          짧아도 길이를 `subLen` 으로 잡으므로, 읽어 보면 종단자 뒤로 넘어갈 것처럼 생겼다.
 *          **실제로는 넘어가지 않는다.** `equals` 의 비교 루프가 첫 불일치에서 끊기고, 널 종단자는
 *          검색어의 어떤 문자와도(검색어에는 널이 없다) 반드시 불일치하기 때문이다. 즉 안전한
 *          이유가 구현 세부(단축 평가)에 걸려 있다.
 *
 *          그 세부가 깨지면 바로 경계 초과 읽기가 된다 — 예컨대 비교를 한 번에 여러 바이트씩
 *          처리하도록 "최적화" 하는 순간이다. 그래서 가드 페이지로 고정한다: 문자열을 페이지
 *          마지막 바이트에 붙여 놓고 다음 페이지를 접근 불가로 만들면, 한 바이트라도 넘어가는
 *          순간 죽는다. 에디터 검색 필드가 이 경로를 매 프레임 탄다(필드보다 긴 검색어).
 */
SW_TEST_CASE( Core_String, StristrStopsAtTerminator )
{
#if defined( SW_PLATFORM_WINDOWS )
    SYSTEM_INFO sysInfo{};
    GetSystemInfo( &sysInfo );
    const size_t pageSize = static_cast<size_t>( sysInfo.dwPageSize );

    // 두 페이지를 잡고 뒤 페이지를 접근 불가로 바꾼다.
    utf8* pBase = static_cast<utf8*>( VirtualAlloc( nullptr, pageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );
    SW_ASSERT_TRUE( pBase != nullptr );

    DWORD oldProtect{ 0 };
    SW_EXPECT_TRUE( VirtualProtect( pBase + pageSize, pageSize, PAGE_NOACCESS, &oldProtect ) != 0 );

    // "ab\0" 의 종단자가 첫 페이지의 **마지막 바이트**가 되도록 붙인다.
    utf8* pHaystack = pBase + pageSize - 3;
    pHaystack[0]    = 'a';
    pHaystack[1]    = 'b';
    pHaystack[2]    = '\0';

    // 건초더미(2글자)보다 긴 검색어 — 길이만 보면 다음 페이지까지 읽어야 하는 모양이다.
    SW_EXPECT_TRUE( sw::StringUtil::stristr( pHaystack, "abcdefghij" ) == nullptr );
    SW_EXPECT_TRUE( sw::StringUtil::stristr( pHaystack, "bc" ) == nullptr );

    // 정상 동작도 같이 확인한다.
    SW_EXPECT_TRUE( sw::StringUtil::stristr( pHaystack, "AB" ) == pHaystack );
    SW_EXPECT_TRUE( sw::StringUtil::stristr( pHaystack, "b" ) == pHaystack + 1 );

    VirtualFree( pBase, 0, MEM_RELEASE );
#endif
}

/**
 * @brief [Core_String] 비-ASCII 바이트를 부호 없이 다룬다
 * @details `char` 의 부호성은 구현 정의이고, UTF-8 의 0x80 이상 바이트는 signed char 에서 음수다.
 *          그대로 int 로 넓히면 두 가지가 깨졌다:
 *
 *          1. `compare` 가 한글처럼 비-ASCII 가 섞인 문자열을 ASCII 보다 **작다고** 답했다.
 *             `strcmp` 규약(부호 없는 바이트 비교)의 반대이고, 같은 함수의 대소문자 구분 경로는
 *             이미 `uint8` 로 비교하고 있어서 두 모드가 서로 다른 순서를 냈다.
 *          2. `computeHash64` 는 `bIgnoreCase` 경로만 부호 확장됐다(기본값이 true 다). 같은 바이트가
 *             경로에 따라 다른 값으로 해싱되고, `char` 가 unsigned 인 플랫폼에서는 해시 자체가
 *             달라진다.
 */
SW_TEST_CASE( Core_String, NonAsciiBytesAreUnsigned )
{
    // "가" = EA B0 80 — 모든 바이트가 signed char 에서 음수다.
    const sw::string korean{ "\xEA\xB0\x80" };
    const sw::string ascii{ "a" };

    // 부호 없는 바이트 비교라면 0xEA > 0x61 이므로 한글이 뒤에 온다.
    SW_EXPECT_TRUE( sw::StringUtil::compare( korean, ascii, false ) > 0 );
    SW_EXPECT_TRUE( sw::StringUtil::compare( korean, ascii, true ) > 0 );
    SW_EXPECT_TRUE( sw::StringUtil::compare( ascii, korean, true ) < 0 );

    // 두 모드의 순서가 일치해야 한다 — 예전에는 ignoreCase 쪽만 뒤집혀 있었다.
    const int32 sensitive   = sw::StringUtil::compare( korean, ascii, false );
    const int32 insensitive = sw::StringUtil::compare( korean, ascii, true );
    SW_EXPECT_TRUE( ( sensitive > 0 ) == ( insensitive > 0 ) );

    // 포인터 오버로드도 같아야 한다.
    SW_EXPECT_TRUE( sw::StringUtil::compare( korean.c_str(), ascii.c_str(), true ) > 0 );

    // 비-ASCII 에는 대소문자가 없으므로 두 해시 경로가 같은 값을 내야 한다.
    const uint64 hashIgnore = sw::StringUtil::computeHash64( korean.c_str(), korean.size(), true );
    const uint64 hashExact  = sw::StringUtil::computeHash64( korean.c_str(), korean.size(), false );
    SW_EXPECT_EQUAL( hashExact, hashIgnore );

    // ASCII 는 예전 동작 그대로여야 한다(대문자만 접힌다).
    SW_EXPECT_EQUAL( sw::StringUtil::computeHash64( "ABC", 3, true ), sw::StringUtil::computeHash64( "abc", 3, true ) );
    SW_EXPECT_TRUE( sw::StringUtil::computeHash64( "ABC", 3, false ) != sw::StringUtil::computeHash64( "abc", 3, false ) );
}

/**
 * @brief [Core_String] `%#` 바로 뒤의 `.확장자` 는 리터럴이다 — 로그 파일 이름이 `.txt` 를 잃던 자리.
 * @details `%#.txt` 를 "정밀도 0 + 길이 수식어 t + 16진수 x" 로 읽어 `.tx` 가 사라지고 `t` 만 남았다.
 *          이제 `%#` 은 두 글자만 소비한다. printf 형 정밀도(`%.2f`)와 `%#.%#`(버전 표기)은 그대로여야 한다.
 */
SW_TEST_CASE( Core_String, FormatPlaceholderFollowedByExtensionIsLiteral )
{
    utf8 buffer[128]{};

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "LOG_%#-%#_%#.txt", 2026, 9, "8a19215712386c90" );
    SW_EXPECT_STREQ( "LOG_2026-9_8a19215712386c90.txt", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "api %#.%#.%#", 1, 3, 250 );
    SW_EXPECT_STREQ( "api 1.3.250", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%.2f", 3.14159 );
    SW_EXPECT_STREQ( "3.14", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#. done", "x" );
    SW_EXPECT_STREQ( "x. done", buffer );
}
