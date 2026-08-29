#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Core/CoreMinimal.h"

namespace sw
{
	namespace
	{
		struct StringUtilInternal
		{
			static constexpr uint32 kMaxUnicodeCodepoint = 0x10FFFF;
			static constexpr uint32 kSurrogateBegin		 = 0xD800;
			static constexpr uint32 kSurrogateEnd		 = 0xDFFF;
			static constexpr uint32 kUtf16LowBoundary	 = 0x10000;

			static SW_INLINE constexpr bool isWhitespace( utf8 c ) noexcept
			{
				return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
			}

			static SW_INLINE constexpr bool isWhitespace( utf16 c ) noexcept
			{
				return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\f' || c == L'\v';
			}

			struct Utf8LeadInfo
			{
				size_t _sequenceLength;
				uint32 _initialBits;
				uint32 _minCodepoint;
			};

			/**
			 * @brief UTF-8 선행 바이트로부터 멀티바이트 시퀀스 길이(1~4 바이트) 및 초기 비트를 분류합니다.
			 */
			static constexpr Utf8LeadInfo classifyUtf8LeadByte( const uint8 leadByte ) noexcept
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

			/**
			 * @brief 바이트 배열이 표준 UTF-8 인코딩 규칙을 준수하는지 검증합니다.
			 * @details
			 * - **64비트 SWAR (SIMD Within A Register) Fast-Scan**:
			 *   대부분의 텍스트(로그 포맷, 태그, 영문 등)가 7비트 ASCII(0x00~0x7F)라는 점에 착안하여,
			 *   `uint64` 8바이트씩 한 번에 읽어 최상위 비트 마스크(`0x8080808080808080ULL`)로 1클럭에 8바이트를 초고속 검사합니다.
			 * - **멀티바이트 시퀀스 정밀 검증**:
			 *   비-ASCII 바이트를 만났을 때만 2~4바이트 UTF-8 리드 바이트를 분류하고 후속 바이트(0x80~0xBF), Overlong, Surrogate(0xD800~0xDFFF), 최대 유니코드 범위를 철저히 검증합니다.
			 */
			static bool isValidUtf8( const uint8* pData, const size_t length ) noexcept
			{
				size_t pos{ 0 };

				// 8바이트 단위 SWAR 빠른 ASCII 검사 (1클럭에 8글자 일괄 검사)
				while ( pos + 8 <= length )
				{
					uint64 chunk{ 0 };
					Memory::copy( &chunk, pData + pos, 8 );
					if ( ( chunk & 0x8080808080808080ULL ) == 0 )
					{
						pos += 8;
						continue;
					}
					break;
				}

				while ( pos < length )
				{
					const uint8 byte0 = pData[pos];
					// 1바이트 ASCII 문자 (0x00 ~ 0x7F)
					if ( ( byte0 & 0x80 ) == 0 )
					{
						++pos;
						// 다시 8바이트 SWAR 가속 스캔 시도
						while ( pos + 8 <= length )
						{
							uint64 chunk{ 0 };
							Memory::copy( &chunk, pData + pos, 8 );
							if ( ( chunk & 0x8080808080808080ULL ) == 0 )
							{
								pos += 8;
								continue;
							}
							break;
						}
						continue;
					}

					// 2~4바이트 멀티바이트 UTF-8 시퀀스 검증
					const Utf8LeadInfo lead = classifyUtf8LeadByte( byte0 );
					if ( lead._sequenceLength == 0 || pos + lead._sequenceLength > length )
						return false;

					uint32 codepoint = lead._initialBits;
					for ( size_t byteIndex = 1; byteIndex < lead._sequenceLength; ++byteIndex )
					{
						const uint8 continuationByte = pData[pos + byteIndex];
						if ( ( continuationByte & 0xC0 ) != 0x80 )
							return false;
						codepoint = ( codepoint << 6 ) | ( continuationByte & 0x3F );
					}

					// Overlong 인코딩, UTF-16 서로게이트(Surrogate), 유니코드 최대치 초과 여부 검사
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

			static DecodedCodepoint decodeUtf8Sequence( const utf8* pData, const size_t remaining ) noexcept
			{
				const Utf8LeadInfo lead		 = classifyUtf8LeadByte( static_cast<uint8>( pData[0] ) );
				const size_t	   byteCount = MathUtil::min( lead._sequenceLength, remaining );

				uint32 codepoint = lead._initialBits;
				for ( size_t byteIndex = 1; byteIndex < byteCount; ++byteIndex )
				{
					codepoint = ( codepoint << 6 ) | ( static_cast<uint8>( pData[byteIndex] ) & 0x3F );
				}

				return { codepoint, byteCount };
			}

			static void appendWideChar( wstring& out, const uint32 codepoint )
			{
				if constexpr ( sizeof( utf16 ) == 2 )
				{
					if ( codepoint < kUtf16LowBoundary )
						out.push_back( static_cast<utf16>( codepoint ) );
					else
					{
						const uint32 value = codepoint - kUtf16LowBoundary;
						out.push_back( static_cast<utf16>( 0xD800 + ( value >> 10 ) ) );
						out.push_back( static_cast<utf16>( 0xDC00 + ( value & 0x3FF ) ) );
					}
				}
				else
					out.push_back( static_cast<utf16>( codepoint ) );
			}

			static void appendUtf8( string& out, const uint32 codepoint )
			{
				if ( codepoint <= 0x7F )
					out.push_back( static_cast<utf8>( codepoint ) );
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
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "StringUtil" );

	/**
	 * @brief 32비트 부호 있는 정수를 0-Alloc 스택 버퍼를 통해 문자열로 변환합니다.
	 */
	string to_string( int32 value )
	{
		utf8 buf[constant::kMaxBuffer32];
		auto [ptr, ec] = std::to_chars( buf, buf + sizeof( buf ), value );
		return string( buf, static_cast<size_t>( ptr - buf ) );
	}

	/**
	 * @brief 32비트 부호 없는 정수를 문자열로 변환합니다.
	 */
	string to_string( uint32 value )
	{
		utf8 buf[constant::kMaxBuffer32];
		auto [ptr, ec] = std::to_chars( buf, buf + sizeof( buf ), value );
		return string( buf, static_cast<size_t>( ptr - buf ) );
	}

	/**
	 * @brief 64비트 정수를 문자열로 변환합니다.
	 */
	string to_string( int64 value )
	{
		utf8 buf[constant::kMaxBuffer32];
		auto [ptr, ec] = std::to_chars( buf, buf + sizeof( buf ), value );
		return string( buf, static_cast<size_t>( ptr - buf ) );
	}

	/**
	 * @brief 64비트 부호 없는 정수를 문자열로 변환합니다.
	 */
	string to_string( uint64 value )
	{
		utf8 buf[constant::kMaxBuffer32];
		auto [ptr, ec] = std::to_chars( buf, buf + sizeof( buf ), value );
		return string( buf, static_cast<size_t>( ptr - buf ) );
	}

	/**
	 * @brief 32비트 부동소수점 숫자를 문자열로 변환합니다.
	 */
	string to_string( float32 value )
	{
		utf8 buf[constant::kMaxBuffer64];
		auto [ptr, ec] = std::to_chars( buf, buf + sizeof( buf ), value );
		if ( ec == std::errc() )
			return string( buf, static_cast<size_t>( ptr - buf ) );

		formatstring( buf, sizeof( buf ), "%#", value );
		return string{ buf };
	}

	/**
	 * @brief 64비트 배정밀도 부동소수점 숫자를 문자열로 변환합니다.
	 */
	string to_string( float64 value )
	{
		utf8 buf[constant::kMaxBuffer64];
		auto [ptr, ec] = std::to_chars( buf, buf + sizeof( buf ), value );
		if ( ec == std::errc() )
			return string( buf, static_cast<size_t>( ptr - buf ) );

		formatstring( buf, sizeof( buf ), "%#", value );
		return string{ buf };
	}

	bool StringUtil::isNullOrEmpty( const utf8* str )
	{
		return ( str == nullptr || *str == '\0' );
	}

	bool StringUtil::isNullOrEmpty( const utf16* str )
	{
		return ( str == nullptr || *str == L'\0' );
	}

	wstring StringUtil::utf8ToUtf16( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		SW_LOG_ASSERT( isValidUTF8( input ), "UTF8 문자열이 아닙니다" );

		const size_t length = strlen( input );
		wstring		 result{};
		result.reserve( length );

		size_t pos{ 0 };
		while ( pos < length )
		{
			const uint8 byte0 = static_cast<uint8>( input[pos] );
			if ( byte0 <= 0x7F )
			{
				result.push_back( static_cast<utf16>( byte0 ) );
				++pos;
				continue;
			}

			const StringUtilInternal::DecodedCodepoint decoded = StringUtilInternal::decodeUtf8Sequence( input + pos, length - pos );
			StringUtilInternal::appendWideChar( result, decoded._codepoint );
			pos += MathUtil::max( decoded._byteCount, static_cast<size_t>( 1 ) );
		}

		return result;
	}

	string StringUtil::utf16ToUtf8( const utf16* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const size_t length = strlen( input );
		string		 result{};
		result.reserve( length );

		size_t pos{ 0 };
		while ( pos < length )
		{
			uint32 codepoint = input[pos];
			if ( codepoint <= 0x7F )
			{
				result.push_back( static_cast<utf8>( codepoint ) );
				++pos;
				continue;
			}

			if constexpr ( sizeof( utf16 ) == 2 )
			{
				const bool bIsHighSurrogate = ( 0xD800 <= codepoint && codepoint <= 0xDBFF );
				if ( bIsHighSurrogate )
				{
					if ( pos + 1 < length )
					{
						const uint32 lowSurrogate = input[pos + 1];
						if ( 0xDC00 <= lowSurrogate && lowSurrogate <= 0xDFFF )
						{
							codepoint = StringUtilInternal::kUtf16LowBoundary + ( ( codepoint - 0xD800 ) << 10 ) + ( lowSurrogate - 0xDC00 );
							++pos;
						}
						else
							codepoint = 0xFFFD;
					}
					else
						codepoint = 0xFFFD;
				}
				else if ( 0xDC00 <= codepoint && codepoint <= 0xDFFF )
					codepoint = 0xFFFD;
			}

			StringUtilInternal::appendUtf8( result, codepoint );
			++pos;
		}

		SW_LOG_ASSERT( isValidUTF8( result.c_str() ), "UTF8 문자열이 아닙니다" );
		return result;
	}

	string StringUtil::utf16ToLocale( const utf16* input )
	{
		return toString( input );
	}

	wstring StringUtil::localeToUtf16( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};
		if ( isValidUTF8( input ) )
			return utf8ToUtf16( input );

		return toWString( input );
	}

	string StringUtil::localeToUtf8( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};
		if ( isValidUTF8( input ) )
			return string{ input };

		const wstring wideStr = toWString( input );
		return utf16ToUtf8( wideStr.c_str() );
	}

