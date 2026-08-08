#include "StringUtil.h"
#include "StringUtil.h"
/**
 * @file StringUtil.cpp
 * @brief 문자열 유틸리티 구현
 */
#include "pch.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Utility/Log/Logger.h"

#include "StringUtil.h"

namespace
{
	constexpr uint32 kMaxUnicodeCodepoint = 0x10FFFF;
	constexpr uint32 kSurrogateBegin	  = 0xD800;
	constexpr uint32 kSurrogateEnd		  = 0xDFFF;
	constexpr uint32 kUtf16LowBoundary	  = 0x10000;

	struct Utf8LeadInfo
	{
		size_t _sequenceLength;
		uint32 _initialBits;
		uint32 _minCodepoint;
	};

	constexpr Utf8LeadInfo classifyUtf8LeadByte( const uint8 leadByte ) noexcept
	{
		if ( ( leadByte & 0x80 ) == 0x00 )
			return { 1, leadByte, 0x0 };
		if ( ( leadByte & 0xE0 ) == 0xC0 )
			return { 2, static_cast<uint32>( leadByte & 0x1F ), 0x80 };
		if ( ( leadByte & 0xF0 ) == 0xE0 )
			return { 3, static_cast<uint32>( leadByte & 0x0F ), 0x800 };
		if ( ( leadByte & 0xF8 ) == 0xF0 )
			return { 4, static_cast<uint32>( leadByte & 0x07 ), kUtf16LowBoundary };

		return { 0, 0, 0 };
	}

	bool isValidUtf8( const uint8* data, const size_t length ) noexcept
	{
		size_t pos = 0;
		while ( pos < length )
		{
			const Utf8LeadInfo lead = classifyUtf8LeadByte( data[pos] );
			if ( lead._sequenceLength == 0 || pos + lead._sequenceLength > length )
				return false;

			uint32 codepoint = lead._initialBits;
			for ( size_t byteIndex = 1; byteIndex < lead._sequenceLength; ++byteIndex )
			{
				const uint8 continuationByte = data[pos + byteIndex];
				if ( ( continuationByte & 0xC0 ) != 0x80 )
					return false;
				codepoint = ( codepoint << 6 ) | ( continuationByte & 0x3F );
			}

			const bool bIsOverlong	 = codepoint < lead._minCodepoint;
			const bool bIsSurrogate	 = ( codepoint >= kSurrogateBegin ) && ( codepoint <= kSurrogateEnd );
			const bool bIsOutOfRange = codepoint > kMaxUnicodeCodepoint;
			if ( bIsOverlong || bIsSurrogate || bIsOutOfRange )
				return false;

			pos += lead._sequenceLength;
		}
		return true;
	}

	struct DecodedCodepoint
	{
		uint32 _codepoint;
		size_t _byteCount;
	};

	DecodedCodepoint decodeUtf8Sequence( const utf8* data, const size_t remaining ) noexcept
	{
		const Utf8LeadInfo lead		 = classifyUtf8LeadByte( static_cast<uint8>( data[0] ) );
		const size_t	   byteCount = std::min( lead._sequenceLength, remaining );

		uint32 codepoint = lead._initialBits;
		for ( size_t byteIndex = 1; byteIndex < byteCount; ++byteIndex )
			codepoint = ( codepoint << 6 ) | ( static_cast<uint8>( data[byteIndex] ) & 0x3F );

		return { codepoint, byteCount };
	}

	void appendWideChar( std::wstring& out, const uint32 codepoint )
	{
		if constexpr ( sizeof( wchar_t ) == 2 )
		{
			if ( codepoint < kUtf16LowBoundary )
			{
				out.push_back( static_cast<wchar_t>( codepoint ) );
			}
			else
			{
				const uint32 value = codepoint - kUtf16LowBoundary;
				out.push_back( static_cast<wchar_t>( 0xD800 + ( value >> 10 ) ) );
				out.push_back( static_cast<wchar_t>( 0xDC00 + ( value & 0x3FF ) ) );
			}
		}
		else
		{
			out.push_back( static_cast<wchar_t>( codepoint ) );
		}
	}

