#pragma once
/**
 * @file StringUtil.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{

	struct StringUtil
	{
		static constexpr const utf8* kWhiteSpace = " \n\r\t\f\v";

		/**
		 * @brief isNullOrEmpty 처리를 수행합니다.
		 */
		static bool isNullOrEmpty( const utf8* str );
		/**
		 * @brief isNullOrEmpty 처리를 수행합니다.
		 */
		static bool isNullOrEmpty( const utf16* str );

		/**
		 * @brief utf8ToUtf16 처리를 수행합니다.
		 */
		static std::wstring utf8ToUtf16( const std::string_view& input );
		/**
		 * @brief utf16ToUtf8 처리를 수행합니다.
		 */
		static std::string	utf16ToUtf8( const std::wstring_view& input );

		/**
		 * @brief utf16ToLocale 처리를 수행합니다.
		 */
		static std::string	utf16ToLocale( const std::wstring_view& input );
		/**
		 * @brief localeToUtf16 처리를 수행합니다.
		 */
		static std::wstring localeToUtf16( const std::string_view& input );

		/**
		 * @brief localeToUtf8 처리를 수행합니다.
		 */
		static std::string localeToUtf8( const std::string_view& input );
		/**
		 * @brief utf8ToLocale 처리를 수행합니다.
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
		 * @brief split 처리를 수행합니다.
		 */
		static std::vector<std::string> split( const std::string_view& input, const std::string_view& delimiterList );
		/**
		 * @brief toUpper 처리를 수행합니다.
		 */
		static std::string				toUpper( const std::string_view& input );
		/**
		 * @brief toLower 처리를 수행합니다.
		 */
		static std::string				toLower( const std::string_view& input );

		/**
		 * @brief trimStart 처리를 수행합니다.
		 */
		static std::string trimStart( const std::string_view& input );
		/**
		 * @brief trimEnd 처리를 수행합니다.
		 */
		static std::string trimEnd( const std::string_view& input );
		/**
		 * @brief trim 처리를 수행합니다.
		 */
		static std::string trim( const std::string_view& input );

		/**
		 * @brief strlen 처리를 수행합니다.
		 */
		static uint32 strlen( const utf8* str );
		/**
		 * @brief strlen 처리를 수행합니다.
		 */
		static uint32 strlen( const utf16* str );

		/**
		 * @brief strnicmp 처리를 수행합니다.
		 */
		static int32 strnicmp( const utf8* lhs, const utf8* rhs, const uint32 stringLength );
		/**
		 * @brief strnicmp 처리를 수행합니다.
		 */
		static int32 strnicmp( const utf16* lhs, const utf16* rhs, const uint32 stringLength );

		/**
		 * @brief strcmp 처리를 수행합니다.
		 */
		static int32 strcmp( const utf8* lhs, const utf8* rhs );
		/**
		 * @brief strcmp 처리를 수행합니다.
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
		 * @brief toString 처리를 수행합니다.
		 */
		static std::string	toString( const std::wstring_view& input );
		/**
		 * @brief toWString 처리를 수행합니다.
		 */
		static std::wstring toWString( const std::string_view& input );
		/**
		 * @brief isValidUTF8 처리를 수행합니다.
		 */
		static bool			isValidUTF8( const std::string_view input );
	};
}