	string StringUtil::utf8ToLocale( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const wstring wideStr = utf8ToUtf16( input );
		return toString( wideStr.c_str() );
	}

	string StringUtil::toUpper( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const size_t length = strlen( input );
		string		 result;
		result.resize( length );

		for ( size_t charIndex = 0; charIndex < length; ++charIndex )
		{
			const uint8 uCh	  = static_cast<uint8>( input[charIndex] );
			result[charIndex] = ( uCh >= 'a' && uCh <= 'z' ) ? static_cast<utf8>( uCh - 32 ) : input[charIndex];
		}

		return result;
	}

	wstring StringUtil::toUpper( const utf16* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const size_t length = strlen( input );
		wstring		 result;
		result.resize( length );

		for ( size_t charIndex = 0; charIndex < length; ++charIndex )
		{
			const utf16 ch	  = input[charIndex];
			result[charIndex] = ( ch >= L'a' && ch <= L'z' ) ? static_cast<utf16>( ch - 32 ) : static_cast<utf16>( std::towupper( static_cast<wint_t>( ch ) ) );
		}

		return result;
	}

	string StringUtil::toLower( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const size_t length = strlen( input );
		string		 result;
		result.resize( length );

		for ( size_t charIndex = 0; charIndex < length; ++charIndex )
		{
			const uint8 uCh	  = static_cast<uint8>( input[charIndex] );
			result[charIndex] = ( uCh >= 'A' && uCh <= 'Z' ) ? static_cast<utf8>( uCh + 32 ) : input[charIndex];
		}

		return result;
	}