	void appendUtf8( std::string& out, const uint32 codepoint )
	{
		if ( codepoint <= 0x7F )
		{
			out.push_back( static_cast<utf8>( codepoint ) );
		}
		else if ( codepoint <= 0x7FF )
		{
			out.push_back( static_cast<utf8>( 0xC0 | ( codepoint >> 6 ) ) );
			out.push_back( static_cast<utf8>( 0x80 | ( codepoint & 0x3F ) ) );
		}
		else if ( codepoint <= 0xFFFF )
		{
			out.push_back( static_cast<utf8>( 0xE0 | ( codepoint >> 12 ) ) );
			out.push_back( static_cast<utf8>( 0x80 | ( ( codepoint >> 6 ) & 0x3F ) ) );
			out.push_back( static_cast<utf8>( 0x80 | ( codepoint & 0x3F ) ) );
		}
		else
		{
			out.push_back( static_cast<utf8>( 0xF0 | ( codepoint >> 18 ) ) );
			out.push_back( static_cast<utf8>( 0x80 | ( ( codepoint >> 12 ) & 0x3F ) ) );
			out.push_back( static_cast<utf8>( 0x80 | ( ( codepoint >> 6 ) & 0x3F ) ) );
			out.push_back( static_cast<utf8>( 0x80 | ( codepoint & 0x3F ) ) );
		}
	}
} // namespace

namespace sw
{

	bool StringUtil::isNullOrEmpty( const utf8* str )
	{
		return ( str == nullptr || *str == '\0' );
	}

	bool StringUtil::isNullOrEmpty( const utf16* str )
	{
		return ( str == nullptr || *str == L'\0' );
	}

	std::wstring StringUtil::localeToUtf16( const std::string_view& input )
	{
		if ( isValidUTF8( input ) )
			return utf8ToUtf16( input );

		return toWString( input );
	}

	std::wstring StringUtil::utf8ToUtf16( const std::string_view& input )
	{
		SW_LOG_ASSERT( isValidUTF8( input ), "UTF8 문자열이 아닙니다" );

		std::wstring result{};
		result.reserve( input.size() );

		size_t pos = 0;
		while ( pos < input.size() )
		{
			const DecodedCodepoint decoded = decodeUtf8Sequence( input.data() + pos, input.size() - pos );
			appendWideChar( result, decoded._codepoint );
			pos += decoded._byteCount;
		}

		return result;
	}

	std::string StringUtil::utf16ToUtf8( const std::wstring_view& input )
	{
		std::string result{};
		result.reserve( input.size() );

		size_t pos = 0;
		while ( pos < input.size() )
		{
			uint32 codepoint = static_cast<uint32>( input[pos] );

			if constexpr ( sizeof( wchar_t ) == 2 )
			{
				const bool bIsHighSurrogate = ( codepoint >= 0xD800 ) && ( codepoint <= 0xDBFF );
				if ( bIsHighSurrogate )
				{
					if ( pos + 1 < input.size() )
					{
						const uint32 lowSurrogate = static_cast<uint32>( input[pos + 1] );
						if ( lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF )
						{
							codepoint = kUtf16LowBoundary + ( ( codepoint - 0xD800 ) << 10 ) + ( lowSurrogate - 0xDC00 );
							++pos;
						}
						else
						{
							codepoint = 0xFFFD;
						}
					}
					else
					{
						codepoint = 0xFFFD;
					}
				}
				else if ( codepoint >= 0xDC00 && codepoint <= 0xDFFF )
				{
					codepoint = 0xFFFD;
				}
			}

			appendUtf8( result, codepoint );
			++pos;
		}

		SW_LOG_ASSERT( isValidUTF8( result ), "UTF8 문자열이 아닙니다" );
		return result;
	}

	std::string StringUtil::utf16ToLocale( const std::wstring_view& input )
	{
		return toString( input );
	}

	std::string StringUtil::localeToUtf8( const std::string_view& input )
	{
		if ( input.empty() )
			return std::string{};
		if ( isValidUTF8( input ) )
			return std::string{ input.data(), input.size() };

		const std::wstring wideStr = toWString( input );
		return utf16ToUtf8( wideStr );
	}

	std::string StringUtil::utf8ToLocale( const std::string_view& input )
	{
		if ( input.empty() )
			return std::string{};

		const std::wstring wideStr = utf8ToUtf16( input );
		return toString( wideStr );
	}

