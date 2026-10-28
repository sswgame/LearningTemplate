/**
 * @file StringUtil.h
 * @brief UTF 변환 · 해시 · 분할 · 트림 같은 문자열 유틸리티입니다(입력은 널 종료 utf8* / utf16*).
 */
#pragma once
#include "Core/Common/AsciiUtil.h"
#include "Core/Common/Defines.h"
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/Utf8Util.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) to_string — 숫자 → sw::string (StringUtil 분류의 자유 함수)
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

    /** @brief 두 문자열의 공통 접두 · 접미를 뺀 변경 구간입니다(텍스트 Undo · 패치용). */
    struct StringChangeSpan
    {
        string _removed;
        string _added;
        uint32 _prefixLength{ 0 };
        uint32 _suffixLength{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief `StringUtil::split` 의 선택입니다. 기본은 아무것도 바꾸지 않는다(빈 토큰도 남는다 — `"a,,b"` → `a` · 빈 글 · `b`). */
    struct StringSplitOptions
    {
        bool _bTrim{ false };      ///< 토큰마다 앞뒤 공백을 뗀다
        bool _bDropEmpty{ false }; ///< (자른 뒤) 빈 토큰을 버린다
    };
} // namespace sw

namespace sw
{
    namespace StringUtilCrcInternal
    {
        /** @brief IEEE 802.3 CRC32 테이블을 컴파일 타임에 만듭니다. */
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
    // 2) StringUtil — UTF 변환 · 해시 · 분할 · 트림(모두 static)
    // ------------------------------------------------------------------------------
    struct SW_API StringUtil
    {
        static constexpr const utf8*  kWhiteSpace  = " \n\r\t\f\v";
        static constexpr const utf16* kWhiteSpaceW = L" \n\r\t\f\v";

        /** @brief UTF-8 문자열이 nullptr 이거나 비어 있는지 확인합니다. */
        static bool isNullOrEmpty( const utf8* pStr );
        /** @brief UTF-16 문자열이 nullptr 이거나 비어 있는지 확인합니다. */
        static bool isNullOrEmpty( const utf16* pStr );

        /** @brief 바이트열이 올바른 UTF-8 인지 검사합니다. */
        static bool isValidUtf8( const utf8* pInput );
        /**
         * @brief 올바른 UTF-8 은 그대로 두고, 잘못된 바이트만 백슬래시 + `xNN`(16진 두 자리) 넉 자로 바꿉니다. 결과는 늘 올바른 UTF-8 입니다.
         * @details 로그가 이것을 쓴다. 한 바이트만 틀려도 **줄 전체**를 로캘 변환하면, `setlocale` 을 부르는 곳이 없어 C 로캘이라
         *          Windows 에서는 멀쩡한 한글까지 깨지고 glibc 에서는 줄이 통째로 빈다. 무슨 바이트였는지가 남아 원인도 찾을 수 있다.
         */
        static string escapeInvalidUtf8( string_view text );
        /**
         * @brief @p text 의 @p inoutOffset 위치에서 UTF-8 글자 하나를 읽어 코드 포인트를 돌려주고, 오프셋을 그 글자 뒤로 옮깁니다.
         * @details 잘못된 바이트는 U+FFFD 로 읽고 한 바이트 이상 넘깁니다(무한 반복이 없습니다). 오프셋이 끝이면 0 을 돌려주고 옮기지 않습니다.
         */
        static uint32 decodeUtf8( string_view text, size_t& inoutOffset );
        /**
         * @brief @p text 를 바이트 @p cut 에서 자를 때 글자 한가운데가 아닌 가장 가까운 앞 경계(글자 시작)를 돌려줍니다.
         * @details @p cut 이 @p text 길이 이상이면 길이를 돌려준다. UTF-8 글자는 최대 4 바이트라 이어지는 바이트(10xxxxxx)를 세 번까지만 물린다(잘못된 UTF-8 을 끝없이 거슬러 가지 않는다).
         */
        [[nodiscard]] static constexpr size_t floorToUtf8Boundary( string_view text, size_t cut )
        {
            if ( cut >= text.size() )
                return text.size();
            return Utf8Util::backOffToCharacterStart( text.data(), cut );
        }
        /**
         * @brief `decodeUtf8` 와 같게 글자 하나를 읽되, 올바른 UTF-8 이었는지 돌려줍니다(false 면 @p outCodepoint 는 U+FFFD).
         * @details 오프셋이 끝이면 false 이고 옮기지 않습니다. 잘못된 시퀀스는 `decodeUtf8` 가 정한 길이만큼 넘어간다(글자마다 직접 비트를 풀지 말고 이것을 쓴다).
         */
        [[nodiscard]] static bool tryDecodeUtf8( string_view text, size_t& inoutOffset, uint32& outCodepoint );
        /** @brief @p text 가 올바른 글자 정확히 하나(그 밖의 바이트 없음)이면 그 코드 포인트를 @p outCodepoint 에 담고 true 입니다. 빈 글 · 두 글자 이상 · 잘못된 UTF-8 은 false 입니다. */
        [[nodiscard]] static bool tryDecodeSingleCodepoint( string_view text, uint32& outCodepoint );
        /**
         * @brief UTF-8 글을 UCS-2(16 비트 코드 단위, 기본 다국어 평면)로 바꿉니다. 평면 밖(이모지 등)과 잘못된 바이트는 U+FFFD 한 글자가 됩니다.
         * @details UCS-2 글꼴만 받는 경로(X11 코어 글꼴의 `XDrawString16`)용이다. 서로게이트 쌍으로 늘리는 `utf8ToUtf16` 와 다르다.
         */
        static void utf8ToUcs2( string_view text, vector<uint16>& outListUnit );
        /** @brief 코드 포인트 하나를 UTF-8 로 @p inoutText 끝에 덧붙입니다. */
        static void appendUtf8( string& inoutText, uint32 codepoint );
        /**
         * @brief 바이트 오프셋이 몇째 줄 · 몇째 글자인지 셉니다(둘 다 1 부터). 열은 바이트가 아니라 UTF-8 글자 수입니다.
         * @details 파서 오류를 `경로:줄:열` 로 적을 때 쓴다 — IDE 터미널은 그 형식을 눌러 그 위치로 간다. 오프셋이 글 밖이면 끝 위치입니다.
         */
        static void getLineAndColumn( string_view text, size_t byteOffset, uint32& outLine, uint32& outColumn );

        /** @brief UTF-8 문자열을 UTF-16(sw::wstring)으로 바꿉니다. */
        static wstring utf8ToUtf16( const utf8* pInput );
        /** @brief UTF-16 문자열을 UTF-8(sw::string)로 바꿉니다. */
        static string utf16ToUtf8( const utf16* pInput );

        /** @brief ASCII 숫자('0'~'9')인지 봅니다. */
        static constexpr bool isDigitChar( const utf8 ch ) { return '0' <= ch && ch <= '9'; }

        /** @brief UTF-8 문자 하나를 소문자로 바꿉니다(ASCII 만). */
        static constexpr utf8 toLowerChar( const utf8 ch ) { return AsciiUtil::toLower( ch ); }

        /** @brief UTF-16 문자 하나를 소문자로 바꿉니다(ASCII 만). */
        static constexpr utf16 toLowerChar( const utf16 ch ) { return AsciiUtil::toLower( ch ); }

        /** @brief UTF-8 문자 하나를 대문자로 바꿉니다(ASCII 만). */
        static constexpr utf8 toUpperChar( const utf8 ch ) { return ( 'a' <= ch && ch <= 'z' ) ? static_cast<utf8>( ch - ( 'a' - 'A' ) ) : ch; }

        /** @brief UTF-16 문자 하나를 대문자로 바꿉니다(ASCII 만). */
        static constexpr utf16 toUpperChar( const utf16 ch ) { return ( L'a' <= ch && ch <= L'z' ) ? static_cast<utf16>( ch - ( L'a' - L'A' ) ) : ch; }

        /** @brief UTF-8 문자열 전체를 대문자로 변환합니다. */
        static string toUpper( const utf8* pInput );
        /** @brief UTF-16 문자열 전체를 대문자로 변환합니다. */
        static wstring toUpper( const utf16* pInput );
        /** @brief UTF-8 문자열 전체를 소문자로 변환합니다. */
        static string toLower( const utf8* pInput );
        /** @brief UTF-16 문자열 전체를 소문자로 변환합니다. */
        static wstring toLower( const utf16* pInput );
        /**
         * @brief PascalCase 식별자를 밑줄로 낱말을 가른 이름으로 바꿉니다(ASCII). `NotSignedIn` → `not_signed_in`, `HTTPMethod` → `http_method`, `UI` → `ui`.
         * @details 열거자 이름에서 지표 라벨 · 셰이더 define 을 만들 때 씁니다. @p bUpper 이면 `NOT_SIGNED_IN` 입니다.
         */
        static string toSnakeCase( string_view pascalName, bool bUpper );

        /** @brief 편집 전후 텍스트의 공통 접두 · 접미를 빼고 바뀐 가운데 부분만 남깁니다. */
        static StringChangeSpan makeChangeSpan( string_view before, string_view after );
        /** @brief 편집 후 텍스트와 변경 구간으로 편집 전 본문을 만듭니다. */
        static string reconstructBefore( const StringChangeSpan& span, string_view afterText );
        /** @brief 편집 전 텍스트와 변경 구간으로 편집 후 본문을 만듭니다. */
        static string reconstructAfter( const StringChangeSpan& span, string_view beforeText );

        /** @brief 문자열 안의 특정 문자를 다른 문자로 바꿉니다. */
        static void replaceChar( string& inoutStr, utf8 fromChar, utf8 toChar );
        /** @brief 문자열 안의 특정 문자를 다른 문자로 바꿉니다(UTF-16). */
        static void replaceChar( wstring& inoutStr, utf16 fromChar, utf16 toChar );
        /** @brief 부분 문자열 from 을 to 로 바꾼 새 문자열을 반환합니다. */
        static string replace( string_view input, string_view from, string_view to );

        /** @brief 두 문자열이 같은지 확인합니다(대소문자 무시 옵션). */
        static bool equals( string_view lhs, string_view rhs, bool bIgnoreCase = false ) noexcept;
        /** @brief 두 문자열이 같은지 확인합니다(대소문자 무시 옵션). */
        static bool equals( wstring_view lhs, wstring_view rhs, bool bIgnoreCase = false ) noexcept;
        /** @brief 두 C 문자열이 같은지 확인합니다(nullptr 안전, 대소문자 무시 옵션). */
        static bool equals( const utf8* pLhs, const utf8* pRhs, bool bIgnoreCase = false ) noexcept;
        /** @brief 두 C 문자열이 같은지 확인합니다(nullptr 안전, 대소문자 무시 옵션). */
        static bool equals( const utf16* pLhs, const utf16* pRhs, bool bIgnoreCase = false ) noexcept;

        /** @brief 두 문자열을 사전순으로 비교합니다(작으면 음수, 같으면 0, 크면 양수). */
        static int32 compare( string_view lhs, string_view rhs, bool bIgnoreCase = false ) noexcept;
        /** @brief 두 문자열을 사전순으로 비교합니다(작으면 음수, 같으면 0, 크면 양수). */
        static int32 compare( wstring_view lhs, wstring_view rhs, bool bIgnoreCase = false ) noexcept;
        /** @brief 두 C 문자열을 사전순으로 비교합니다(nullptr 안전, 작으면 음수, 같으면 0, 크면 양수). */
        static int32 compare( const utf8* pLhs, const utf8* pRhs, bool bIgnoreCase = false ) noexcept;
        /** @brief 두 C 문자열을 사전순으로 비교합니다(nullptr 안전, 작으면 음수, 같으면 0, 크면 양수). */
        static int32 compare( const utf16* pLhs, const utf16* pRhs, bool bIgnoreCase = false ) noexcept;

        /** @brief 문자열이 prefix 로 시작하는지 확인합니다(할당 없음). */
        static bool startsWith( string_view str, string_view prefix, bool bIgnoreCase = false ) noexcept;
        /** @brief 문자열이 suffix 로 끝나는지 확인합니다(할당 없음). */
        static bool endsWith( string_view str, string_view suffix, bool bIgnoreCase = false ) noexcept;
        /**
         * @brief 문자열 **안 어딘가에** 부분 문자열이 있는지 확인합니다(할당 없음).
         * @details `startsWith` · `endsWith` 의 짝입니다. `str.find( sub ) != npos` 로 직접 적으면 **`bIgnoreCase` 를 잃습니다**
         *          (대소문자를 무시하는 짝 함수와 판정이 갈라져 `Dungeon_01` 만 던전이 아니게 되는 식).
         * @return 있으면 true. `sub` 가 비어 있으면 true 입니다(표준 `find` 와 같습니다).
         */
        static bool contains( string_view str, string_view sub, bool bIgnoreCase = false ) noexcept;
        /**
         * @brief `*`(0 글자 이상, '/' 도 넘는다) · `?`(한 글자) 와일드카드 @p pattern 이 @p text 전체에 맞는지 봅니다(ASCII 대소문자 무시).
         * @details 글자 단위의 바탕 매처입니다. 경로에는 `/` 를 넘지 않는 `matchesPathPattern` 을 씁니다(이 함수를 경로에 직접 쓰면 `*` 가 폴더를 넘는다).
         */
        static bool matchesWildcard( string_view pattern, string_view text ) noexcept;
        /**
         * @brief 경로 패턴 @p pattern 이 @p path(`/` 구분, 호출부가 구분자를 맞춘다)에 맞는지 봅니다. 팩 쿠커 제외 패턴과 에디터 임포트 규칙이 함께 쓰는 마디 매칭입니다.
         * @details `/` 없는 패턴은 파일 이름에만 맞춥니다. `/` 가 있으면 경로를 마디(폴더 이름 · 파일 이름) 단위로 맞춥니다.
         *          마디 안의 `*` · `?` 는 마디를 넘지 않고, 마디 하나가 통째로 `**` 이면 0 개 이상의 마디에 맞습니다. 대소문자는 가리지 않습니다.
         */
        [[nodiscard]] static bool matchesPathPattern( string_view pattern, string_view path );

        /** @brief string_view 의 앞 공백을 제거한 뷰를 반환합니다. */
        static string_view trimStart( string_view input );
        /** @brief wstring_view 의 앞 공백을 제거한 뷰를 반환합니다. */
        static wstring_view trimStart( wstring_view input );

        /** @brief string_view 의 뒤 공백을 제거한 뷰를 반환합니다. */
        static string_view trimEnd( string_view input );
        /** @brief wstring_view 의 뒤 공백을 제거한 뷰를 반환합니다. */
        static wstring_view trimEnd( wstring_view input );

        /** @brief string_view 의 앞뒤 공백을 제거한 뷰를 반환합니다(할당 없음). */
        static string_view trim( string_view input );
        /** @brief wstring_view 의 앞뒤 공백을 제거한 뷰를 반환합니다(할당 없음). */
        static wstring_view trim( wstring_view input );

        /**
         * @brief @p text 를 줄마다 나눠 @p callback( string_view line ) 을 부릅니다. 할당하지 않습니다.
         * @details 줄 끝의 `\r` 하나를 제거하므로 CRLF 글도 같은 줄을 냅니다. 빈 줄도 넘기지만, 글이 `\n` 으로 끝나면 그 뒤에 빈 줄을
         *          하나 더 만들지 않습니다(빈 글은 줄이 없습니다). 콜백이 `bool` 을 돌려주면 false 에서 멈춥니다.
         */
        template <typename TCallback>
        static void forEachLine( string_view text, TCallback&& callback )
        {
            size_t lineStart = 0;
            while ( lineStart < text.size() )
            {
                size_t lineEnd = text.find( '\n', lineStart );
                if ( lineEnd == string_view::npos )
                    lineEnd = text.size();
                string_view line = text.substr( lineStart, lineEnd - lineStart );
                if ( line.empty() == false && line.back() == '\r' )
                    line.remove_suffix( 1 );
                lineStart = lineEnd + 1;
                if constexpr ( std::is_same_v<std::invoke_result_t<TCallback&, string_view>, bool> )
                {
                    if ( callback( line ) == false )
                        return;
                }
                else
                {
                    callback( line );
                }
            }
        }

        /**
         * @brief @p separators 의 아무 글자로 나뉜 비지 않은 토큰마다 @p callback( string_view token ) 을 부릅니다. 할당하지 않습니다.
         * @details 구분자가 이어지면 빈 토큰은 건너뜁니다. 콜백이 `bool` 을 돌려주면 false 에서 멈춥니다.
         * @code
         *     StringUtil::forEachToken( "Spring, Fall", ",; ", [&]( string_view token ) { ... } );
         * @endcode
         */
        template <typename TCallback>
        static void forEachToken( string_view text, string_view separators, TCallback&& callback )
        {
            size_t tokenStart = 0;
            while ( tokenStart < text.size() )
            {
                size_t tokenEnd = text.find_first_of( separators, tokenStart );
                if ( tokenEnd == string_view::npos )
                    tokenEnd = text.size();
                if ( tokenEnd > tokenStart )
                {
                    const string_view token = text.substr( tokenStart, tokenEnd - tokenStart );
                    if constexpr ( std::is_same_v<std::invoke_result_t<TCallback&, string_view>, bool> )
                    {
                        if ( callback( token ) == false )
                            return;
                    }
                    else
                    {
                        callback( token );
                    }
                }
                tokenStart = tokenEnd + 1;
            }
        }

        /**
         * @brief @p separators 의 아무 글자에서 나눈 토큰 목록입니다(원본을 가리키는 뷰 — 원본이 살아 있는 동안만 쓴다).
         * @details 글을 나누는 정본입니다. 호출부에서 `find` · `substr` 루프를 다시 짜지 않습니다. 할당 없이 하나씩 보려면 `forEachToken`
         *          (빈 토큰을 늘 버린다), 줄은 `forEachLine` 입니다. 빈 글은 `_bDropEmpty` 가 아니면 빈 토큰 하나입니다(파이썬 `str.split( sep )` 과 같다).
         */
        [[nodiscard]] static vector<string_view> split( string_view text, string_view separators, StringSplitOptions options = {} );

        /** @brief 문자열 길이(문자 수)를 반환합니다. */
        static uint32 strlen( const utf8* pStr );
        /** @brief 문자열 길이(문자 수)를 반환합니다. */
        static uint32 strlen( const utf16* pStr );

        /**
         * @brief 원본을 목적지에 복사합니다. **항상 NUL 로 끝나고**, 들어가지 않으면 자릅니다.
         * @param length 목적지 **버퍼의 크기**(문자 수)입니다. 종료 문자 몫을 포함합니다.
         * @details 플랫폼 함수에 맡기지 않습니다 — Windows 의 `strncpy_s( dst, length, src, length )` 는 원본이 종료 문자까지
         *          들어가지 않으면 목적지를 **빈 문자열로 만들고** 잘못된 매개변수 핸들러를 부르고, `::strncpy` 는 `length` 글자를
         *          복사하고 **종료 문자를 붙이지 않습니다.** 잘라 낼 때도 반드시 끝을 맺는 하나의 규칙으로 맞춥니다.
         */
        static void strncpy( utf8* pOutDest, const utf8* pSource, uint32 length );
        /** @brief 위와 같습니다. 항상 NUL 로 끝나고, 들어가지 않으면 자릅니다. */
        static void strncpy( utf16* pOutDest, const utf16* pSource, uint32 length );

        /** @brief 부분 문자열이 처음 나오는 위치를 반환합니다. */
        static const utf8* strstr( const utf8* pStr, const utf8* pSubstr );
        /** @brief 부분 문자열이 처음 나오는 위치를 반환합니다. */
        static const utf16* strstr( const utf16* pStr, const utf16* pSubstr );

        /** @brief 대소문자를 무시하고 부분 문자열이 처음 나오는 위치를 반환합니다. */
        static const utf8* stristr( const utf8* pStr, const utf8* pSubstr );
        /** @brief 대소문자를 무시하고 부분 문자열이 처음 나오는 위치를 반환합니다. */
        static const utf16* stristr( const utf16* pStr, const utf16* pSubstr );

        /** @brief 문자가 처음 나오는 위치를 반환합니다. */
        static const utf8* strchr( const utf8* pStr, utf8 ch );
        /** @brief 문자가 처음 나오는 위치를 반환합니다. */
        static const utf16* strchr( const utf16* pStr, utf16 ch );

        /**
         * @brief 불리언 토큰(`true` · `false` · `1` · `0` · `yes` · `no` · `on` · `off`, 대소문자 무시)을 읽습니다. 아니면 false 이고 값은 그대로입니다.
         * @details 앞뒤 공백을 자른 뒤 비교합니다. 폴백 값을 받는 형식은 두지 않습니다 — "ture" 가 조용히 false 가 되지 않게 호출부가 실패를 다룹니다.
         */
        [[nodiscard]] static bool tryParseBool( string_view token, bool& outValue );

        /**
         * @brief `키=값` 한 토막을 첫 @p separator 에서 나누고 키와 값의 앞뒤 공백을 자릅니다(할당 없음).
         * @return 구분자가 없거나 키가 비면 false 이고 출력은 그대로입니다. 값은 비어도 됩니다(`key=`).
         * @details 호출부마다 `find( '=' )` · `substr` 을 손으로 쓰면 공백 자르기 · 빈 키 거절이 길마다 달라져 이 하나로 모읍니다.
         */
        [[nodiscard]] static bool trySplitKeyValue( string_view text, string_view& outKey, string_view& outValue, utf8 separator = '=' );

        /**
         * @brief string_view 토큰을 32비트 실수로 파싱합니다(할당 없음).
         * @return 성공하면 true, 실패하면 false(outValue 는 바꾸지 않습니다)
         */
        [[nodiscard]] static bool parseFloat( string_view token, float32& outValue );

        /**
         * @brief @p separators 로 나뉜 실수(`"1 0.5 0"` · `"(1, 2, 3)"`)가 정확히 @p count 개일 때만 읽습니다(할당 없음).
         * @details 토큰이 모자라거나 남거나 숫자가 아니면 false 이고 @p pOutValue 는 그대로입니다(개수 비교를 호출부에 맡기지 않는 형식).
         */
        [[nodiscard]] static bool parseExactFloats( string_view text, string_view separators, float32* pOutValue, uint32 count );

        /**
         * @brief string_view 토큰을 64비트 실수로 파싱합니다(할당 없음).
         * @return 성공하면 true, 실패하면 false(outValue 는 바꾸지 않습니다)
         */
        [[nodiscard]] static bool parseDouble( string_view token, float64& outValue );

        /**
         * @brief string_view 토큰을 32비트 정수로 파싱합니다(할당 없음).
         * @return 성공하면 true, 실패하면 false(outValue 는 바꾸지 않습니다)
         */
        [[nodiscard]] static bool parseInt( string_view token, int32& outValue, int32 base = 10 );

        /**
         * @brief @p separators 로 나뉜 32비트 정수(부호 허용) 목록을 @p outListValue 에 읽습니다.
         * @details 먼저 비웁니다. 빈 글은 빈 목록으로 성공이고, 숫자가 아닌 토큰이 있으면 false 입니다(그때 목록은 거기까지 읽은 값입니다).
         */
        [[nodiscard]] static bool parseIntList( string_view text, string_view separators, vector<int32>& outListValue );

        /**
         * @brief string_view 토큰을 64비트 정수로 파싱합니다(할당 없음).
         * @return 성공하면 true, 실패하면 false(outValue 는 바꾸지 않습니다)
         */
        [[nodiscard]] static bool parseInt64( string_view token, int64& outValue, int32 base = 10 );

        /**
         * @brief string_view 토큰을 64비트 부호 없는 정수로 파싱합니다(할당 없음).
         * @return 성공하면 true, 실패하면 false(outValue 는 바꾸지 않습니다)
         */
        [[nodiscard]] static bool parseUint64( string_view token, uint64& outValue, int32 base = 10 );

        /**
         * @brief 정수 · 부동소수점 숫자를 할당 없이 버퍼에 포맷하고, 쓴 길이를 반환합니다.
         * @param precisionOrBase 정수면 진법(기본 10), 실수면 소수점 아래 자릿수(기본 -1: 가장 짧은 표현)
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
                        pOutBuffer[len]  = '\0';
                        return len;
                    }
                }
                else
                {
                    auto [pPtr, ec] = std::to_chars( pOutBuffer, pOutBuffer + capacity - 1, value, std::chars_format::fixed, precisionOrBase );
                    if ( ec == std::errc{} )
                    {
                        const uint32 len = static_cast<uint32>( pPtr - pOutBuffer );
                        pOutBuffer[len]  = '\0';
                        return len;
                    }
                }
            }
            else if constexpr ( std::is_integral_v<T> || std::is_enum_v<T> )
            {
                const int32 base = ( precisionOrBase <= 0 ) ? 10 : precisionOrBase;
                auto [pPtr, ec]  = std::to_chars( pOutBuffer, pOutBuffer + capacity - 1, value, base );
                if ( ec == std::errc{} )
                {
                    const uint32 len = static_cast<uint32>( pPtr - pOutBuffer );
                    pOutBuffer[len]  = '\0';
                    return len;
                }
            }
            pOutBuffer[0] = '\0';
            return 0;
        }

    private:
        static constexpr array<uint32, constant::kMaxBuffer256> kArrCrc32Table = StringUtilCrcInternal::makeCrc32TableInternal();

    public:
        /** @brief 임의의 바이너리 버퍼에 대한 IEEE 802.3 CRC32 체크섬을 테이블 조회로 빠르게 계산합니다. */
        static constexpr uint32 computeCrc32( const void* pData, const size_t length ) noexcept
        {
            if ( pData == nullptr || length == 0 )
                return 0;

            const uint8* pBytes = static_cast<const uint8*>( pData );
            uint32       crc    = 0xFFFFFFFFu;
            for ( size_t byteIndex = 0; byteIndex < length; ++byteIndex )
            {
                crc = ( crc >> 8 ) ^ kArrCrc32Table[( crc ^ pBytes[byteIndex] ) & 0xFFu];
            }
            return ~crc;
        }
    };
} // namespace sw