	wstring StringUtil::toLower( const utf16* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const size_t length = strlen( input );
		wstring		 result;
		result.resize( length );

		for ( size_t charIndex = 0; charIndex < length; ++charIndex )
		{
			const utf16 ch	  = input[charIndex];
			result[charIndex] = ( ch >= L'A' && ch <= L'Z' ) ? static_cast<utf16>( ch + 32 ) : static_cast<utf16>( std::towlower( static_cast<wint_t>( ch ) ) );
		}

		return result;
	}

	bool StringUtil::equalsIgnoreCase( const utf8* lhs, const utf8* rhs )
	{
		if ( lhs == rhs )
			return true;
		if ( lhs == nullptr || rhs == nullptr )
			return false;

		while ( *lhs != '\0' && *rhs != '\0' )
		{
			if ( *lhs != *rhs && toLowerChar( *lhs ) != toLowerChar( *rhs ) )
				return false;
			++lhs;
			++rhs;
		}
		return *lhs == *rhs;
	}

	bool StringUtil::equalsIgnoreCase( const utf16* lhs, const utf16* rhs )
	{
		if ( lhs == rhs )
			return true;
		if ( lhs == nullptr || rhs == nullptr )
			return false;

		while ( *lhs != L'\0' && *rhs != L'\0' )
		{
			if ( *lhs != *rhs && toLowerChar( *lhs ) != toLowerChar( *rhs ) )
				return false;
			++lhs;
			++rhs;
		}
		return *lhs == *rhs;
	}