	std::vector<std::string> StringUtil::split( const std::string_view& input, const std::string_view& delimiterList )
	{
		std::vector<std::string> tokenList{};

		size_t startPos = 0;
		while ( true )
		{
			const size_t foundPos = input.find_first_of( delimiterList, startPos );
			if ( foundPos == std::string::npos )
				break;

			if ( foundPos != startPos )
				tokenList.emplace_back( input.substr( startPos, foundPos - startPos ) );

			startPos = foundPos + 1;
		}

		const bool bHasRemain = ( startPos < input.length() );
		if ( bHasRemain )
			tokenList.emplace_back( input.substr( startPos ) );

		return tokenList;
	}

	std::string StringUtil::toUpper( const std::string_view& input )
	{
		std::string result{ input.data(), input.size() };
		std::transform( result.begin(), result.end(), result.begin(), []( const utf8 ch )
		{
			const uint8 uCh = static_cast<uint8>( ch );
			return ( uCh <= 127 ) ? static_cast<utf8>( std::toupper( uCh ) ) : ch;
		} );

		return result;
	}

	std::string StringUtil::toLower( const std::string_view& input )
	{
		std::string result{ input.data(), input.size() };
		std::transform( result.begin(), result.end(), result.begin(), []( const utf8 ch )
		{
			const uint8 uCh = static_cast<uint8>( ch );
			return ( uCh <= 127 ) ? static_cast<utf8>( std::tolower( uCh ) ) : ch;
		} );

		return result;
	}

	std::string StringUtil::trimStart( const std::string_view& input )
	{
		const size_t start = input.find_first_not_of( kWhiteSpace );
		return ( start == std::string::npos ) ? std::string{} : std::string{ input.substr( start ) };
	}

	std::string StringUtil::trimEnd( const std::string_view& input )
	{
		const size_t end = input.find_last_not_of( kWhiteSpace );
		return ( end == std::string::npos ) ? std::string{} : std::string{ input.substr( 0, end + 1 ) };
	}

	std::string StringUtil::trim( const std::string_view& input )
	{
		return trimEnd( trimStart( input ) );
	}

	uint32 StringUtil::strlen( const utf8* str )
	{
		if ( StringUtil::isNullOrEmpty( str ) )
			return 0;
		return static_cast<uint32>( std::char_traits<utf8>::length( str ) );
	}

	uint32 StringUtil::strlen( const utf16* str )
	{
		if ( StringUtil::isNullOrEmpty( str ) )
			return 0;
		return static_cast<uint32>( std::char_traits<utf16>::length( str ) );
	}

	int32 StringUtil::strnicmp( const utf8* lhs, const utf8* rhs, const uint32 stringLength )
	{
#if defined( SW_PLATFORM_WINDOWS )
		return _strnicmp( lhs, rhs, stringLength );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		return strncasecmp( lhs, rhs, stringLength );
#else
		for ( uint32 charIndex = 0; charIndex < stringLength; ++charIndex )
		{
			const int32 c1 = std::tolower( static_cast<uint8>( lhs[charIndex] ) );
			const int32 c2 = std::tolower( static_cast<uint8>( rhs[charIndex] ) );
			if ( c1 != c2 )
				return c1 - c2;
			if ( lhs[charIndex] == '\0' )
				break;
		}
		return 0;
#endif
	}

	int32 StringUtil::strnicmp( const utf16* lhs, const utf16* rhs, const uint32 stringLength )
	{
#if defined( SW_PLATFORM_WINDOWS )
		return _wcsnicmp( lhs, rhs, stringLength );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		return wcsncasecmp( lhs, rhs, stringLength );
#else
		for ( uint32 charIndex = 0; charIndex < stringLength; ++charIndex )
		{
			const int32 c1 = towlower( static_cast<wint_t>( lhs[charIndex] ) );
			const int32 c2 = towlower( static_cast<wint_t>( rhs[charIndex] ) );
			if ( c1 != c2 )
				return c1 - c2;
			if ( lhs[charIndex] == L'\0' )
				break;
		}
		return 0;
#endif
	}

	int32 StringUtil::strcmp( const utf8* lhs, const utf8* rhs )
	{
		if ( lhs == rhs )
			return 0;
		if ( StringUtil::isNullOrEmpty( lhs ) )
			return -1;
		if ( StringUtil::isNullOrEmpty( rhs ) )
			return 1;
		return std::strcmp( reinterpret_cast<const utf8*>( lhs ), reinterpret_cast<const utf8*>( rhs ) );
	}

