#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Core/CoreMinimal.h"

namespace sw
{
    namespace
    {
        struct StringUtilInternal
        {
            /**
             * @brief 목적지 버퍼에 원본을 채우고 **반드시** NUL 로 끝냅니다. 들어가지 않으면 자릅니다.
             * @details 두 오버로드(utf8 · utf16)가 같은 규칙을 따르도록 한 곳에 둡니다. 예전에는 각자 플랫폼 함수를 불러서
             *          **플랫폼마다 결과가 달랐습니다.**
             */
            template <typename CharType>
            static void copyTerminated( CharType* pOutDest, const CharType* pSource, uint32 length )
            {
                if ( pOutDest == nullptr || length == 0 )
                    return;
                if ( pSource == nullptr )
                {
                    pOutDest[0] = CharType{};
                    return;
                }

                uint32 copiedCount = 0;
                while ( copiedCount + 1 < length && pSource[copiedCount] != CharType{} )
                {
                    pOutDest[copiedCount] = pSource[copiedCount];
                    ++copiedCount;
                }
                pOutDest[copiedCount] = CharType{};
            }

            static constexpr uint32 kMaxUnicodeCodepoint = 0x10FFFF;
            static constexpr uint32 kSurrogateBegin      = 0xD800;
            static constexpr uint32 kSurrogateEnd        = 0xDFFF;
            static constexpr uint32 kUtf16LowBoundary    = 0x10000;

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
             * @brief UTF-8 선행 바이트로 멀티바이트 시퀀스의 길이(1 ~ 4바이트)와 초기 비트를 분류합니다.
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
             * @brief 바이트 배열이 UTF-8 인코딩 규칙을 지키는지 검증합니다.
             * @details
             * - **64비트 SWAR(SIMD Within A Register) 빠른 검사**:
             *   대부분의 텍스트(로그 포맷, 태그, 영문 등)가 7비트 ASCII(0x00 ~ 0x7F)라는 점을 이용해, `uint64` 로 8바이트씩 읽어
             *   최상위 비트 마스크(`0x8080808080808080ULL`)로 한 번에 8바이트를 검사합니다.
             * - **멀티바이트 시퀀스 검증**:
             *   ASCII 가 아닌 바이트를 만났을 때만 2 ~ 4바이트 UTF-8 선행 바이트를 분류하고, 후속 바이트(0x80 ~ 0xBF) · overlong ·
             *   서로게이트(0xD800 ~ 0xDFFF) · 유니코드 최대 범위를 검사합니다.
             */
            static bool isValidUtf8( const uint8* pData, const size_t length ) noexcept
            {
                size_t pos{ 0 };

                // 8바이트 단위 SWAR 로 ASCII 인지 빠르게 검사한다(한 번에 8글자)
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
                    // 1바이트 ASCII 문자(0x00 ~ 0x7F)
                    if ( ( byte0 & 0x80 ) == 0 )
                    {
                        ++pos;
                        // 다시 8바이트 SWAR 빠른 검사를 시도한다
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

                    // 2 ~ 4바이트 멀티바이트 UTF-8 시퀀스를 검증한다
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

                    // overlong 인코딩, UTF-16 서로게이트, 유니코드 최댓값 초과를 검사한다
                    const bool bIsOverlong   = codepoint < lead._minCodepoint;
                    const bool bIsSurrogate  = ( codepoint >= kSurrogateBegin ) && ( codepoint <= kSurrogateEnd );
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
                const Utf8LeadInfo lead      = classifyUtf8LeadByte( static_cast<uint8>( pData[0] ) );
                const size_t       byteCount = MathUtil::min( lead._sequenceLength, remaining );

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

            /** @brief 로케일 변환이 실패했음을 나타내는 값입니다(C 런타임 규약). */
            static constexpr size_t kConversionFailed = static_cast<size_t>( -1 );

            /**
             * @brief wide → 로케일 멀티바이트 변환 원시 연산입니다.
             * @param pOutBuffer nullptr 이면 변환하지 않고 필요한 크기만 반환합니다.
             * @return 널 종료를 포함한 바이트 수. 실패하면 kConversionFailed
             * @details 플랫폼 분기를 이 한 줄짜리 함수에만 둡니다. 예전에는 "크기 질의 → 버퍼 준비 → 변환" 전체를 두 방향(utf16 →
             *          locale, locale → utf16)이 각자 적어서 같은 #if 가 네 벌이었습니다.
             */
            static size_t wideToMultiByteInternal( utf8* pOutBuffer, size_t bufferSize, const utf16* pInput )
            {
#if defined( SW_PLATFORM_WINDOWS )
                size_t convertedSize{ 0 };
                if ( wcstombs_s( &convertedSize, pOutBuffer, bufferSize, pInput, bufferSize ) != 0 )
                    return kConversionFailed;
                return convertedSize;
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
                const size_t writtenSize = wcstombs( pOutBuffer, pInput, bufferSize );
                if ( writtenSize == kConversionFailed )
                    return kConversionFailed;
                // POSIX 는 널 종료를 세지 않는다. Windows 쪽과 같은 규칙으로 맞춘다.
                return writtenSize + 1;
#else
    #error "Unsupported platform"
#endif
            }

            /**
             * @brief 로케일 멀티바이트 → wide 변환 원시 연산입니다.
             * @param pOutBuffer nullptr 이면 변환하지 않고 필요한 문자 수만 반환합니다.
             * @return 널 종료를 포함한 문자 수. 실패하면 kConversionFailed
             */
            static size_t multiByteToWideInternal( utf16* pOutBuffer, size_t bufferSize, const utf8* pInput )
            {
#if defined( SW_PLATFORM_WINDOWS )
                size_t convertedSize{ 0 };
                if ( mbstowcs_s( &convertedSize, pOutBuffer, bufferSize, pInput, bufferSize ) != 0 )
                    return kConversionFailed;
                return convertedSize;
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
                const size_t writtenSize = mbstowcs( pOutBuffer, pInput, bufferSize );
                if ( writtenSize == kConversionFailed )
                    return kConversionFailed;
                return writtenSize + 1;
#else
    #error "Unsupported platform"
#endif
            }

            static string utf16ToLocaleInternal( const utf16* pInput )
            {
                if ( pInput == nullptr || *pInput == L'\0' )
                    return {};

                const size_t requiredSize = wideToMultiByteInternal( nullptr, 0, pInput );
                if ( requiredSize == 0 || requiredSize == kConversionFailed )
                    return {};

                string buffer( requiredSize - 1, '\0' );
                wideToMultiByteInternal( buffer.data(), requiredSize, pInput );
                return buffer;
            }

            static wstring localeToUtf16Internal( const utf8* pInput )
            {
                if ( pInput == nullptr || *pInput == '\0' )
                    return {};

                const size_t requiredSize = multiByteToWideInternal( nullptr, 0, pInput );
                if ( requiredSize == 0 || requiredSize == kConversionFailed )
                    return {};

                wstring buffer( requiredSize - 1, L'\0' );
                multiByteToWideInternal( buffer.data(), requiredSize, pInput );
                return buffer;
            }

            template <typename T>
            static string integerToString( T value )
            {
                utf8 arrBuf[constant::kMaxBuffer32];
                auto [ptr, ec] = std::to_chars( arrBuf, arrBuf + sizeof( arrBuf ), value );
                return string( arrBuf, static_cast<size_t>( ptr - arrBuf ) );
            }

            template <typename T>
            static string floatToString( T value )
            {
                utf8 arrBuf[constant::kMaxBuffer64];
                auto [ptr, ec] = std::to_chars( arrBuf, arrBuf + sizeof( arrBuf ), value );
                if ( ec == std::errc() )
                    return string( arrBuf, static_cast<size_t>( ptr - arrBuf ) );

                formatstring( arrBuf, sizeof( arrBuf ), "%#", value );
                return string{ arrBuf };
            }

            /**
             * @brief 문자를 하나씩 바꾼 사본을 만듭니다. `toUpper` · `toLower` 의 utf8 · utf16 네 벌이 같은 뼈대(길이 재기 · 크기 잡기 · 한 글자씩 변환)를 씁니다.
             * @details 바꾸는 규칙만 호출하는 쪽이 넘깁니다. utf8 은 ASCII 만, utf16 은 ASCII 밖을 C 런타임(`towupper` · `towlower`)에 맡깁니다.
             */
            template <typename StringType, typename CharType, typename MapFn>
            static StringType mapEachChar( const CharType* pInput, MapFn&& mapChar )
            {
                if ( StringUtil::isNullOrEmpty( pInput ) )
                    return {};

                const size_t length = StringUtil::strlen( pInput );
                StringType   result;
                result.resize( length );
                for ( size_t charIndex = 0; charIndex < length; ++charIndex )
                    result[charIndex] = mapChar( pInput[charIndex] );
                return result;
            }

            /** @brief 길이가 같고 글자마다 같으면 true 입니다. 대소문자 무시는 ASCII 만 합니다. `string_view` · `wstring_view` 가 함께 씁니다. */
            template <typename ViewType>
            static bool equalsView( ViewType lhs, ViewType rhs, bool bIgnoreCase ) noexcept
            {
                if ( lhs.size() != rhs.size() )
                    return false;
                if ( bIgnoreCase == false )
                    return lhs == rhs;
                for ( size_t charIndex = 0; charIndex < lhs.size(); ++charIndex )
                {
                    if ( lhs[charIndex] != rhs[charIndex] && StringUtil::toLowerChar( lhs[charIndex] ) != StringUtil::toLowerChar( rhs[charIndex] ) )
                        return false;
                }
                return true;
            }

            /**
             * @brief 정수 토큰의 앞부분(공백 · 부호(`+`, 허용하면 `-`) · 기수 접두사(`0x`))을 떼어 냅니다.
             * @details `parseInt` · `parseInt64` · `parseUint64` 가 이 스무 줄을 각자 들고 있었습니다. 기수가 0 이면 접두사로 정하고
             *          (없으면 10), 기수 16 은 `0x` 가 있어도 되고 없어도 됩니다. 부호를 허용하지 않는 쪽(`uint64`)은 `-` 를 남겨 두어
             *          `from_chars` 가 거부하게 합니다.
             * @return 숫자 부분이 남고 기수가 [2, 36] 이면 true
             */
            static bool splitIntegerToken( string_view token, bool bAllowNegative, int32& inoutBase, string_view& outDigits, bool& outNegative )
            {
                string_view trimmed = StringUtil::trim( token );
                outNegative         = false;
                if ( trimmed.empty() )
                    return false;

                if ( trimmed.front() == '+' )
                {
                    trimmed.remove_prefix( 1 );
                }
                else if ( bAllowNegative && trimmed.front() == '-' )
                {
                    outNegative = true;
                    trimmed.remove_prefix( 1 );
                }
                if ( trimmed.empty() )
                    return false;

                const bool bHexPrefix = trimmed.size() >= 2 && trimmed[0] == '0' && ( trimmed[1] == 'x' || trimmed[1] == 'X' );
                if ( inoutBase == 0 )
                    inoutBase = bHexPrefix ? 16 : 10;
                if ( inoutBase == 16 && bHexPrefix )
                    trimmed.remove_prefix( 2 );

                if ( inoutBase < 2 || inoutBase > 36 || trimmed.empty() )
                    return false;
                outDigits = trimmed;
                return true;
            }

            /** @brief 남은 글자가 모두 숫자여야 true 입니다. 뒤에 다른 것이 붙어 있으면 실패입니다. */
            template <typename UnsignedType>
            static bool parseDigitsExact( string_view digits, int32 base, UnsignedType& outValue )
            {
                const auto [ptr, ec] = std::from_chars( digits.data(), digits.data() + digits.size(), outValue, base );
                return ec == std::errc{} && ptr == digits.data() + digits.size();
            }

            /** @brief 부호 있는 정수를 읽습니다. 절댓값을 부호 없는 타입으로 읽고 범위를 확인합니다(최솟값의 절댓값은 최댓값 + 1 입니다). */
            template <typename SignedType, typename UnsignedType>
            static bool parseSignedInteger( string_view token, int32 base, SignedType kMinValue, SignedType kMaxValue, SignedType& outValue )
            {
                string_view digits;
                bool        bNegative{ false };
                if ( splitIntegerToken( token, true, base, digits, bNegative ) == false )
                    return false;

                UnsignedType magnitude{ 0 };
                if ( parseDigitsExact( digits, base, magnitude ) == false )
                    return false;

                const UnsignedType maxMagnitude = static_cast<UnsignedType>( kMaxValue );
                if ( bNegative )
                {
                    if ( magnitude > maxMagnitude + 1 )
                        return false;
                    outValue = ( magnitude == maxMagnitude + 1 ) ? kMinValue : -static_cast<SignedType>( magnitude );
                    return true;
                }
                if ( magnitude > maxMagnitude )
                    return false;
                outValue = static_cast<SignedType>( magnitude );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "StringUtil" );

    /**
     * @brief 32비트 부호 있는 정수를 문자열로 바꿉니다(포맷은 스택 버퍼에서 합니다).
     */
    string to_string( int32 value )
    {
        return StringUtilInternal::integerToString( value );
    }

    /**
     * @brief 32비트 부호 없는 정수를 문자열로 변환합니다.
     */
    string to_string( uint32 value )
    {
        return StringUtilInternal::integerToString( value );
    }

    /**
     * @brief 64비트 정수를 문자열로 변환합니다.
     */
    string to_string( int64 value )
    {
        return StringUtilInternal::integerToString( value );
    }

    /**
     * @brief 64비트 부호 없는 정수를 문자열로 변환합니다.
     */
    string to_string( uint64 value )
    {
        return StringUtilInternal::integerToString( value );
    }

    /**
     * @brief 32비트 부동소수점 숫자를 문자열로 변환합니다.
     */
    string to_string( float32 value )
    {
        return StringUtilInternal::floatToString( value );
    }

    /**
     * @brief 64비트 배정밀도 부동소수점 숫자를 문자열로 변환합니다.
     */
    string to_string( float64 value )
    {
        return StringUtilInternal::floatToString( value );
    }

    bool StringUtil::isNullOrEmpty( const utf8* pStr )
    {
        return ( pStr == nullptr || *pStr == '\0' );
    }

    bool StringUtil::isNullOrEmpty( const utf16* pStr )
    {
        return ( pStr == nullptr || *pStr == L'\0' );
    }

    wstring StringUtil::utf8ToUtf16( const utf8* pInput )
    {
        if ( isNullOrEmpty( pInput ) )
            return {};

        SW_LOG_ASSERT( isValidUtf8( pInput ), "UTF8 문자열이 아닙니다" );

        const size_t length = strlen( pInput );
        wstring      result{};
        result.reserve( length );

        size_t pos{ 0 };
        while ( pos < length )
        {
            const uint8 byte0 = static_cast<uint8>( pInput[pos] );
            if ( byte0 <= 0x7F )
            {
                result.push_back( static_cast<utf16>( byte0 ) );
                ++pos;
                continue;
            }

            const StringUtilInternal::DecodedCodepoint decoded = StringUtilInternal::decodeUtf8Sequence( pInput + pos, length - pos );
            StringUtilInternal::appendWideChar( result, decoded._codepoint );
            pos += MathUtil::max( decoded._byteCount, static_cast<size_t>( 1 ) );
        }

        return result;
    }

    string StringUtil::utf16ToUtf8( const utf16* pInput )
    {
        if ( isNullOrEmpty( pInput ) )
            return {};

        const size_t length = strlen( pInput );
        string       result{};
        result.reserve( length );

        size_t pos{ 0 };
        while ( pos < length )
        {
            uint32 codepoint = pInput[pos];
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
                        const uint32 lowSurrogate = pInput[pos + 1];
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

        SW_LOG_ASSERT( isValidUtf8( result.c_str() ), "UTF8 문자열이 아닙니다" );
        return result;
    }

    string StringUtil::utf16ToLocale( const utf16* pInput )
    {
        return StringUtilInternal::utf16ToLocaleInternal( pInput );
    }

    wstring StringUtil::localeToUtf16( const utf8* pInput )
    {
        if ( isNullOrEmpty( pInput ) )
            return {};
        if ( isValidUtf8( pInput ) )
            return utf8ToUtf16( pInput );

        return StringUtilInternal::localeToUtf16Internal( pInput );
    }

    string StringUtil::localeToUtf8( const utf8* pInput )
    {
        if ( isNullOrEmpty( pInput ) )
            return {};
        if ( isValidUtf8( pInput ) )
            return string{ pInput };

        const wstring wideStr = StringUtilInternal::localeToUtf16Internal( pInput );
        return utf16ToUtf8( wideStr.c_str() );
    }

    string StringUtil::utf8ToLocale( const utf8* pInput )
    {
        if ( isNullOrEmpty( pInput ) )
            return {};

        const wstring wideStr = utf8ToUtf16( pInput );
        return StringUtilInternal::utf16ToLocaleInternal( wideStr.c_str() );
    }

    string StringUtil::toUpper( const utf8* pInput )
    {
        return StringUtilInternal::mapEachChar<string>( pInput, []( utf8 ch )
        { return toUpperChar( ch ); } );
    }

    wstring StringUtil::toUpper( const utf16* pInput )
    {
        return StringUtilInternal::mapEachChar<wstring>( pInput, []( utf16 ch )
        { return ( ch < 0x80 ) ? toUpperChar( ch ) : static_cast<utf16>( std::towupper( static_cast<wint_t>( ch ) ) ); } );
    }

    string StringUtil::toLower( const utf8* pInput )
    {
        return StringUtilInternal::mapEachChar<string>( pInput, []( utf8 ch )
        { return toLowerChar( ch ); } );
    }

    wstring StringUtil::toLower( const utf16* pInput )
    {
        return StringUtilInternal::mapEachChar<wstring>( pInput, []( utf16 ch )
        { return ( ch < 0x80 ) ? toLowerChar( ch ) : static_cast<utf16>( std::towlower( static_cast<wint_t>( ch ) ) ); } );
    }

    void StringUtil::replaceChar( string& inoutStr, const utf8 fromChar, const utf8 toChar )
    {
        for ( utf8& ch : inoutStr )
        {
            if ( ch == fromChar )
                ch = toChar;
        }
    }

    void StringUtil::replaceChar( wstring& inoutStr, const utf16 fromChar, const utf16 toChar )
    {
        for ( utf16& ch : inoutStr )
        {
            if ( ch == fromChar )
                ch = toChar;
        }
    }

    string StringUtil::replace( string_view input, string_view from, string_view to )
    {
        if ( input.empty() || from.empty() )
            return string{ input.data(), input.size() };

        string result;
        result.reserve( input.size() );
        size_t startPos = 0;
        size_t findPos  = input.find( from, startPos );
        while ( findPos != string_view::npos )
        {
            const string_view prefix = input.substr( startPos, findPos - startPos );
            result.append( prefix.data(), prefix.size() );
            result.append( to.data(), to.size() );
            startPos = findPos + from.size();
            findPos  = input.find( from, startPos );
        }
        const string_view suffix = input.substr( startPos );
        result.append( suffix.data(), suffix.size() );
        return result;
    }

    bool StringUtil::equals( string_view lhs, string_view rhs, bool bIgnoreCase ) noexcept
    {
        return StringUtilInternal::equalsView( lhs, rhs, bIgnoreCase );
    }

    bool StringUtil::equals( wstring_view lhs, wstring_view rhs, bool bIgnoreCase ) noexcept
    {
        return StringUtilInternal::equalsView( lhs, rhs, bIgnoreCase );
    }

    bool StringUtil::equals( const utf8* pLhs, const utf8* pRhs, bool bIgnoreCase ) noexcept
    {
        if ( pLhs == pRhs )
            return true;
        if ( pLhs == nullptr || pRhs == nullptr )
            return false;
        if ( bIgnoreCase )
        {
            while ( *pLhs != '\0' && *pRhs != '\0' )
            {
                if ( *pLhs != *pRhs && toLowerChar( *pLhs ) != toLowerChar( *pRhs ) )
                    return false;
                ++pLhs;
                ++pRhs;
            }
            return *pLhs == *pRhs;
        }
        return std::strcmp( pLhs, pRhs ) == 0;
    }

    bool StringUtil::equals( const utf16* pLhs, const utf16* pRhs, bool bIgnoreCase ) noexcept
    {
        if ( pLhs == pRhs )
            return true;
        if ( pLhs == nullptr || pRhs == nullptr )
            return false;
        if ( bIgnoreCase )
        {
            while ( *pLhs != L'\0' && *pRhs != L'\0' )
            {
                if ( *pLhs != *pRhs && toLowerChar( *pLhs ) != toLowerChar( *pRhs ) )
                    return false;
                ++pLhs;
                ++pRhs;
            }
            return *pLhs == *pRhs;
        }
        return std::wcscmp( pLhs, pRhs ) == 0;
    }

    // 바이트를 **부호 없이** 비교한다. `char` 를 그대로 int 로 넓히면 UTF-8 의 0x80 이상 바이트가 음수가 되어, 한글처럼
    // ASCII 가 아닌 문자가 섞인 문자열이 ASCII 보다 **작다고** 나왔다(strcmp 규약과 반대다). 그러면 정렬 · 이진 검색에
    // 이 함수를 쓰는 쪽이 일관되지 않게 동작한다.
    int32 StringUtil::compare( string_view lhs, string_view rhs, bool bIgnoreCase ) noexcept
    {
        const size_t minLen = ( lhs.size() < rhs.size() ) ? lhs.size() : rhs.size();
        if ( bIgnoreCase )
        {
            for ( size_t charIndex = 0; charIndex < minLen; ++charIndex )
            {
                const int32 c1 = static_cast<int32>( static_cast<uint8>( toLowerChar( lhs[charIndex] ) ) );
                const int32 c2 = static_cast<int32>( static_cast<uint8>( toLowerChar( rhs[charIndex] ) ) );
                if ( c1 != c2 )
                    return c1 - c2;
            }
        }
        else
        {
            for ( size_t charIndex = 0; charIndex < minLen; ++charIndex )
            {
                const int32 c1 = static_cast<uint8>( lhs[charIndex] );
                const int32 c2 = static_cast<uint8>( rhs[charIndex] );
                if ( c1 != c2 )
                    return c1 - c2;
            }
        }
        if ( lhs.size() < rhs.size() )
            return -1;
        if ( lhs.size() > rhs.size() )
            return 1;
        return 0;
    }

    int32 StringUtil::compare( wstring_view lhs, wstring_view rhs, bool bIgnoreCase ) noexcept
    {
        const size_t minLen = ( lhs.size() < rhs.size() ) ? lhs.size() : rhs.size();
        if ( bIgnoreCase )
        {
            for ( size_t charIndex = 0; charIndex < minLen; ++charIndex )
            {
                const int32 c1 = static_cast<int32>( toLowerChar( lhs[charIndex] ) );
                const int32 c2 = static_cast<int32>( toLowerChar( rhs[charIndex] ) );
                if ( c1 != c2 )
                    return c1 - c2;
            }
        }
        else
        {
            for ( size_t charIndex = 0; charIndex < minLen; ++charIndex )
            {
                const int32 c1 = static_cast<int32>( lhs[charIndex] );
                const int32 c2 = static_cast<int32>( rhs[charIndex] );
                if ( c1 != c2 )
                    return c1 - c2;
            }
        }
        if ( lhs.size() < rhs.size() )
            return -1;
        if ( lhs.size() > rhs.size() )
            return 1;
        return 0;
    }

    int32 StringUtil::compare( const utf8* pLhs, const utf8* pRhs, bool bIgnoreCase ) noexcept
    {
        if ( pLhs == pRhs )
            return 0;
        if ( pLhs == nullptr )
            return -1;
        if ( pRhs == nullptr )
            return 1;
        if ( bIgnoreCase )
        {
            while ( *pLhs != '\0' && *pRhs != '\0' )
            {
                const int32 c1 = static_cast<int32>( static_cast<uint8>( toLowerChar( *pLhs ) ) );
                const int32 c2 = static_cast<int32>( static_cast<uint8>( toLowerChar( *pRhs ) ) );
                if ( c1 != c2 )
                    return c1 - c2;
                ++pLhs;
                ++pRhs;
            }
            return static_cast<int32>( static_cast<uint8>( *pLhs ) ) - static_cast<int32>( static_cast<uint8>( *pRhs ) );
        }
        return std::strcmp( pLhs, pRhs );
    }

    int32 StringUtil::compare( const utf16* pLhs, const utf16* pRhs, bool bIgnoreCase ) noexcept
    {
        if ( pLhs == pRhs )
            return 0;
        if ( pLhs == nullptr )
            return -1;
        if ( pRhs == nullptr )
            return 1;
        if ( bIgnoreCase )
        {
            while ( *pLhs != L'\0' && *pRhs != L'\0' )
            {
                const int32 c1 = static_cast<int32>( toLowerChar( *pLhs ) );
                const int32 c2 = static_cast<int32>( toLowerChar( *pRhs ) );
                if ( c1 != c2 )
                    return c1 - c2;
                ++pLhs;
                ++pRhs;
            }
            return static_cast<int32>( *pLhs ) - static_cast<int32>( *pRhs );
        }
        return std::wcscmp( pLhs, pRhs );
    }

    bool StringUtil::startsWith( string_view str, string_view prefix, bool bIgnoreCase ) noexcept
    {
        if ( prefix.empty() )
            return true;
        if ( str.size() < prefix.size() )
            return false;
        if ( bIgnoreCase )
        {
            const string_view head = str.substr( 0, prefix.size() );
            return equals( head, prefix, true );
        }
        return str.compare( 0, prefix.size(), prefix ) == 0;
    }

    bool StringUtil::endsWith( string_view str, string_view suffix, bool bIgnoreCase ) noexcept
    {
        if ( suffix.empty() )
            return true;
        if ( str.size() < suffix.size() )
            return false;
        if ( bIgnoreCase )
        {
            const string_view tail = str.substr( str.size() - suffix.size() );
            return equals( tail, suffix, true );
        }
        return str.compare( str.size() - suffix.size(), suffix.size(), suffix ) == 0;
    }

    bool StringUtil::contains( string_view str, string_view sub, bool bIgnoreCase ) noexcept
    {
        if ( sub.empty() )
            return true;
        if ( str.size() < sub.size() )
            return false;
        if ( bIgnoreCase == false )
            return str.find( sub ) != string_view::npos;

        // 대소문자를 무시할 때는 시작 위치마다 `equals` 로 확인한다. 사본을 만들지 않는다.
        const size_t lastStart = str.size() - sub.size();
        for ( size_t start = 0; start <= lastStart; ++start )
        {
            if ( equals( str.substr( start, sub.size() ), sub, true ) )
                return true;
        }
        return false;
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

    uint32 StringUtil::strlen( const utf8* pStr )
    {
        if ( isNullOrEmpty( pStr ) )
            return 0;
        return static_cast<uint32>( std::char_traits<utf8>::length( pStr ) );
    }

    uint32 StringUtil::strlen( const utf16* pStr )
    {
        if ( isNullOrEmpty( pStr ) )
            return 0;
        return static_cast<uint32>( std::char_traits<utf16>::length( pStr ) );
    }

    void StringUtil::strncpy( utf8* pOutDest, const utf8* pSource, const uint32 length )
    {
        StringUtilInternal::copyTerminated( pOutDest, pSource, length );
    }

    void StringUtil::strncpy( utf16* pOutDest, const utf16* pSource, const uint32 length )
    {
        StringUtilInternal::copyTerminated( pOutDest, pSource, length );
    }

    const utf8* StringUtil::strstr( const utf8* pStr, const utf8* pSubstr )
    {
        if ( isNullOrEmpty( pStr ) || isNullOrEmpty( pSubstr ) )
            return nullptr;
        return ::strstr( pStr, pSubstr );
    }

    const utf16* StringUtil::strstr( const utf16* pStr, const utf16* pSubstr )
    {
        if ( isNullOrEmpty( pStr ) || isNullOrEmpty( pSubstr ) )
            return nullptr;
        return wcsstr( pStr, pSubstr );
    }

    const utf8* StringUtil::stristr( const utf8* pStr, const utf8* pSubstr )
    {
        if ( isNullOrEmpty( pStr ) || isNullOrEmpty( pSubstr ) )
            return nullptr;

        const size_t subLen = strlen( pSubstr );
        while ( *pStr != '\0' )
        {
            if ( equals( string_view( pStr, subLen ), string_view( pSubstr, subLen ), true ) )
                return pStr;
            ++pStr;
        }
        return nullptr;
    }

    const utf16* StringUtil::stristr( const utf16* pStr, const utf16* pSubstr )
    {
        if ( isNullOrEmpty( pStr ) || isNullOrEmpty( pSubstr ) )
            return nullptr;

        const size_t subLen = strlen( pSubstr );
        while ( *pStr != L'\0' )
        {
            if ( equals( wstring_view( pStr, subLen ), wstring_view( pSubstr, subLen ), true ) )
                return pStr;
            ++pStr;
        }
        return nullptr;
    }

    const utf8* StringUtil::strchr( const utf8* pStr, const utf8 ch )
    {
        if ( pStr == nullptr )
            return nullptr;
        return ::strchr( pStr, ch );
    }

    const utf16* StringUtil::strchr( const utf16* pStr, const utf16 ch )
    {
        if ( pStr == nullptr )
            return nullptr;
        return wcschr( pStr, ch );
    }

    bool StringUtil::parseBool( string_view token, bool bFallback )
    {
        const string_view trimmed = trim( token );
        if ( trimmed.empty() )
            return bFallback;
        if ( trimmed == "1" || equals( trimmed, "true", true ) || equals( trimmed, "yes", true ) ||
             equals( trimmed, "on", true ) )
            return true;
        if ( trimmed == "0" || equals( trimmed, "false", true ) || equals( trimmed, "no", true ) ||
             equals( trimmed, "off", true ) )
            return false;
        return bFallback;
    }

    bool StringUtil::parseFloat( string_view token, float32& outValue )
    {
        string_view trimmed = trim( token );
        if ( trimmed.empty() )
            return false;
        if ( trimmed.front() == '+' )
            trimmed.remove_prefix( 1 );
        if ( trimmed.empty() )
            return false;

        float32 val{ 0.0f };
        const auto [ptr, ec] = std::from_chars( trimmed.data(), trimmed.data() + trimmed.size(), val );
        if ( ec == std::errc{} && ptr == trimmed.data() + trimmed.size() )
        {
            outValue = val;
            return true;
        }
        return false;
    }

    bool StringUtil::parseDouble( string_view token, float64& outValue )
    {
        string_view trimmed = trim( token );
        if ( trimmed.empty() )
            return false;
        if ( trimmed.front() == '+' )
            trimmed.remove_prefix( 1 );
        if ( trimmed.empty() )
            return false;

        float64 val{ 0.0 };
        const auto [ptr, ec] = std::from_chars( trimmed.data(), trimmed.data() + trimmed.size(), val );
        if ( ec == std::errc{} && ptr == trimmed.data() + trimmed.size() )
        {
            outValue = val;
            return true;
        }
        return false;
    }

    bool StringUtil::parseInt( string_view token, int32& outValue, int32 base )
    {
        return StringUtilInternal::parseSignedInteger<int32, uint32>( token, base, MathUtil::MinInt32, MathUtil::MaxInt32, outValue );
    }

    bool StringUtil::parseInt64( string_view token, int64& outValue, int32 base )
    {
        return StringUtilInternal::parseSignedInteger<int64, uint64>( token, base, MathUtil::MinInt64, MathUtil::MaxInt64, outValue );
    }

    bool StringUtil::parseUint64( string_view token, uint64& outValue, int32 base )
    {
        string_view digits;
        bool        bNegative{ false };
        if ( StringUtilInternal::splitIntegerToken( token, false, base, digits, bNegative ) == false )
            return false;
        return StringUtilInternal::parseDigitsExact( digits, base, outValue );
    }

    bool StringUtil::isValidUtf8( const utf8* pInput )
    {
        if ( pInput == nullptr )
            return false;
        return StringUtilInternal::isValidUtf8( reinterpret_cast<const uint8*>( pInput ), strlen( pInput ) );
    }

    StringChangeSpan StringUtil::makeChangeSpan( string_view before, string_view after )
    {
        StringChangeSpan span{};
        const size_t     beforeSize = before.size();
        const size_t     afterSize  = after.size();
        const size_t     minSize    = ( beforeSize < afterSize ) ? beforeSize : afterSize;
        size_t           prefix     = 0;
        while ( prefix < minSize && before[prefix] == after[prefix] )
            ++prefix;

        size_t suffix = 0;
        while ( suffix < ( beforeSize - prefix ) && suffix < ( afterSize - prefix ) &&
                before[beforeSize - 1 - suffix] == after[afterSize - 1 - suffix] )
            ++suffix;

        span._prefixLength = static_cast<uint32>( prefix );
        span._suffixLength = static_cast<uint32>( suffix );
        span._removed      = string{ before.substr( prefix, beforeSize - prefix - suffix ) };
        span._added        = string{ after.substr( prefix, afterSize - prefix - suffix ) };
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
        // `data()` 를 넘기지만 **길이도 함께** 넘기므로, 널 종료를 기대하는 호출이 아니다.
        // 검사기가 커스텀 string 의 (ptr, count) 오버로드를 알아보지 못한다.
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
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
        // 위와 같다. 길이를 함께 넘긴다.
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        result.append( beforeText.data(), prefixLength );
        result.append( span._added );
        if ( suffixLength > 0 )
            result.append( beforeText.data() + ( beforeText.size() - suffixLength ), suffixLength );
        return result;
    }
} // namespace sw
