/**
 * @file StringUtil.h
 * @brief UTF 변환·해시·분할·트림 등 문자열 유틸 (입력은 null-terminated utf8* / utf16*)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{

	// ------------------------------------------------------------------------------
	// 1) to_string — 숫자 → sw::string (StringUtil 카테고리, 자유 함수)
	// ------------------------------------------------------------------------------
	/** @brief int32 를 십진 문자열로 바꿉니다. */
	SW_API string to_string( int32 value );
	/** @brief uint32 를 십진 문자열로 바꿉니다. */
	SW_API string to_string( uint32 value );
	/** @brief int64 를 십진 문자열로 바꿉니다. */
	SW_API string to_string( int64 value );
	/** @brief uint64 를 십진 문자열로 바꿉니다. */
	SW_API string to_string( uint64 value );
	/** @brief float32 를 십진 문자열로 바꿉니다. */
	SW_API string to_string( float32 value );
	/** @brief float64 를 십진 문자열로 바꿉니다. */
	SW_API string to_string( float64 value );

	/** @brief 두 문자열의 공통 접두·접미를 뺀 변경 구간 (텍스트 Undo/패치용) */
	struct StringChangeSpan
	{
		string _removed;
		string _added;
		uint32 _prefixLength{ 0 };
		uint32 _suffixLength{ 0 };
	};

	// ------------------------------------------------------------------------------
	// 2) StringUtil — UTF 변환 · 해시 · 분할 · 트림 (전부 static)
	// ------------------------------------------------------------------------------
	struct SW_API StringUtil
	{
		static constexpr const utf8*  kWhiteSpace  = " \n\r\t\f\v";
		static constexpr const utf16* kWhiteSpaceW = L" \n\r\t\f\v";

		/** @brief 입력된 UTF-8 문자열이 null이거나 비어 있는지 확인합니다. */
		static bool isNullOrEmpty( const utf8* str );
		/** @brief 입력된 UTF-16 문자열이 null이거나 비어 있는지 확인합니다. */
		static bool isNullOrEmpty( const utf16* str );

		/** @brief 입력된 바이트 시퀀스가 유효한 UTF-8 형식인지 검사합니다. */
		static bool isValidUTF8( const utf8* input );

		/** @brief UTF-8 문자열을 UTF-16(sw::wstring) 형식으로 변환합니다. */
		static wstring utf8ToUtf16( const utf8* input );
		/** @brief UTF-16 문자열을 UTF-8(sw::string) 형식으로 변환합니다. */
		static string utf16ToUtf8( const utf16* input );

		/** @brief UTF-16 문자열을 시스템 로캘(Locale) 문자열로 변환합니다. */
		static string utf16ToLocale( const utf16* input );
		/** @brief 시스템 로캘 문자열을 UTF-16 형식으로 변환합니다. */
		static wstring localeToUtf16( const utf8* input );

		/** @brief 시스템 로캘 문자열을 UTF-8 형식으로 변환합니다. */
		static string localeToUtf8( const utf8* input );
		/** @brief UTF-8 문자열을 시스템 로캘 형식으로 변환합니다. */
		static string utf8ToLocale( const utf8* input );

		/** @brief 단일 UTF-8 문자를 소문자로 변환합니다. */
		static constexpr utf8 toLowerChar( const utf8 c ) { return ( c >= 'A' && c <= 'Z' ) ? static_cast<utf8>( c + ( 'a' - 'A' ) ) : c; }

		/** @brief 단일 UTF-16 문자를 소문자로 변환합니다. */
		static constexpr utf16 toLowerChar( const utf16 c ) { return ( c >= L'A' && c <= L'Z' ) ? static_cast<utf16>( c + ( L'a' - L'A' ) ) : c; }

		/** @brief 단일 UTF-8 문자를 대문자로 변환합니다. */
		static constexpr utf8 toUpperChar( const utf8 c ) { return ( c >= 'a' && c <= 'z' ) ? static_cast<utf8>( c - ( 'a' - 'A' ) ) : c; }

		/** @brief 단일 UTF-16 문자를 대문자로 변환합니다. */
		static constexpr utf16 toUpperChar( const utf16 c ) { return ( c >= L'a' && c <= L'z' ) ? static_cast<utf16>( c - ( L'a' - L'A' ) ) : c; }

		/** @brief UTF-8 문자열 전체를 대문자로 변환합니다. */
		static string toUpper( const utf8* input );
		/** @brief UTF-16 문자열 전체를 대문자로 변환합니다. */
		static wstring toUpper( const utf16* input );
		/** @brief UTF-8 문자열 전체를 소문자로 변환합니다. */
		static string toLower( const utf8* input );
		/** @brief UTF-16 문자열 전체를 소문자로 변환합니다. */
		static wstring toLower( const utf16* input );

		/** @brief 전/후 텍스트의 공통 접두·접미를 빼고 중간만 남깁니다. */
		static StringChangeSpan makeChangeSpan( string_view before, string_view after );
		/** @brief after 텍스트와 스팬으로 편집 전 본문을 만듭니다. */
		static string reconstructBefore( const StringChangeSpan& span, string_view afterText );
		/** @brief before 텍스트와 스팬으로 편집 후 본문을 만듭니다. */
		static string reconstructAfter( const StringChangeSpan& span, string_view beforeText );

		/** @brief ASCII 대소문자 무시 동등 비교 (키/태그/속성 이름용). 값 비교에는 쓰지 말 것. */
		static bool equalsIgnoreCase( const utf8* lhs, const utf8* rhs );
		/** @brief 대소문자 무시 동등 비교를 합니다. */
		static bool equalsIgnoreCase( const utf16* lhs, const utf16* rhs );
		/** @brief string_view 대소문자 무시 동등 비교 */
		static bool equalsIgnoreCase( string_view lhs, string_view rhs );
		/** @brief wstring_view 대소문자 무시 동등 비교 */
		static bool equalsIgnoreCase( wstring_view lhs, wstring_view rhs );

		/** @brief 문자열 앞(시작 부분)의 공백(Whitespace) 문자를 모두 제거합니다. */
		static string trimStart( const utf8* input );
		/** @brief 앞 공백을 제거합니다. */
		static wstring trimStart( const utf16* input );
		/** @brief string_view의 앞 공백을 제거한 뷰를 반환합니다. */
		static string_view trimStart( string_view input );
		/** @brief wstring_view의 앞 공백을 제거한 뷰를 반환합니다. */
		static wstring_view trimStart( wstring_view input );

		/** @brief 문자열 뒤(끝 부분)의 공백 문자를 모두 제거합니다. */
		static string trimEnd( const utf8* input );
		/** @brief 뒤 공백을 제거합니다. */
		static wstring trimEnd( const utf16* input );
		/** @brief string_view의 뒤 공백을 제거한 뷰를 반환합니다. */
		static string_view trimEnd( string_view input );
		/** @brief wstring_view의 뒤 공백을 제거한 뷰를 반환합니다. */
		static wstring_view trimEnd( wstring_view input );

		/** @brief 문자열 양 끝의 공백 문자를 모두 제거합니다. */
		static string trim( const utf8* input );
		/** @brief 양끝 공백을 제거합니다. */
		static wstring trim( const utf16* input );
		/** @brief string_view의 앞뒤 공백을 제거한 뷰를 반환합니다 (Zero Allocation). */
		static string_view trim( string_view input );
		/** @brief wstring_view의 앞뒤 공백을 제거한 뷰를 반환합니다 (Zero Allocation). */
		static wstring_view trim( wstring_view input );

		/** @brief 문자열의 길이(문자 수)를 반환합니다. */
		static uint32 strlen( const utf8* str );
		/** @brief 문자열 길이를 반환합니다. */
		static uint32 strlen( const utf16* str );

		/** @brief 대소문자를 구분하지 않고 처음 주어진 길이(length)만큼 두 문자열을 비교합니다. */
		static int32 strnicmp( const utf8* lhs, const utf8* rhs, uint32 stringLength );
		/** @brief 대소문자 무시로 길이만큼 비교합니다. */
		static int32 strnicmp( const utf16* lhs, const utf16* rhs, uint32 stringLength );

		/** @brief 두 문자열을 사전순으로 비교합니다 (동일하면 0). */
		static int32 strcmp( const utf8* lhs, const utf8* rhs );
		/** @brief 사전순으로 비교합니다. */
		static int32 strcmp( const utf16* lhs, const utf16* rhs );

		/** @brief 두 문자열을 지정된 길이(length)만큼 사전순으로 비교합니다 (동일하면 0). */
		static int32 strncmp( const utf8* lhs, const utf8* rhs, uint32 stringLength );
		/** @brief 지정된 길이만큼 사전순으로 비교합니다. */
		static int32 strncmp( const utf16* lhs, const utf16* rhs, uint32 stringLength );

		/** @brief 지정된 길이(length)만큼 문자를 안전하게 복사합니다. */
		static void strncpy( utf8* pDestination, const utf8* pSource, uint32 length );
		/** @brief 문자를 안전하게 복사합니다. */
		static void strncpy( utf16* pDestination, const utf16* pSource, uint32 length );

		/** @brief 부분 문자열(substr)이 처음으로 나타나는 위치를 반환합니다. */
		static const utf8* strstr( const utf8* str, const utf8* substr );
		/** @brief 부분 문자열이 처음 나타나는 위치를 반환합니다. */
		static const utf16* strstr( const utf16* str, const utf16* substr );

		/** @brief 대소문자를 무시하고 부분 문자열이 처음으로 나타나는 위치를 반환합니다. */
		static const utf8* stristr( const utf8* str, const utf8* substr );
		/** @brief 대소문자 무시로 부분 문자열을 찾습니다. */
		static const utf16* stristr( const utf16* str, const utf16* substr );

		/** @brief 문자열에서 특정 문자가 처음으로 나타나는 위치를 반환합니다. */
		static const utf8* strchr( const utf8* str, utf8 c );
		/** @brief 문자가 처음 나타나는 위치를 반환합니다. */
		static const utf16* strchr( const utf16* str, utf16 c );

		/** @brief 정수를 파싱합니다. 실패·빈 입력은 0입니다. */
		static int32 atoi( const utf8* str );
		/** @brief 정수를 파싱합니다. 실패 시 0입니다. */
		static int32 atoi( const utf16* str );

		/** @brief 64비트 정수를 파싱합니다. 실패·빈 입력은 0입니다. */
		static int64 atoll( const utf8* str );
		/** @brief 64비트 정수를 파싱합니다. 실패 시 0입니다. */
		static int64 atoll( const utf16* str );

		/** @brief 실수를 파싱합니다. 실패·빈 입력은 0입니다. */
		static float64 atof( const utf8* str );
		/** @brief 실수를 파싱합니다. 실패 시 0입니다. */
		static float64 atof( const utf16* str );

		/**
		 * @brief true/false/1/0/yes/no/on/off 토큰을 bool로 파싱합니다 (대소문자 무시).
		 * @details 트림 후 매칭. 빈 입력이거나 알 수 없으면 fallback.
		 */
		static bool parseBool( string_view token, bool fallback = false );

		/** @brief 32비트 실수를 파싱합니다. */
		static float32 strtof( const utf8* str, utf8** endPtr = nullptr );
		/** @brief 32비트 실수를 파싱합니다. */
		static float32 strtof( const utf16* str, utf16** endPtr = nullptr );

		/** @brief 64비트 실수를 파싱합니다. */
		static float64 strtod( const utf8* str, utf8** endPtr = nullptr );
		/** @brief 64비트 실수를 파싱합니다. */
		static float64 strtod( const utf16* str, utf16** endPtr = nullptr );

		/**
		 * @brief 정수를 파싱합니다. base 0이면 0x/8진/10진을 자동 판별합니다.
		 * @param endPtr 선택. 파싱이 끝난 첫 문자를 가리킵니다(nullptr 허용).
		 */
		static int64 strtoll( const utf8* str, utf8** endPtr = nullptr, int32 base = 0 );
		/** @brief 정수를 파싱합니다(기수 자동 판별). */
		static int64 strtoll( const utf16* str, utf16** endPtr = nullptr, int32 base = 0 );

		/** @brief 부호 없는 정수를 파싱합니다. */
		static uint64 strtoull( const utf8* str, utf8** endPtr = nullptr, int32 base = 0 );
		/** @brief 부호 없는 정수를 파싱합니다. */
		static uint64 strtoull( const utf16* str, utf16** endPtr = nullptr, int32 base = 0 );

		static constexpr uint64 kOffset64 = 14695981039346656037ULL;
		static constexpr uint64 kPrime64  = 1099511628211ULL;
		static constexpr uint32 kOffset32 = 2166136261U;
		static constexpr uint32 kPrime32  = 16777619U;

		/** @brief 64비트 FNV-1a 해시를 계산합니다 (포인터 + 길이). */
		template <typename CharT>
		static constexpr uint64 computeHash64( const CharT* str, const size_t length, const bool bIgnoreCase = true, const uint64 seed = kOffset64 ) noexcept
		{
			uint64 hash = seed;
			for ( size_t charIndex = 0; charIndex < length; ++charIndex )
			{
				const uint64 c = bIgnoreCase ? static_cast<uint64>( toLowerChar( str[charIndex] ) ) : static_cast<uint64>( static_cast<uint8>( str[charIndex] ) );
				hash		   = ( hash ^ c ) * kPrime64;
			}
			return hash;
		}

		/** @brief 64비트 FNV-1a 해시를 계산합니다 (string, string_view, fixed_string 등 지원). */
		template <typename StringType, typename = std::enable_if_t<std::is_class_v<StringType>>>
		static constexpr uint64 computeHash64( const StringType& str, const bool bIgnoreCase = true, const uint64 seed = kOffset64 ) noexcept
		{
			return computeHash64( str.data(), str.size(), bIgnoreCase, seed );
		}

		/** @brief 32비트 FNV-1a 해시를 계산합니다 (포인터 + 길이). */
		template <typename CharT>
		static constexpr uint32 computeHash32( const CharT* str, const size_t length, const bool bIgnoreCase = true, const uint32 seed = kOffset32 ) noexcept
		{
			uint32 hash = seed;
			for ( size_t charIndex = 0; charIndex < length; ++charIndex )
			{
				const uint32 c = bIgnoreCase ? static_cast<uint32>( toLowerChar( str[charIndex] ) ) : static_cast<uint32>( static_cast<uint8>( str[charIndex] ) );
				hash		   = ( hash ^ c ) * kPrime32;
			}
			return hash;
		}

		/** @brief 32비트 FNV-1a 해시를 계산합니다 (string, string_view, fixed_string 등 지원). */
		template <typename StringType, typename = std::enable_if_t<std::is_class_v<StringType>>>
		static constexpr uint32 computeHash32( const StringType& str, const bool bIgnoreCase = true, const uint32 seed = kOffset32 ) noexcept
		{
			return computeHash32( str.data(), str.size(), bIgnoreCase, seed );
		}

		/** @brief 임의의 바이너리 버퍼에 대해 IEEE 802.3 CRC32 체크섬을 계산합니다. */
		static constexpr uint32 computeCrc32( const void* pData, const size_t length ) noexcept
		{
			if ( pData == nullptr || length == 0 )
				return 0;

			const uint8* pBytes = static_cast<const uint8*>( pData );
			uint32		 crc	= 0xFFFFFFFFu;
			for ( size_t byteIndex = 0; byteIndex < length; ++byteIndex )
			{
				crc ^= static_cast<uint32>( pBytes[byteIndex] );
				for ( uint32 bitIndex = 0; bitIndex < 8; ++bitIndex )
				{
					const uint32 mask = static_cast<uint32>( -( static_cast<int32>( crc & 1u ) ) );
					crc				  = ( crc >> 1 ) ^ ( 0xEDB88320u & mask );
				}
			}
			return ~crc;
		}

	private:
		/** @brief 내부 UTF-16 변환 헬퍼입니다. */
		static string toString( const utf16* input );
		/** @brief 내부 UTF-8 변환 헬퍼입니다. */
		static wstring toWString( const utf8* input );
	};
} // namespace sw