	int32 StringUtil::strcmp( const utf16* lhs, const utf16* rhs )
	{
		if ( lhs == rhs )
			return 0;
		if ( StringUtil::isNullOrEmpty( lhs ) )
			return -1;
		if ( StringUtil::isNullOrEmpty( rhs ) )
			return 1;
		return std::wcscmp( reinterpret_cast<const utf16*>( lhs ), reinterpret_cast<const utf16*>( rhs ) );
	}

	void StringUtil::strncpy( utf8* destination, const utf8* source, const uint32 length )
	{
#if defined( SW_PLATFORM_WINDOWS )
		::strncpy_s( destination, length, source );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		::strncpy( destination, source, length );
#else
	#error "Unsupported platform"
#endif
	}

	void StringUtil::strncpy( utf16* destination, const utf16* source, const uint32 length )
	{
#if defined( SW_PLATFORM_WINDOWS )
		::wcsncpy_s( destination, length, source, _TRUNCATE );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		::wcsncpy( destination, source, length );
#else
	#error "Unsupported platform"
#endif
	}

	const utf8* StringUtil::strstr( const utf8* str, const utf8* substr )
	{
		return ::strstr( str, substr );
	}

	const utf16* StringUtil::strstr( const utf16* str, const utf16* substr )
	{
		return wcsstr( str, substr );
	}

	const utf8* StringUtil::stristr( const utf8* str, const utf8* substr )
	{
		if ( str == nullptr || substr == nullptr )
			return nullptr;

		const utf8*	 start	   = str;
		const uint32 substrLen = StringUtil::strlen( substr );

		while ( *start != 0 )
		{
			if ( strnicmp( start, substr, substrLen ) == 0 )
				return start;
			++start;
		}
		return nullptr;
	}

	const utf16* StringUtil::stristr( const utf16* str, const utf16* substr )
	{
		if ( str == nullptr || substr == nullptr )
			return nullptr;

		const utf16* start	   = str;
		const uint32 substrLen = StringUtil::strlen( substr );

		while ( *start != 0 )
		{
			if ( strnicmp( start, substr, substrLen ) == 0 )
				return start;
			++start;
		}
		return nullptr;
	}

	const utf8* StringUtil::strchr( const utf8* str, const utf8 c )
	{
		return ::strchr( str, c );
	}

	const utf16* StringUtil::strchr( const utf16* str, const utf16 c )
	{
		return wcschr( str, c );
	}

	std::string StringUtil::toString( const std::wstring_view& input )
	{
		if ( input.empty() )
			return std::string{};

		std::wstring nullTerminated{ input };
		size_t		 requiredSize = 0;
#if defined( SW_PLATFORM_WINDOWS )
		wcstombs_s( &requiredSize, nullptr, 0, nullTerminated.c_str(), 0 );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		wcstombs( nullptr, nullTerminated.c_str(), 0 );
#else
	#error "Unsupported platform"
#endif
		if ( requiredSize == 0 || requiredSize == static_cast<size_t>( -1 ) )
			return std::string{};

		std::string buffer( requiredSize - 1, '\0' );
#if defined( SW_PLATFORM_WINDOWS )
		wcstombs_s( nullptr, buffer.data(), requiredSize, nullTerminated.c_str(), requiredSize );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		wcstombs( buffer.data(), nullTerminated.c_str(), requiredSize );
#else
	#error "Unsupported platform"
#endif

		return buffer;
	}

	std::wstring StringUtil::toWString( const std::string_view& input )
	{
		if ( input.empty() )
			return std::wstring{};

		std::string nullTerminated{ input };
		size_t		requiredSize = 0;
#if defined( SW_PLATFORM_WINDOWS )
		mbstowcs_s( &requiredSize, nullptr, 0, nullTerminated.c_str(), 0 );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		mbstowcs( nullptr, nullTerminated.c_str(), 0 );
#else
	#error "Unsupported platform"
#endif
		if ( requiredSize == 0 || requiredSize == static_cast<size_t>( -1 ) )

			return std::wstring{};

		std::wstring buffer( requiredSize - 1, L'\0' );
#if defined( SW_PLATFORM_WINDOWS )
		mbstowcs_s( nullptr, buffer.data(), requiredSize, nullTerminated.c_str(), requiredSize );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		mbstowcs( buffer.data(), nullTerminated.c_str(), requiredSize );
#else
	#error "Unsupported platform"
#endif

		return buffer;
	}

	bool StringUtil::isValidUTF8( const std::string_view input )
	{
		return isValidUtf8( reinterpret_cast<const uint8*>( input.data() ), input.size() );
	}
} // namespace sw
