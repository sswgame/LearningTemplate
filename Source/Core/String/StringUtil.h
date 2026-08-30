/**
 * @file StringUtil.h
 * @brief UTF 변환·해시·분할·트림 등 문자열 유틸 (입력은 null-terminated utf8* / utf16*)
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"
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

	namespace StringUtilCrcInternal
	{
		/** @brief IEEE 802.3 CRC32 테이블을 컴파일 타임에 생성합니다. */
		constexpr array<uint32, constant::kMaxBuffer256> makeCrc32TableInternal() noexcept
		{
			array<uint32, constant::kMaxBuffer256> arrTable{};
			for ( uint32 index = 0; index < constant::kMaxBuffer256; ++index )
			{
				uint32 crc = index;
				for ( uint32 bitIndex = 0; bitIndex < 8; ++bitIndex )
				{
					crc = ( crc & 1u ) != 0 ? ( ( crc >> 1 ) ^ 0xEDB88320u ) : ( crc >> 1 );
				}
				arrTable[index] = crc;
			}
			return arrTable;
		}
	} // namespace StringUtilCrcInternal

	// ------------------------------------------------------------------------------
	// 2) StringUtil — UTF 변환 · 해시 · 분할 · 트림 (전부 static)
	// ------------------------------------------------------------------------------
	struct SW_API StringUtil
	{
		static constexpr const utf8*  kWhiteSpace  = " \n\r\t\f\v";
		static constexpr const utf16* kWhiteSpaceW = L" \n\r\t\f\v";

		/** @brief 입력된 UTF-8 문자열이 null이거나 비어 있는지 확인합니다. */
		static bool isNullOrEmpty( const utf8* pStr );
		/** @brief 입력된 UTF-16 문자열이 null이거나 비어 있는지 확인합니다. */
		static bool isNullOrEmpty( const utf16* pStr );

		/** @brief 입력된 바이트 시퀀스가 유효한 UTF-8 형식인지 검사합니다. */
		static bool isValidUTF8( const utf8* pInput );

		/** @brief UTF-8 문자열을 UTF-16(sw::wstring) 형식으로 변환합니다. */
		static wstring utf8ToUtf16( const utf8* pInput );
		/** @brief UTF-16 문자열을 UTF-8(sw::string) 형식으로 변환합니다. */
		static string utf16ToUtf8( const utf16* pInput );

		/** @brief UTF-16 문자열을 시스템 로캘(Locale) 문자열로 변환합니다. */
		static string utf16ToLocale( const utf16* pInput );
		/** @brief 시스템 로캘 문자열을 UTF-16 형식으로 변환합니다. */
		static wstring localeToUtf16( const utf8* pInput );

		/** @brief 시스템 로캘 문자열을 UTF-8 형식으로 변환합니다. */
		static string localeToUtf8( const utf8* pInput );
		/** @brief UTF-8 문자열을 시스템 로캘 형식으로 변환합니다. */
		static string utf8ToLocale( const utf8* pInput );

		/** @brief 단일 UTF-8 문자를 소문자로 변환합니다. */
		static constexpr utf8 toLowerChar( const utf8 ch ) { return ( 'A' <= ch && ch <= 'Z' ) ? static_cast<utf8>( ch + ( 'a' - 'A' ) ) : ch; }

		/** @brief 단일 UTF-16 문자를 소문자로 변환합니다. */
		static constexpr utf16 toLowerChar( const utf16 ch ) { return ( L'A' <= ch && ch <= L'Z' ) ? static_cast<utf16>( ch + ( L'a' - L'A' ) ) : ch; }

		/** @brief 단일 UTF-8 문자를 대문자로 변환합니다. */
		static constexpr utf8 toUpperChar( const utf8 ch ) { return ( 'a' <= ch && ch <= 'z' ) ? static_cast<utf8>( ch - ( 'a' - 'A' ) ) : ch; }

		/** @brief 단일 UTF-16 문자를 대문자로 변환합니다. */
		static constexpr utf16 toUpperChar( const utf16 ch ) { return ( L'a' <= ch && ch <= L'z' ) ? static_cast<utf16>( ch - ( L'a' - L'A' ) ) : ch; }

		/** @brief UTF-8 문자열 전체를 대문자로 변환합니다. */
		static string toUpper( const utf8* pInput );
		/** @brief UTF-16 문자열 전체를 대문자로 변환합니다. */
		static wstring toUpper( const utf16* pInput );
		/** @brief UTF-8 문자열 전체를 소문자로 변환합니다. */
		static string toLower( const utf8* pInput );
		/** @brief UTF-16 문자열 전체를 소문자로 변환합니다. */
		static wstring toLower( const utf16* pInput );

		/** @brief 전/후 텍스트의 공통 접두·접미를 빼고 중간만 남깁니다. */
		static StringChangeSpan makeChangeSpan( string_view before, string_view after );
		/** @brief after 텍스트와 스팬으로 편집 전 본문을 만듭니다. */
		static string reconstructBefore( const StringChangeSpan& span, string_view afterText );
		/** @brief before 텍스트와 스팬으로 편집 후 본문을 만듭니다. */
		static string reconstructAfter( const StringChangeSpan& span, string_view beforeText );

		/** @brief 문자열 내의 특정 문자를 다른 문자로 치환합니다. */
		static void replaceChar( string& inoutStr, utf8 fromChar, utf8 toChar );
		/** @brief 문자열 내의 특정 문자를 다른 문자로 치환합니다 (UTF-16). */
		static void replaceChar( wstring& inoutStr, utf16 fromChar, utf16 toChar );
		/** @brief 특정 부분 문자열을 다른 문자열로 치환한 새 문자열을 반환합니다. */
		static string replace( string_view input, string_view from, string_view to );

		/** @brief 두 문자열이 일치하는지 확인합니다 (대소문자 무시 옵션). */
		static bool equals( string_view lhs, string_view rhs, bool bIgnoreCase = false ) noexcept;
		/** @brief 두 문자열이 일치하는지 확인합니다 (대소문자 무시 옵션). */
		static bool equals( wstring_view lhs, wstring_view rhs, bool bIgnoreCase = false ) noexcept;
		/** @brief 두 C 문자열이 일치하는지 확인합니다 (nullptr 안전, 대소문자 무시 옵션). */
		static bool equals( const utf8* pLhs, const utf8* pRhs, bool bIgnoreCase = false ) noexcept;
		/** @brief 두 C 문자열이 일치하는지 확인합니다 (nullptr 안전, 대소문자 무시 옵션). */
		static bool equals( const utf16* pLhs, const utf16* pRhs, bool bIgnoreCase = false ) noexcept;

		/** @brief 두 문자열을 사전순으로 3-Way 비교합니다 (< 0, == 0, > 0). */
		static int32 compare( string_view lhs, string_view rhs, bool bIgnoreCase = false ) noexcept;
		/** @brief 두 문자열을 사전순으로 3-Way 비교합니다 (< 0, == 0, > 0). */
		static int32 compare( wstring_view lhs, wstring_view rhs, bool bIgnoreCase = false ) noexcept;
		/** @brief 두 C 문자열을 사전순으로 3-Way 비교합니다 (nullptr 안전, < 0, == 0, > 0). */
		static int32 compare( const utf8* pLhs, const utf8* pRhs, bool bIgnoreCase = false ) noexcept;
		/** @brief 두 C 문자열을 사전순으로 3-Way 비교합니다 (nullptr 안전, < 0, == 0, > 0). */
		static int32 compare( const utf16* pLhs, const utf16* pRhs, bool bIgnoreCase = false ) noexcept;

		/** @brief 문자열이 지정된 접두사(prefix)로 시작하는지 확인합니다 (Zero Allocation). */
		static bool startsWith( string_view str, string_view prefix, bool bIgnoreCase = false ) noexcept;
		/** @brief 문자열이 지정된 접미사(suffix)로 끝나는지 확인합니다 (Zero Allocation). */
		static bool endsWith( string_view str, string_view suffix, bool bIgnoreCase = false ) noexcept;

		/** @brief 문자열 앞(시작 부분)의 공백(Whitespace) 문자를 모두 제거합니다. */
		static string trimStart( const utf8* pInput );
		/** @brief 앞 공백을 제거합니다. */
		static wstring trimStart( const utf16* pInput );
		/** @brief string_view의 앞 공백을 제거한 뷰를 반환합니다. */
		static string_view trimStart( string_view input );
		/** @brief wstring_view의 앞 공백을 제거한 뷰를 반환합니다. */
		static wstring_view trimStart( wstring_view input );

		/** @brief 문자열 뒤(끝 부분)의 공백 문자를 모두 제거합니다. */
		static string trimEnd( const utf8* pInput );
		/** @brief 뒤 공백을 제거합니다. */
		static wstring trimEnd( const utf16* pInput );
		/** @brief string_view의 뒤 공백을 제거한 뷰를 반환합니다. */
		static string_view trimEnd( string_view input );
		/** @brief wstring_view의 뒤 공백을 제거한 뷰를 반환합니다. */
		static wstring_view trimEnd( wstring_view input );

		/** @brief 문자열 양 끝의 공백 문자를 모두 제거합니다. */
		static string trim( const utf8* pInput );
		/** @brief 양끝 공백을 제거합니다. */
		static wstring trim( const utf16* pInput );
		/** @brief string_view의 앞뒤 공백을 제거한 뷰를 반환합니다 (Zero Allocation). */
		static string_view trim( string_view input );
		/** @brief wstring_view의 앞뒤 공백을 제거한 뷰를 반환합니다 (Zero Allocation). */
		static wstring_view trim( wstring_view input );

		/** @brief 문자열의 길이(문자 수)를 반환합니다. */
		static uint32 strlen( const utf8* pStr );
		/** @brief 문자열 길이를 반환합니다. */
		static uint32 strlen( const utf16* pStr );

		/** @brief 지정된 길이(length)만큼 문자를 안전하게 복사합니다. */
		static void strncpy( utf8* pOutDest, const utf8* pSource, uint32 length );
		/** @brief 문자를 안전하게 복사합니다. */
		static void strncpy( utf16* pOutDest, const utf16* pSource, uint32 length );

		/** @brief 부분 문자열(substr)이 처음으로 나타나는 위치를 반환합니다. */
		static const utf8* strstr( const utf8* pStr, const utf8* pSubstr );
		/** @brief 부분 문자열이 처음 나타나는 위치를 반환합니다. */
		static const utf16* strstr( const utf16* pStr, const utf16* pSubstr );

		/** @brief 대소문자를 무시하고 부분 문자열이 처음으로 나타나는 위치를 반환합니다. */
		static const utf8* stristr( const utf8* pStr, const utf8* pSubstr );
		/** @brief 대소문자 무시로 부분 문자열을 찾습니다. */
		static const utf16* stristr( const utf16* pStr, const utf16* pSubstr );

		/** @brief 문자열에서 특정 문자가 처음으로 나타나는 위치를 반환합니다. */
		static const utf8* strchr( const utf8* pStr, utf8 ch );
		/** @brief 문자가 처음 나타나는 위치를 반환합니다. */
		static const utf16* strchr( const utf16* pStr, utf16 ch );

		/**
		 * @brief true/false/1/0/yes/no/on/off 토큰을 bool로 파싱합니다 (대소문자 무시).
		 * @details 트림 후 매칭. 빈 입력이거나 알 수 없으면 fallback.
		 */
		static bool parseBool( string_view token, bool bFallback = false );

		/**
		 * @brief string_view 토큰을 32비트 실수로 파싱합니다 (0-Alloc).
		 * @return 파싱 성공 시 true, 실패 시 false (outValue 미변경).
		 */
		static bool parseFloat( string_view token, float32& outValue );

		/**
		 * @brief string_view 토큰을 64비트 실수로 파싱합니다 (0-Alloc).
		 * @return 파싱 성공 시 true, 실패 시 false (outValue 미변경).
		 */
		static bool parseDouble( string_view token, float64& outValue );

		/**
		 * @brief string_view 토큰을 32비트 정수로 파싱합니다 (0-Alloc).
		 * @return 파싱 성공 시 true, 실패 시 false (outValue 미변경).
		 */
		static bool parseInt( string_view token, int32& outValue, int32 base = 10 );

		/**
		 * @brief string_view 토큰을 64비트 정수로 파싱합니다 (0-Alloc).
		 * @return 파싱 성공 시 true, 실패 시 false (outValue 미변경).
		 */
		static bool parseInt64( string_view token, int64& outValue, int32 base = 10 );

		/**
		 * @brief string_view 토큰을 64비트 부호 없는 정수로 파싱합니다 (0-Alloc).
		 * @return 파싱 성공 시 true, 실패 시 false (outValue 미변경).
		 */
		static bool parseUInt64( string_view token, uint64& outValue, int32 base = 10 );

		/**
		 * @brief 정수/부동소수점 숫자를 0-Alloc으로 버퍼에 고속 포맷팅하고 쓰여진 길이를 반환합니다.
		 * @param precisionOrBase 정수의 경우 진법(기본 10), 실수의 경우 고정 소수점 자릿수(기본 -1: 최단 표현).
		 */
		template <typename T>
		static uint32 formatNumber( utf8* pOutBuffer, uint32 capacity, T value, int32 precisionOrBase = -1 )
		{
			if ( pOutBuffer == nullptr || capacity == 0 )
				return 0;

			if constexpr ( std::is_floating_point_v<T> )
			{
				if ( precisionOrBase < 0 )
				{
					auto [pPtr, ec] = std::to_chars( pOutBuffer, pOutBuffer + capacity - 1, value );
					if ( ec == std::errc{} )
					{
						const uint32 len = static_cast<uint32>( pPtr - pOutBuffer );
						pOutBuffer[len]	 = '\0';
						return len;
					}
				}
				else
				{
					auto [pPtr, ec] = std::to_chars( pOutBuffer, pOutBuffer + capacity - 1, value, std::chars_format::fixed, precisionOrBase );
					if ( ec == std::errc{} )
					{
						const uint32 len = static_cast<uint32>( pPtr - pOutBuffer );
						pOutBuffer[len]	 = '\0';
						return len;
					}
				}
			}
			else if constexpr ( std::is_integral_v<T> || std::is_enum_v<T> )
			{
				const int32 base = ( precisionOrBase <= 0 ) ? 10 : precisionOrBase;
				auto [pPtr, ec]	 = std::to_chars( pOutBuffer, pOutBuffer + capacity - 1, value, base );
				if ( ec == std::errc{} )
				{
					const uint32 len = static_cast<uint32>( pPtr - pOutBuffer );
					pOutBuffer[len]	 = '\0';
					return len;
				}
			}
			pOutBuffer[0] = '\0';
			return 0;
		}

		static constexpr uint64 kOffset64 = 14695981039346656037ULL;
		static constexpr uint64 kPrime64  = 1099511628211ULL;
		static constexpr uint32 kOffset32 = 2166136261U;
		static constexpr uint32 kPrime32  = 16777619U;

		/** @brief 64비트 FNV-1a 해시를 계산합니다 (포인터 + 길이). */
		template <typename CharT>
		static constexpr uint64 computeHash64( const CharT* pStr, const size_t length, const bool bIgnoreCase = true, const uint64 seed = kOffset64 ) noexcept
		{
			uint64 hash = seed;
			for ( size_t charIndex = 0; charIndex < length; ++charIndex )
			{
				const uint64 c = bIgnoreCase ? static_cast<uint64>( toLowerChar( pStr[charIndex] ) ) : static_cast<uint64>( static_cast<uint8>( pStr[charIndex] ) );
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
		static constexpr uint32 computeHash32( const CharT* pStr, const size_t length, const bool bIgnoreCase = true, const uint32 seed = kOffset32 ) noexcept
		{
			uint32 hash = seed;
			for ( size_t charIndex = 0; charIndex < length; ++charIndex )
			{
				const uint32 c = bIgnoreCase ? static_cast<uint32>( toLowerChar( pStr[charIndex] ) ) : static_cast<uint32>( static_cast<uint8>( pStr[charIndex] ) );
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

	private:
		static constexpr array<uint32, constant::kMaxBuffer256> kArrCrc32Table = StringUtilCrcInternal::makeCrc32TableInternal();

	public:
		/** @brief 임의의 바이너리 버퍼에 대해 IEEE 802.3 CRC32 체크섬을 테이블 룩업으로 고속 계산합니다. */
		static constexpr uint32 computeCrc32( const void* pData, const size_t length ) noexcept
		{
			if ( pData == nullptr || length == 0 )
				return 0;

			const uint8* pBytes = static_cast<const uint8*>( pData );
			uint32		 crc	= 0xFFFFFFFFu;
			for ( size_t byteIndex = 0; byteIndex < length; ++byteIndex )
			{
				crc = ( crc >> 8 ) ^ kArrCrc32Table[( crc ^ pBytes[byteIndex] ) & 0xFFu];
			}
			return ~crc;
		}
	};
} // namespace sw
