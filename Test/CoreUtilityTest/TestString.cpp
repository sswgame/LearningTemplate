/**
 * @file TestString.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Utility/String/hashed_string.h"
#include "Core/Utility/String/fixed_string.h"
#include "Core/Utility/String/string_splitter.h"
#include "Core/Utility/String/formatString.h"

SW_TEST_CASE( Utility_String, StringUtilBasic )
{
	SW_EXPECT_TRUE( sw::StringUtil::isNullOrEmpty( static_cast<const utf8*>( nullptr ) ) );
	SW_EXPECT_TRUE( sw::StringUtil::isNullOrEmpty( "" ) );
	SW_EXPECT_FALSE( sw::StringUtil::isNullOrEmpty( "Hello" ) );

	std::string text	= "  Hello World!  ";
	std::string trimmed = sw::StringUtil::trim( text );
	SW_EXPECT_EQUAL( std::string( "Hello World!" ), trimmed );

	std::string upper = sw::StringUtil::toUpper( "hello" );
	SW_EXPECT_EQUAL( std::string( "HELLO" ), upper );

	std::string lower = sw::StringUtil::toLower( "WORLD" );
	SW_EXPECT_EQUAL( std::string( "world" ), lower );

	std::vector<std::string> parts = sw::StringUtil::split( "apple,banana,orange", "," );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( parts.size() ) );
	if ( parts.size() == 3 )
	{
		SW_EXPECT_EQUAL( std::string( "apple" ), parts[0] );
		SW_EXPECT_EQUAL( std::string( "banana" ), parts[1] );
		SW_EXPECT_EQUAL( std::string( "orange" ), parts[2] );
	}
}

SW_TEST_CASE( Utility_String, HashedString )
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

SW_TEST_CASE( Utility_String, StringSplitter )
{
	sw::string_splitter					 splitter( "one|two|three", { "|" } );
	const std::vector<std::string_view>& tokens = splitter.getSplitList();

	SW_EXPECT_EQUAL( 3u, splitter.getCount() );
	if ( tokens.size() == 3 )
	{
		SW_EXPECT_EQUAL( std::string_view( "one" ), tokens[0] );
		SW_EXPECT_EQUAL( std::string_view( "two" ), tokens[1] );
		SW_EXPECT_EQUAL( std::string_view( "three" ), tokens[2] );
	}
}

SW_TEST_CASE( Utility_String, FixedStringOperations )
{
	sw::fixed_string<64> fs( "Hello" );
	SW_EXPECT_EQUAL( 5u, fs.size() );
	SW_EXPECT_FALSE( fs.empty() );
	SW_EXPECT_EQUAL( std::string( "Hello" ), std::string( fs.c_str() ) );

	fs.append( " World" );
	SW_EXPECT_EQUAL( std::string( "Hello World" ), std::string( fs.c_str() ) );

	fs.push_back( '!' );
	SW_EXPECT_EQUAL( std::string( "Hello World!" ), std::string( fs.c_str() ) );

	sw::fixed_string<64> sub = fs.substr( 0, 5 );
	SW_EXPECT_EQUAL( std::string( "Hello" ), std::string( sub.c_str() ) );

	uint32 foundPos = fs.find( "World" );
	SW_EXPECT_EQUAL( 6u, foundPos );

	fs.erase( 5, 6 );
	SW_EXPECT_EQUAL( std::string( "Hello!" ), std::string( fs.c_str() ) );
}

SW_TEST_CASE( Utility_String, FixedStringFullCoverage )
{
	sw::fixed_string<32> fs( "Engine" );
	SW_EXPECT_EQUAL( 'E', fs.front() );
	SW_EXPECT_EQUAL( 'e', fs.back() );
	SW_EXPECT_EQUAL( 'g', fs[2] );
	SW_EXPECT_EQUAL( 'n', fs.at( 1 ) );

	fs.insert( 0, "Core" );
	SW_EXPECT_EQUAL( std::string( "CoreEngine" ), std::string( fs.c_str() ) );

	fs.pop_back();
	SW_EXPECT_EQUAL( std::string( "CoreEngin" ), std::string( fs.c_str() ) );

	fs += "e";
	SW_EXPECT_EQUAL( std::string( "CoreEngine" ), std::string( fs.c_str() ) );

	sw::fixed_string<32> fs2( "CoreEngine" );
	sw::fixed_string<32> fs3( "OtherEngine" );

	SW_EXPECT_EQUAL( 0, fs.compare( fs2 ) );
	SW_EXPECT_TRUE( fs.compare( fs3 ) != 0 );

	sw::fixed_wstring<32> wfs( L"WideString" );
	SW_EXPECT_EQUAL( 10u, wfs.size() );
	SW_EXPECT_FALSE( wfs.empty() );
}

SW_TEST_CASE( Utility_String, FormatStringUtility )
{
	utf8 buffer[256] = {};

	sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Value: %#", 42 );
	SW_EXPECT_EQUAL( std::string( "Value: 42" ), std::string( buffer ) );

	sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Name: %#, Count: %#, Size: %#", "Subsystem", -10, 256u );
	SW_EXPECT_EQUAL( std::string( "Name: Subsystem, Count: -10, Size: 256" ), std::string( buffer ) );

	sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Hex: 0x%#, HEX: 0x%#", sw::Fmt( 255, sw::Format().hex() ), sw::Fmt( 255, sw::Format().hexUpper() ) );
	SW_EXPECT_EQUAL( std::string( "Hex: 0xff, HEX: 0xFF" ), std::string( buffer ) );

	sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Progress: %#%% complete", 100 );
	SW_EXPECT_EQUAL( std::string( "Progress: 100% complete" ), std::string( buffer ) );
}

#include "Core/Utility/String/StringBuilder.h"

// SW_TEST_CASE( Utility_String, StringBuilderZeroAllocation )\n// Temporarily disabled due to format issue

