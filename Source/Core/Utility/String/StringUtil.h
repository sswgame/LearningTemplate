#pragma once
/**
 * @file StringUtil.h
 * @brief UTF 변환·해시·분할·트림 등 문자열 유틸
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{

	struct StringUtil
	{
		static constexpr const utf8* kWhiteSpace = " \n\r\t\f\v";

		/**
		 * @brief null 또는 빈 문자열인지 반환합니다
		 */
		static bool isNullOrEmpty( const utf8* str );
		/**
		 * @brief null 또는 빈 문자열인지 반환합니다
		 */
		static bool isNullOrEmpty( const utf16* str );

		/**
		 * @brief UTF-8을 UTF-16으로 변환합니다
		 */
		static std::wstring utf8ToUtf16( const std::string_view& input );
		/**
		 * @brief UTF-16을 UTF-8으로 변환합니다
		 */
		static std::string utf16ToUtf8( const std::wstring_view& input );

		/**
		 * @brief UTF-16을 로케일 문자열로 변환합니다
		 */
		static std::string utf16ToLocale( const std::wstring_view& input );
		/**
		 * @brief 로케일 문자열을 UTF-16으로 변환합니다
		 */
		static std::wstring localeToUtf16( const std::string_view& input );

		/**
		 * @brief 로케일 문자열을 UTF-8으로 변환합니다
		 */
		static std::string localeToUtf8( const std::string_view& input );
		/**
		 * @brief UTF-8을 로케일 문자열로 변환합니다
		 */
		static std::string utf8ToLocale( const std::string_view& input );

		static constexpr utf8 toLowerChar( const utf8 c )
		{
			return ( c >= 'A' && c <= 'Z' ) ? static_cast<utf8>( c + ( 'a' - 'A' ) ) : c;
		}

		static constexpr utf16 toLowerChar( const utf16 c )
		{
			return ( c >= L'A' && c <= L'Z' ) ? static_cast<utf16>( c + ( L'a' - L'A' ) ) : c;
		}

		/**
		 * @brief 구분자로 분할합니다
		 */
		static std::vector<std::string> split( const std::string_view& input, const std::string_view& delimiterList );
		/**
		 * @brief 대문자로 변환합니다
		 */
		static std::string toUpper( const std::string_view& input );
		/**
		 * @brief 소문자로 변환합니다
		 */
		static std::string toLower( const std::string_view& input );

		/**
		 * @brief 앞 공백을 제거합니다
		 */
		static std::string trimStart( const std::string_view& input );
		/**
		 * @brief 뒤 공백을 제거합니다
		 */
		static std::string trimEnd( const std::string_view& input );
		/**
		 * @brief 양끝 공백을 제거합니다
		 */
		static std::string trim( const std::string_view& input );

		/**
		 * @brief 문자열 길이를 반환합니다
		 */
		static uint32 strlen( const utf8* str );
		/**
		 * @brief 문자열 길이를 반환합니다
		 */
		static uint32 strlen( const utf16* str );

		/**
		 * @brief 대소문자 무시 비교를 수행합니다
		 */
		static int32 strnicmp( const utf8* lhs, const utf8* rhs, const uint32 stringLength );
		/**
		 * @brief 대소문자 무시 비교를 수행합니다
		 */
		static int32 strnicmp( const utf16* lhs, const utf16* rhs, const uint32 stringLength );

		/**
		 * @brief 문자열을 비교합니다
		 */
		static int32 strcmp( const utf8* lhs, const utf8* rhs );
		/**
		 * @brief 문자열을 비교합니다
		 */
		static int32 strcmp( const utf16* lhs, const utf16* rhs );

		static const utf8*	strstr( const utf8* str, const utf8* substr );
		static const utf16* strstr( const utf16* str, const utf16* substr );

		static const utf8*	stristr( const utf8* str, const utf8* substr );
		static const utf16* stristr( const utf16* str, const utf16* substr );

		static const utf8*	strchr( const utf8* str, const utf8 c );
		static const utf16* strchr( const utf16* str, const utf16 c );

		static constexpr uint64 kOffset64 = 14695981039346656037ULL;
		static constexpr uint64 kPrime64  = 1099511628211ULL;
		static constexpr uint32 kOffset32 = 2166136261U;
		static constexpr uint32 kPrime32  = 16777619U;

		template <typename CharT>
		static constexpr uint64 computeHash64( const CharT* str, const size_t length, const bool bIgnoreCase = true ) noexcept
		{
			uint64 hash = kOffset64;
			for ( size_t i = 0; i < length; ++i )
			{
				const uint64 c = bIgnoreCase ? static_cast<uint64>( toLowerChar( str[i] ) ) : static_cast<uint64>( str[i] );
				hash		   = ( hash ^ c ) * kPrime64;
			}
			return hash;
		}

		template <typename CharT>
		static constexpr uint32 computeHash32( const CharT* str, const size_t length, const bool bIgnoreCase = true ) noexcept
		{
			uint32 hash = kOffset32;
			for ( size_t i = 0; i < length; ++i )
			{
				const uint32 c = bIgnoreCase ? static_cast<uint32>( toLowerChar( str[i] ) ) : static_cast<uint32>( str[i] );
				hash		   = ( hash ^ c ) * kPrime32;
			}
			return hash;
		}

	private:
		/**
		 * @brief 문자열로 변환합니다
		 */
		static std::string toString( const std::wstring_view& input );
		/**
		 * @brief 와이드 문자열로 변환합니다
		 */
		static std::wstring toWString( const std::string_view& input );
		/**
		 * @brief 유효한 UTF-8인지 검사합니다
		 */
		static bool isValidUTF8( const std::string_view input );
	};
} // namespace sw