	bool StringUtil::equalsIgnoreCase( string_view lhs, string_view rhs )
	{
		if ( lhs.size() != rhs.size() )
			return false;

		for ( size_t charIndex = 0; charIndex < lhs.size(); ++charIndex )
		{
			if ( lhs[charIndex] != rhs[charIndex] && toLowerChar( lhs[charIndex] ) != toLowerChar( rhs[charIndex] ) )
				return false;
		}
		return true;
	}

	bool StringUtil::equalsIgnoreCase( wstring_view lhs, wstring_view rhs )
	{
		if ( lhs.size() != rhs.size() )
			return false;

		for ( size_t charIndex = 0; charIndex < lhs.size(); ++charIndex )
		{
			if ( lhs[charIndex] != rhs[charIndex] && toLowerChar( lhs[charIndex] ) != toLowerChar( rhs[charIndex] ) )
				return false;
		}
		return true;
	}

	string StringUtil::trimStart( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const utf8* start = input;
		while ( *start != '\0' && StringUtilInternal::isWhitespace( *start ) )
		{
			++start;
		}
		return string{ start };
	}

	wstring StringUtil::trimStart( const utf16* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const utf16* start = input;
		while ( *start != L'\0' && StringUtilInternal::isWhitespace( *start ) )
		{
			++start;
		}
		return wstring{ start };
	}

	string StringUtil::trimEnd( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const size_t length = strlen( input );
		size_t		 end	= length;
		while ( end > 0 && StringUtilInternal::isWhitespace( input[end - 1] ) )
		{
			--end;
		}
		return string{ input, end };
	}

	wstring StringUtil::trimEnd( const utf16* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const size_t length = strlen( input );
		size_t		 end	= length;
		while ( end > 0 && StringUtilInternal::isWhitespace( input[end - 1] ) )
		{
			--end;
		}
		return wstring{ input, end };
	}

	string StringUtil::trim( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const utf8* start = input;
		while ( *start != '\0' && StringUtilInternal::isWhitespace( *start ) )
		{
			++start;
		}
		if ( *start == '\0' )
			return {};

		const utf8* end = start + strlen( start );
		while ( end > start && StringUtilInternal::isWhitespace( *( end - 1 ) ) )
		{
			--end;
		}
		return string{ start, static_cast<size_t>( end - start ) };
	}

	wstring StringUtil::trim( const utf16* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		const utf16* start = input;
		while ( *start != L'\0' && StringUtilInternal::isWhitespace( *start ) )
		{
			++start;
		}
		if ( *start == L'\0' )
			return {};

		const utf16* end = start + strlen( start );
		while ( end > start && StringUtilInternal::isWhitespace( *( end - 1 ) ) )
		{
			--end;
		}
		return wstring{ start, static_cast<size_t>( end - start ) };
	}

	string_view StringUtil::trimStart( string_view input )
	{
		size_t start{ 0 };
		while ( start < input.size() && StringUtilInternal::isWhitespace( input[start] ) )
		{
			++start;
		}
		return input.substr( start );
	}

	wstring_view StringUtil::trimStart( wstring_view input )
	{
		size_t start{ 0 };
		while ( start < input.size() && StringUtilInternal::isWhitespace( input[start] ) )
		{
			++start;
		}
		return input.substr( start );
	}

