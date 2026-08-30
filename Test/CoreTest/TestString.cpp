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

	sw::string text	   = "  Hello World!  ";
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
	const sw::string		   before = R"({"albedo":"white","roughness":0.5,"name":"Mat"})";
	const sw::string		   after  = R"({"albedo":"white","roughness":0.8,"name":"Mat"})";
	const sw::StringChangeSpan span	  = sw::StringUtil::makeChangeSpan( before, after );
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
	const sw::string		   before0	 = R"({"field":"x"})";
	const sw::string		   after1	 = R"({"field":"xy"})";
	const sw::string		   afterN	 = R"({"field":"xyz"})";
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
	sw::string_splitter					splitter( "one|two|three", { "|" } );
	const sw::vector<std::string_view>& tokens = splitter.getSplitList();

	SW_EXPECT_EQUAL( 3u, splitter.getCount() );
	if ( tokens.size() == 3 )
	{
		SW_EXPECT_EQUAL( std::string_view( "one" ), tokens[0] );
		SW_EXPECT_EQUAL( std::string_view( "two" ), tokens[1] );
		SW_EXPECT_EQUAL( std::string_view( "three" ), tokens[2] );
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
 * @brief [Core_String] 포맷 문자열 유틸
 */
SW_TEST_CASE( Core_String, FormatStringUtility )
{
	utf8 buffer[256] = {};

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
	const uint32		 initialCapacity = sb.capacity();
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
	std::string_view	 sv = "ModernCpp";
	sw::fixed_string<32> fs{ sv };
	SW_EXPECT_STREQ( "ModernCpp", fs.c_str() );
	SW_EXPECT_EQUAL( 9u, fs.size() );
	SW_EXPECT_EQUAL( sv, fs.view() );

	SW_EXPECT_TRUE( fs.equals( "MODERNCPP", true ) );
	SW_EXPECT_FALSE( fs.equals( "MODERNCPP", false ) );
	SW_EXPECT_TRUE( fs.equals( sw::fixed_string<32>( "moderncpp" ), true ) );
	SW_EXPECT_FALSE( fs.equals( "Other", true ) );

	std::hash<sw::fixed_string<32>> hasher;
	size_t							h1 = hasher( fs );
	size_t							h2 = hasher( sw::fixed_string<32>( "ModernCpp" ) );
	SW_EXPECT_EQUAL( h1, h2 );
}

/**
 * @brief [Core_String] UTF-8 및 UTF-16 유니코드 상호 인코딩/디코딩 라운드트립 검증
 */
SW_TEST_CASE( Core_String, UnicodeConversionRoundTrip )
{
	// 한글, 이모지, 특수문자
	const utf8*		  kOriginalUtf8 = "안녕하세요 Engine 🚀 (SW_Engine)";
	const sw::wstring utf16Str		= sw::StringUtil::utf8ToUtf16( kOriginalUtf8 );
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
	SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xC2\xA9" ) );			   // © (U+00A9)
	SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xC3\xA9" ) );			   // é (U+00E9)
	SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xE2\x82\xAC" ) );		   // € (U+20AC)
	SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "안녕하세요 엔진 테스트" ) ); // 한글 3바이트
	SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xF0\x9F\x9A\x80" ) );	   // 🚀 (U+1F680)
	SW_EXPECT_TRUE( sw::StringUtil::isValidUTF8( "\xF0\x9F\x98\x80" ) );	   // 😀 (U+1F600)

	// 4) 불완전/잘린 시퀀스 (Truncated sequences)
	SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xC2" ) );		  // 2바이트 리드 바이트만 존재
	SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xE2\x82" ) );	  // 3바이트 중 2바이트만 존재
	SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xF0\x9F\x9A" ) ); // 4바이트 중 3바이트만 존재

	// 5) 비정상 후속 바이트 (Invalid continuation bytes)
	SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xC2\x20" ) );		  // 후속 바이트가 공백 (0x20 != 0x80..0xBF)
	SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xE2\x82\x20" ) );	  // 3번째 바이트 비정상
	SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xF0\x9F\x9A\xC0" ) ); // 4번째 바이트 비정상

	// 6) Overlong 인코딩 (보안 취약점 방지 검증)
	SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xC0\xAF" ) );		  // Overlong 2바이트 '/'
	SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xC1\xBF" ) );		  // Overlong 2바이트
	SW_EXPECT_FALSE( sw::StringUtil::isValidUTF8( "\xE0\x80\xAF" ) );	  // Overlong 3바이트
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
	const utf8* pWithBom	= "\xEF\xBB\xBFHello UTF-8 BOM!";
	const utf8* pWithoutBom = "Hello Without BOM!";

	SW_EXPECT_EQUAL( std::string_view( "Hello UTF-8 BOM!" ), sw::FileUtil::skipUtf8Bom( pWithBom ) );
	SW_EXPECT_EQUAL( std::string_view( pWithoutBom ), sw::FileUtil::skipUtf8Bom( pWithoutBom ) );

	// 2) 포인터 및 크기 기반 BOM 스킵 검증
	const uint8* pBytes = reinterpret_cast<const uint8*>( pWithBom );
	size_t		 size	= strlen( pWithBom );
	sw::FileUtil::skipUtf8Bom( pBytes, size );
	SW_EXPECT_EQUAL( strlen( "Hello UTF-8 BOM!" ), size );
	SW_EXPECT_EQUAL( 'H', static_cast<utf8>( pBytes[0] ) );

	// 3) 파일 I/O 자동 BOM 제거 검증
	const sw::string tempDir	 = sw::FileUtil::getTempDirectory();
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
	const std::string	   stdStrSource = "FromStdString";
	const std::string_view svSource		= "FromView";
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

	std::istringstream	 inStream( "StreamedContent" );
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
	size_t							 h1 = hasher( wstr );
	size_t							 h2 = hasher( sw::fixed_wstring<32>( wstr.c_str() ) );
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