	string_view StringUtil::trimEnd( string_view input )
	{
		size_t end = input.size();
		while ( end > 0 && StringUtilInternal::isWhitespace( input[end - 1] ) )
		{
			--end;
		}
		return input.substr( 0, end );
	}

	wstring_view StringUtil::trimEnd( wstring_view input )
	{
		size_t end = input.size();
		while ( end > 0 && StringUtilInternal::isWhitespace( input[end - 1] ) )
		{
			--end;
		}
		return input.substr( 0, end );
	}

	string_view StringUtil::trim( string_view input )
	{
		return trimEnd( trimStart( input ) );
	}

	wstring_view StringUtil::trim( wstring_view input )
	{
		return trimEnd( trimStart( input ) );
	}

	uint32 StringUtil::strlen( const utf8* str )
	{
		if ( isNullOrEmpty( str ) )
			return 0;
		return static_cast<uint32>( std::char_traits<utf8>::length( str ) );
	}

	uint32 StringUtil::strlen( const utf16* str )
	{
		if ( isNullOrEmpty( str ) )
			return 0;
		return static_cast<uint32>( std::char_traits<utf16>::length( str ) );
	}

	int32 StringUtil::strnicmp( const utf8* lhs, const utf8* rhs, const uint32 stringLength )
	{
		if ( lhs == rhs )
			return 0;
		if ( lhs == nullptr )
			return -1;
		if ( rhs == nullptr )
			return 1;

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
		if ( lhs == rhs )
			return 0;
		if ( lhs == nullptr )
			return -1;
		if ( rhs == nullptr )
			return 1;

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
		if ( isNullOrEmpty( lhs ) )
			return -1;
		if ( isNullOrEmpty( rhs ) )
			return 1;
		return std::strcmp( lhs, rhs );
	}

	int32 StringUtil::strcmp( const utf16* lhs, const utf16* rhs )
	{
		if ( lhs == rhs )
			return 0;
		if ( isNullOrEmpty( lhs ) )
			return -1;
		if ( isNullOrEmpty( rhs ) )
			return 1;
		return std::wcscmp( lhs, rhs );
	}

	int32 StringUtil::strncmp( const utf8* lhs, const utf8* rhs, const uint32 stringLength )
	{
		if ( lhs == rhs || stringLength == 0 )
			return 0;
		if ( lhs == nullptr )
			return -1;
		if ( rhs == nullptr )
			return 1;
		return std::strncmp( lhs, rhs, stringLength );
	}

	int32 StringUtil::strncmp( const utf16* lhs, const utf16* rhs, const uint32 stringLength )
	{
		if ( lhs == rhs || stringLength == 0 )
			return 0;
		if ( lhs == nullptr )
			return -1;
		if ( rhs == nullptr )
			return 1;
		return std::wcsncmp( lhs, rhs, stringLength );
	}

	void StringUtil::strncpy( utf8* pDestination, const utf8* pSource, const uint32 length )
	{
#if defined( SW_PLATFORM_WINDOWS )
		strncpy_s( pDestination, length, pSource, length );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		::strncpy( pDestination, pSource, length );
#else
	#error "Unsupported platform"
#endif
	}

	void StringUtil::strncpy( utf16* pDestination, const utf16* pSource, const uint32 length )
	{
#if defined( SW_PLATFORM_WINDOWS )
		wcsncpy_s( pDestination, length, pSource, length );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		::wcsncpy( pDestination, pSource, length );
#else
	#error "Unsupported platform"
#endif
	}

	const utf8* StringUtil::strstr( const utf8* str, const utf8* substr )
	{
		if ( str == nullptr || substr == nullptr )
			return nullptr;
		return ::strstr( str, substr );
	}

	const utf16* StringUtil::strstr( const utf16* str, const utf16* substr )
	{
		if ( str == nullptr || substr == nullptr )
			return nullptr;
		return wcsstr( str, substr );
	}

	const utf8* StringUtil::stristr( const utf8* str, const utf8* substr )
	{
		if ( str == nullptr || substr == nullptr )
			return nullptr;

		const utf8*	 start	   = str;
		const uint32 substrLen = strlen( substr );

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
		const uint32 substrLen = strlen( substr );

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
		if ( str == nullptr )
			return nullptr;
		return ::strchr( str, c );
	}

	const utf16* StringUtil::strchr( const utf16* str, const utf16 c )
	{
		if ( str == nullptr )
			return nullptr;
		return wcschr( str, c );
	}

	int32 StringUtil::atoi( const utf8* str )
	{
		if ( isNullOrEmpty( str ) )
			return 0;
		return static_cast<int32>( std::strtol( str, nullptr, 10 ) );
	}

	int32 StringUtil::atoi( const utf16* str )
	{
		if ( isNullOrEmpty( str ) )
			return 0;
		return static_cast<int32>( std::wcstol( str, nullptr, 10 ) );
	}

	int64 StringUtil::atoll( const utf8* str )
	{
		if ( isNullOrEmpty( str ) )
			return 0;
		return std::strtoll( str, nullptr, 10 );
	}

	int64 StringUtil::atoll( const utf16* str )
	{
		if ( isNullOrEmpty( str ) )
			return 0;
		return std::wcstoll( str, nullptr, 10 );
	}

	bool StringUtil::parseBool( string_view token, bool fallback )
	{
		const string_view trimmed = trim( token );
		if ( trimmed.empty() )
			return fallback;
		if ( trimmed == "1" || equalsIgnoreCase( trimmed, "true" ) || equalsIgnoreCase( trimmed, "yes" ) ||
			 equalsIgnoreCase( trimmed, "on" ) )
			return true;
		if ( trimmed == "0" || equalsIgnoreCase( trimmed, "false" ) || equalsIgnoreCase( trimmed, "no" ) ||
			 equalsIgnoreCase( trimmed, "off" ) )
			return false;
		return fallback;
	}

	float64 StringUtil::atof( const utf8* str )
	{
		if ( isNullOrEmpty( str ) )
			return 0.0;
		return std::strtod( str, nullptr );
	}

	float64 StringUtil::atof( const utf16* str )
	{
		if ( isNullOrEmpty( str ) )
			return 0.0;
		return std::wcstod( str, nullptr );
	}

	float32 StringUtil::strtof( const utf8* str, utf8** endPtr )
	{
		if ( isNullOrEmpty( str ) )
		{
			if ( endPtr != nullptr )
				*endPtr = const_cast<utf8*>( str );
			return 0.0f;
		}
		return std::strtof( str, endPtr );
	}

	float32 StringUtil::strtof( const utf16* str, utf16** endPtr )
	{
		if ( isNullOrEmpty( str ) )
		{
			if ( endPtr != nullptr )
				*endPtr = const_cast<utf16*>( str );
			return 0.0f;
		}
		return std::wcstof( str, endPtr );
	}

	float64 StringUtil::strtod( const utf8* str, utf8** endPtr )
	{
		if ( isNullOrEmpty( str ) )
		{
			if ( endPtr != nullptr )
				*endPtr = const_cast<utf8*>( str );
			return 0.0;
		}
		return std::strtod( str, endPtr );
	}

	float64 StringUtil::strtod( const utf16* str, utf16** endPtr )
	{
		if ( isNullOrEmpty( str ) )
		{
			if ( endPtr != nullptr )
				*endPtr = const_cast<utf16*>( str );
			return 0.0;
		}
		return std::wcstod( str, endPtr );
	}

	int64 StringUtil::strtoll( const utf8* str, utf8** endPtr, int32 base )
	{
		if ( isNullOrEmpty( str ) )
		{
			if ( endPtr != nullptr )
				*endPtr = const_cast<utf8*>( str );
			return 0;
		}
		return std::strtoll( str, endPtr, base );
	}

	int64 StringUtil::strtoll( const utf16* str, utf16** endPtr, int32 base )
	{
		if ( isNullOrEmpty( str ) )
		{
			if ( endPtr != nullptr )
				*endPtr = const_cast<utf16*>( str );
			return 0;
		}
		return std::wcstoll( str, endPtr, base );
	}

	uint64 StringUtil::strtoull( const utf8* str, utf8** endPtr, int32 base )
	{
		if ( isNullOrEmpty( str ) )
		{
			if ( endPtr != nullptr )
				*endPtr = const_cast<utf8*>( str );
			return 0;
		}
		return std::strtoull( str, endPtr, base );
	}

	uint64 StringUtil::strtoull( const utf16* str, utf16** endPtr, int32 base )
	{
		if ( isNullOrEmpty( str ) )
		{
			if ( endPtr != nullptr )
				*endPtr = const_cast<utf16*>( str );
			return 0;
		}
		return std::wcstoull( str, endPtr, base );
	}

	string StringUtil::toString( const utf16* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		size_t requiredSize{ 0 };
#if defined( SW_PLATFORM_WINDOWS )
		wcstombs_s( &requiredSize, nullptr, 0, input, 0 );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		requiredSize = wcstombs( nullptr, input, 0 );
		if ( requiredSize != static_cast<size_t>( -1 ) )
			++requiredSize; // include null terminator like wcstombs_s
#else
	#error "Unsupported platform"
#endif
		if ( requiredSize == 0 || requiredSize == static_cast<size_t>( -1 ) )
			return {};

		string buffer( requiredSize - 1, '\0' );
#if defined( SW_PLATFORM_WINDOWS )
		wcstombs_s( nullptr, buffer.data(), requiredSize, input, requiredSize );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		wcstombs( buffer.data(), input, requiredSize );
#else
	#error "Unsupported platform"
#endif

		return buffer;
	}

	wstring StringUtil::toWString( const utf8* input )
	{
		if ( isNullOrEmpty( input ) )
			return {};

		size_t requiredSize{ 0 };
#if defined( SW_PLATFORM_WINDOWS )
		mbstowcs_s( &requiredSize, nullptr, 0, input, 0 );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		requiredSize = mbstowcs( nullptr, input, 0 );
		if ( requiredSize != static_cast<size_t>( -1 ) )
			++requiredSize;
#else
	#error "Unsupported platform"
#endif
		if ( requiredSize == 0 || requiredSize == static_cast<size_t>( -1 ) )
			return {};

		wstring buffer( requiredSize - 1, L'\0' );
#if defined( SW_PLATFORM_WINDOWS )
		mbstowcs_s( nullptr, buffer.data(), requiredSize, input, requiredSize );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		mbstowcs( buffer.data(), input, requiredSize );
#else
	#error "Unsupported platform"
#endif

		return buffer;
	}

	bool StringUtil::isValidUTF8( const utf8* input )
	{
		if ( input == nullptr )
			return false;
		return StringUtilInternal::isValidUtf8( reinterpret_cast<const uint8*>( input ), strlen( input ) );
	}

	StringChangeSpan StringUtil::makeChangeSpan( string_view before, string_view after )
	{
		StringChangeSpan span{};
		const size_t	 beforeSize = before.size();
		const size_t	 afterSize	= after.size();
		const size_t	 minSize	= ( beforeSize < afterSize ) ? beforeSize : afterSize;
		size_t			 prefix		= 0;
		while ( prefix < minSize && before[prefix] == after[prefix] )
			++prefix;

		size_t suffix = 0;
		while ( suffix < ( beforeSize - prefix ) && suffix < ( afterSize - prefix ) &&
				before[beforeSize - 1 - suffix] == after[afterSize - 1 - suffix] )
			++suffix;

		span._prefixLength = static_cast<uint32>( prefix );
		span._suffixLength = static_cast<uint32>( suffix );
		span._removed	   = string{ before.substr( prefix, beforeSize - prefix - suffix ) };
		span._added		   = string{ after.substr( prefix, afterSize - prefix - suffix ) };
		return span;
	}

	string StringUtil::reconstructBefore( const StringChangeSpan& span, string_view afterText )
	{
		const size_t prefixLength = static_cast<size_t>( span._prefixLength );
		const size_t suffixLength = static_cast<size_t>( span._suffixLength );
		if ( afterText.size() < prefixLength + suffixLength )
			return string{ afterText };
		string result;
		result.reserve( prefixLength + span._removed.size() + suffixLength );
		result.append( afterText.data(), prefixLength );
		result.append( span._removed );
		if ( suffixLength > 0 )
			result.append( afterText.data() + ( afterText.size() - suffixLength ), suffixLength );
		return result;
	}

	string StringUtil::reconstructAfter( const StringChangeSpan& span, string_view beforeText )
	{
		const size_t prefixLength = static_cast<size_t>( span._prefixLength );
		const size_t suffixLength = static_cast<size_t>( span._suffixLength );
		if ( beforeText.size() < prefixLength + suffixLength )
			return string{ beforeText };
		string result;
		result.reserve( prefixLength + span._added.size() + suffixLength );
		result.append( beforeText.data(), prefixLength );
		result.append( span._added );
		if ( suffixLength > 0 )
			result.append( beforeText.data() + ( beforeText.size() - suffixLength ), suffixLength );
		return result;
	}
} // namespace sw
