/**
 * @file FormatString.h
 * @brief printf 스타일 포맷 헬퍼
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) Format — 정밀도·너비·기수·정렬. Fmt(value, Format) 로 인자별 지정
    // ------------------------------------------------------------------------------
    /** @brief %# 플레이스홀더에 붙는 출력 옵션입니다. */
    class Format
    {
        friend class FormatString;

    public:
        /** @brief 너비 안에서의 정렬입니다. */
        enum class Alignment : uint8
        {
            Right,
            Left
        };

        /** @brief 정수 기수입니다. HexUpper 는 대문자 A-F. */
        enum class Base : uint8
        {
            Binary   = 2,
            Octal    = 8,
            Decimal  = 10,
            Hex      = 16,
            HexUpper = 17
        };

        /** @brief 너비를 채울 문자입니다. */
        enum class Padding : uint8
        {
            Space,
            Zero
        };

    private:
        /** @brief 정밀도·너비·플래그·기수를 한곳에 둡니다. */
        struct FormatData
        {
            /** @brief 지정된 옵션 비트입니다. */
            enum class Flags : uint8
            {
                None             = 0,
                Precision        = SW_BIT( 0 ),
                Width            = SW_BIT( 1 ),
                ShowSign         = SW_BIT( 2 ),
                LeftAlign        = SW_BIT( 3 ),
                ZeroPad          = SW_BIT( 4 ),
                ShowBoolAsString = SW_BIT( 5 ),
            };

            uint8 _precision = 6;
            uint8 _width{ 0 };
            Flags _flags = Flags::None;
            Base  _base  = Base::Decimal;

            /** @brief 플래그 설정 여부를 반환합니다. */
            constexpr bool hasFlag( Flags flag ) const noexcept { return ( static_cast<uint8>( _flags ) & static_cast<uint8>( flag ) ) != 0; }
            /** @brief 플래그를 켭니다. */
            constexpr void setFlag( Flags flag ) noexcept { _flags = static_cast<Flags>( static_cast<uint8>( _flags ) | static_cast<uint8>( flag ) ); }
            /** @brief 플래그를 끕니다. */
            constexpr void clearFlag( Flags flag ) noexcept { _flags = static_cast<Flags>( static_cast<uint8>( _flags ) & ~static_cast<uint8>( flag ) ); }
        };

    public:
        /** @brief 기본 정밀도 6, 십진, 오른쪽 정렬입니다. */
        constexpr Format() noexcept = default;

        /** @brief 소수 자릿수를 지정합니다. */
        constexpr explicit Format( const int32 precision ) noexcept
        {
            _data._precision = static_cast<uint8>( MathUtil::clamp( precision, 0, 255 ) );
            _data.setFlag( FormatData::Flags::Precision );
        }

        /** @brief 정수 기수를 지정합니다. */
        constexpr explicit Format( const Base base ) noexcept { _data._base = base; }

        /** @brief 너비와 정렬을 지정합니다. */
        constexpr Format( const int32 width, const Alignment align ) noexcept
        {
            _data._width = static_cast<uint8>( MathUtil::clamp( width, 0, 255 ) );
            _data.setFlag( FormatData::Flags::Width );
            if ( align == Alignment::Left )
                _data.setFlag( FormatData::Flags::LeftAlign );
        }

        /** @brief 너비와 채움 문자를 지정합니다. */
        constexpr Format( const int32 width, const Padding pad ) noexcept
        {
            _data._width = static_cast<uint8>( MathUtil::clamp( width, 0, 255 ) );
            _data.setFlag( FormatData::Flags::Width );
            if ( pad == Padding::Zero )
                _data.setFlag( FormatData::Flags::ZeroPad );
        }

        /** @brief 정밀도를 설정합니다. */
        constexpr Format& precision( const int32 precision ) noexcept
        {
            _data._precision = static_cast<uint8>( MathUtil::clamp( precision, 0, 255 ) );
            _data.setFlag( FormatData::Flags::Precision );
            return *this;
        }

        /** @brief 너비(width)를 설정합니다. */
        constexpr Format& width( const int32 width ) noexcept
        {
            _data._width = static_cast<uint8>( MathUtil::clamp( width, 0, 255 ) );
            _data.setFlag( FormatData::Flags::Width );
            return *this;
        }

        /** @brief 왼쪽 정렬을 설정합니다. */
        constexpr Format& leftAlign() noexcept
        {
            _data.setFlag( FormatData::Flags::LeftAlign );
            return *this;
        }

        /** @brief 오른쪽 정렬을 설정합니다. */
        constexpr Format& rightAlign() noexcept
        {
            _data.clearFlag( FormatData::Flags::LeftAlign );
            return *this;
        }

        /** @brief 0으로 채우기를 설정합니다. */
        constexpr Format& zeroPad() noexcept
        {
            _data.setFlag( FormatData::Flags::ZeroPad );
            return *this;
        }

        /** @brief 공백으로 채우기를 설정합니다. */
        constexpr Format& spacePad() noexcept
        {
            _data.clearFlag( FormatData::Flags::ZeroPad );
            return *this;
        }

        /** @brief 부호를 표시하도록 설정합니다. */
        constexpr Format& showSign() noexcept
        {
            _data.setFlag( FormatData::Flags::ShowSign );
            return *this;
        }

        /** @brief 부호를 숨기도록 설정합니다. */
        constexpr Format& hideSign() noexcept
        {
            _data.clearFlag( FormatData::Flags::ShowSign );
            return *this;
        }

        /** @brief Boolean 값을 문자열로 표시하도록 설정합니다. */
        constexpr Format& showBoolAsString() noexcept
        {
            _data.setFlag( FormatData::Flags::ShowBoolAsString );
            return *this;
        }

        /** @brief Boolean 값을 숫자로 표시하도록 설정합니다. */
        constexpr Format& showBoolAsNumber() noexcept
        {
            _data.clearFlag( FormatData::Flags::ShowBoolAsString );
            return *this;
        }

        /** @brief 16진수 소문자로 표시합니다. */
        constexpr Format& hex() noexcept
        {
            _data._base = Base::Hex;
            return *this;
        }

        /** @brief 16진수 대문자로 표시합니다. */
        constexpr Format& hexUpper() noexcept
        {
            _data._base = Base::HexUpper;
            return *this;
        }

        /** @brief 2진수로 표시합니다. */
        constexpr Format& binary() noexcept
        {
            _data._base = Base::Binary;
            return *this;
        }

        /** @brief 8진수로 표시합니다. */
        constexpr Format& octal() noexcept
        {
            _data._base = Base::Octal;
            return *this;
        }

        /** @brief 10진수로 표시합니다. */
        constexpr Format& decimal() noexcept
        {
            _data._base = Base::Decimal;
            return *this;
        }

        /** @brief 정밀도 지정 여부를 반환합니다. */
        constexpr bool hasPrecision() const noexcept { return _data.hasFlag( FormatData::Flags::Precision ); }
        /** @brief 너비 지정 여부를 반환합니다. */
        constexpr bool hasWidth() const noexcept { return _data.hasFlag( FormatData::Flags::Width ); }
        /** @brief 부호 표시 여부를 반환합니다. */
        constexpr bool isShowSign() const noexcept { return _data.hasFlag( FormatData::Flags::ShowSign ); }
        /** @brief 왼쪽 정렬 여부를 반환합니다. */
        constexpr bool isLeftAlign() const noexcept { return _data.hasFlag( FormatData::Flags::LeftAlign ); }
        /** @brief 0 채움 여부를 반환합니다. */
        constexpr bool isZeroPad() const noexcept { return _data.hasFlag( FormatData::Flags::ZeroPad ); }
        /** @brief 불리언 문자열 표시 여부를 반환합니다. */
        constexpr bool isShowBoolAsString() const noexcept { return _data.hasFlag( FormatData::Flags::ShowBoolAsString ); }
        /** @brief 정밀도를 반환합니다. */
        constexpr int32 getPrecision() const noexcept { return _data._precision; }
        /** @brief 너비를 반환합니다. */
        constexpr int32 getWidth() const noexcept { return _data._width; }
        /** @brief 기수를 반환합니다. */
        constexpr Base getBase() const noexcept { return _data._base; }

    private:
        FormatData _data;
    };

    // ------------------------------------------------------------------------------
    // 2) FormattedValue / Fmt — 값과 Format 을 한 인자로 묶음
    // ------------------------------------------------------------------------------
    template <typename T>
    /** @brief 값과 Format 을 같이 넘길 때 씁니다. */
    class FormattedValue
    {
    public:
        /** @brief 이동 생성합니다. */
        constexpr FormattedValue( T&& value, const Format& format ) noexcept
            : _value{ std::forward<T>( value ) }
            , _format{ format } {}

        /** @brief 포맷할 값입니다. */
        constexpr T getValue() const noexcept { return _value; }
        /** @brief 이 값에 적용할 Format 입니다. */
        constexpr const Format& getFormat() const noexcept { return _format; }

    private:
        T      _value;
        Format _format;
    };

    /** @brief 주어진 값과 포맷 설정을 래핑하여 생성하는 헬퍼 함수입니다. */
    template <typename T>
    constexpr FormattedValue<T> Fmt( T&& value, const Format& format ) noexcept { return FormattedValue<T>( std::forward<T>( value ), format ); }

    // ------------------------------------------------------------------------------
    // 3) FormatString — %# 를 인자로 치환해 버퍼에 씀
    //    자유 함수 formatstring 이 이 static 을 호출
    // ------------------------------------------------------------------------------
    /**
     * @brief 포맷 문자열을 버퍼에 씁니다. 자리표는 두 종류다.
     * @details - `%#` — 옵션이 붙지 않는 순수 자리표. 두 글자만 소비하고 뒤 글자는 리터럴이다(`%#dB`, `%#x%#`, `%#.txt`).
     *          - printf 형 — 서식이 필요하면 이쪽: `%3d`, `%-20s`, `%08x`, `%.2f`, `%+d`. 플래그·너비·정밀도·길이 수식어·
     *            변환 문자를 printf 대로 읽되 타입은 인자가 정한다(`%d` 에 문자열을 줘도 문자열). 공백 플래그(`% d`)는 없다.
     *          - `%%` 는 퍼센트. 알아볼 수 없는 `%…` 는 리터럴이고 인자를 소비하지 않는다.
     *          - 값 쪽 서식은 `Fmt( value, Format()... )` 로도 준다.
     * @note 인자 수는 Debug 에서 실행 시점에 대조한다 — 자리표를 소비하는 자리에서, 따로 세는 패스 없이. 컴파일 시점 검사는
     *       C++17 에서 함수인 채로는 불가능하다(C++20 `consteval` 포맷 타입으로 갈 때 옮긴다). 실행 시점 검사는 데이터에서
     *       오는 포맷(현지화)까지 본다. Release/Shipping 에선 아무것도 하지 않는다.
     */
    class FormatString
    {
    public:
        /** @brief 포맷 문자열을 버퍼에 씁니다. */
        template <typename... Args>
        static void formatstring( utf8* SW_RESTRICT pBuffer, uint32 capacity, string_view format, Args&&... args ) noexcept
        {
            SW_ASSERT( pBuffer != nullptr && capacity > 0 );

            uint32 pos{ 0 };
            if constexpr ( sizeof...( args ) > 0 )
            {
                pos = formatInternal( pBuffer, 0, capacity, format, std::forward<Args>( args )... );
            }
            else
            {
                // 인자가 없으니 자리표도 없어야 한다 — 있으면 호출부가 인자를 빠뜨린 것이다.
#if defined( SW_DEBUG )
                if ( findNextPlaceholder( format )._pos != string_view::npos )
                    reportArgumentMismatch( format, "placeholders but no arguments" );
#endif
                pos = write( pBuffer, 0, capacity, format );
            }

            pBuffer[MathUtil::min( pos, capacity - 1 )] = '\0';
        }

        /** @brief 자리표 수와 인자 수가 어긋난 호출을 Debug 에서 세웁니다 — 어느 포맷인지 stderr 에 찍고(로거는 재귀라 못 쓴다) 디버그 브레이크. */
        static void reportArgumentMismatch( string_view format, const utf8* pReason ) noexcept
        {
            std::fputs( "[formatstring] argument/placeholder mismatch (", stderr );
            std::fputs( pReason, stderr );
            std::fputs( "): \"", stderr );
            std::fwrite( format.data(), 1, format.size(), stderr );
            std::fputs( "\"\n", stderr );
            std::fflush( stderr );
            SW_ASSERT( false && "formatstring: argument/placeholder mismatch" );
        }

        /** @brief 포맷이 소비할 인자 수 — `%#` 과 유효한 printf 서식이 각 1개, `%%`·모르는 `%…` 는 0. 실행 경로는 안 쓰고 테스트의 static_assert 용이다. */
        static constexpr uint32 countPlaceholders( string_view format ) noexcept
        {
            uint32 count{ 0 };
            while ( true )
            {
                const PlaceholderMatch match = findNextPlaceholder( format );
                if ( match._pos == string_view::npos )
                    return count;
                ++count;
                format = format.substr( match._pos + match._len );
            }
        }

    private:
        static constexpr string_view kDigitLower        = "0123456789abcdefghijklmnopqrstuvwxyz";
        static constexpr string_view kDigitUpper        = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        static constexpr size_t      kIntegerBufferSize = 64;
        /** @brief 실수 고정소수점의 최대 길이 — 부호 1 + 정수부 309 (DBL_MAX) + '.' + 정밀도 최대 255 + NUL = 567. to_chars 가 실패할 수 없다. */
        static constexpr size_t kFloatBufferSize = 640;
        static constexpr size_t kTempBufferSize  = kFloatBufferSize;

        template <typename T>
        /** @brief FormattedValue 가 아니면 false 입니다. */
        struct is_formatted_value : std::false_type
        {
        };

        template <typename T>
        /** @brief FormattedValue<T> 특수화는 true 입니다. */
        struct is_formatted_value<FormattedValue<T>> : std::true_type
        {
        };

        template <typename T>
        static constexpr bool is_formatted_value_v = is_formatted_value<std::decay_t<T>>::value;

        /** @brief %# 위치와 그 자리에 쓸 Format 입니다. */
        struct PlaceholderMatch
        {
            size_t _pos = string_view::npos;
            size_t _len{ 0 };
            Format _overrideFormat{};
            bool   _bHasOverrideFormat{ false };
        };

        /**
         * @brief `%` 뒤의 printf 서식(플래그·너비·정밀도·길이·변환)을 읽어 Format 으로 옮깁니다.
         * @details 문법은 printf 와 같다: `% [flags] [width] [.precision] [length] conversion`. 문자를 읽어 Format 의
         *          스위치를 켜 주기만 한다 — 값을 찍는 코드는 손대지 않는다.
         * @param format     `%` 바로 다음부터의 남은 문자열.
         * @param outFormat  읽어 낸 옵션을 켤 Format.
         * @param outHasSpec 하나라도 옵션을 읽었으면 true.
         * @return 소비한 길이(`%` 포함). 서식이 아니면 0.
         */
        static constexpr size_t parseFormatSpec( string_view format, Format& outFormat, bool& outHasSpec ) noexcept
        {
            size_t cursor{ 0 };
            outHasSpec = false;

            // 1) 플래그 — 순서는 상관없고 여러 개가 올 수 있다.
            bool bDone{ false };
            while ( cursor < format.size() && bDone == false )
            {
                switch ( format[cursor] )
                {
                    case '-':
                        outFormat.leftAlign();
                        outHasSpec = true;
                        ++cursor;
                        break;
                    case '+':
                        outFormat.showSign();
                        outHasSpec = true;
                        ++cursor;
                        break;
                    case '0':
                        outFormat.zeroPad();
                        outHasSpec = true;
                        ++cursor;
                        break;
                    // `#` 은 플래그가 아니다 — `%#` 은 findNextPlaceholder 가 두 글자만 소비하는 순수 자리표라 여기 오지 않는다.
                    // 공백도 플래그로 받지 않는다 — 받으면 `%# Failed` 의 'F' 가 변환 문자로 읽힌다. 서식이 아니면 return 0.
                    default:
                        bDone = true;
                        break;
                }
            }

            // 2) 너비
            uint32 widthValue{ 0 };
            bool   bHasWidth{ false };
            while ( cursor < format.size() && format[cursor] >= '0' && format[cursor] <= '9' )
            {
                widthValue = widthValue * 10u + static_cast<uint32>( format[cursor] - '0' );
                bHasWidth  = true;
                ++cursor;
            }
            if ( bHasWidth )
            {
                outFormat.width( static_cast<int32>( widthValue ) );
                outHasSpec = true;
            }

            // 3) 정밀도 — printf 규약 그대로다(`%.f` 는 정밀도 0).
            if ( cursor < format.size() && format[cursor] == '.' )
            {
                ++cursor;
                uint32 precisionValue{ 0 };
                while ( cursor < format.size() && format[cursor] >= '0' && format[cursor] <= '9' )
                {
                    precisionValue = precisionValue * 10u + static_cast<uint32>( format[cursor] - '0' );
                    ++cursor;
                }
                outFormat.precision( static_cast<int32>( precisionValue ) );
                outHasSpec = true;
            }

            // 4) 길이 수식어 — 값의 타입은 인자가 정하므로 읽고 버린다.
            if ( cursor + 1 < format.size() && format.substr( cursor, 2 ) == "ll" )
                cursor += 2;
            else if ( cursor < format.size() &&
                      ( format[cursor] == 'l' || format[cursor] == 'z' || format[cursor] == 'h' || format[cursor] == 'j' ||
                        format[cursor] == 't' ) )
                ++cursor;

            // 5) 변환 문자
            if ( cursor >= format.size() )
                return 0;

            switch ( format[cursor] )
            {
                // 진수 지시자(x/X/o/p)는 Format 으로 옮기고, 나머지 변환 문자는 타입이 인자에서 오므로 읽고 버린다.
                case 'x':
                    outFormat.hex();
                    outHasSpec = true;
                    break;
                case 'X':
                    outFormat.hexUpper();
                    outHasSpec = true;
                    break;
                case 'p':
                    outFormat.hex();
                    outHasSpec = true;
                    break;
                case 'o':
                    outFormat.octal();
                    outHasSpec = true;
                    break;
                case 'd':
                case 'i':
                case 'u':
                case 'f':
                case 'F':
                case 'g':
                case 'G':
                case 'e':
                case 'E':
                case 's':
                case 'c':
                    break;
                default:
                    return 0; // 서식이 아니다
            }
            return cursor + 2; // `%` + 여기까지
        }

        /** @brief 다음 플레이스홀더를 찾습니다. */
        static constexpr PlaceholderMatch findNextPlaceholder( string_view format ) noexcept
        {
            PlaceholderMatch match;
            size_t           charIndex{ 0 };
            // 한 글자씩 훑지 않고 '%' 를 곧장 찾는다 — find 는 memchr 로 내려간다. 포맷 문자열은
            // 대개 리터럴이 길고 플레이스홀더가 드물어서, 훑는 쪽이 이 함수의 대부분이었다.
            while ( ( charIndex = format.find( '%', charIndex ) ) != string_view::npos )
            {
                {
                    if ( charIndex + 1 < format.size() )
                    {
                        // `%%` 는 리터럴 퍼센트다.
                        if ( format[charIndex + 1] == '%' )
                        {
                            charIndex += 2;
                            continue;
                        }

                        // `%#` 은 순수 자리표 — 두 글자만 소비하고 뒤는 리터럴이다. 서식이 필요하면 printf 형을 쓴다.
                        if ( format[charIndex + 1] == '#' )
                        {
                            match._pos = charIndex;
                            match._len = 2;
                            return match;
                        }

                        Format       specFormat{};
                        bool         bHasSpec{ false };
                        const size_t consumed = parseFormatSpec( format.substr( charIndex + 1 ), specFormat, bHasSpec );
                        if ( consumed > 0 )
                        {
                            match._pos                = charIndex;
                            match._len                = consumed;
                            match._overrideFormat     = specFormat;
                            match._bHasOverrideFormat = bHasSpec;
                            return match;
                        }

                        // 알아볼 수 없는 `%…` 는 서식이 아니다 — `%` 를 리터럴로 두고 인자를 소비하지 않는다(소비하면 뒤 인자가 밀린다).
                        ++charIndex;
                        continue;
                    }
                }
                ++charIndex; // 문자열 끝의 '%' — 더 볼 것이 없다
            }
            return match;
        }

        /** @brief 자리표 사이의 리터럴 구간을 씁니다 — `%%` 는 `%` 하나로. */
        static uint32 writeFormatPrefix( utf8* SW_RESTRICT pBuffer, uint32 pos, uint32 capacity, string_view prefix ) noexcept
        {
            size_t prefixIndex{ 0 };
            while ( prefixIndex < prefix.size() && pos < capacity - 1 )
            {
                if ( prefix[prefixIndex] == '%' && prefixIndex + 1 < prefix.size() && prefix[prefixIndex + 1] == '%' )
                {
                    pBuffer[pos++] = '%';
                    prefixIndex += 2;
                }
                else
                    pBuffer[pos++] = prefix[prefixIndex++];
            }
            return pos;
        }

        /** @brief 자리표 하나에 인자 하나를 붙이고 나머지로 재귀합니다. */
        template <typename T, typename... Args>
        static uint32 formatInternal( utf8* SW_RESTRICT pBuffer, uint32 pos, uint32 capacity, string_view format, T&& value, Args&&... args ) noexcept
        {
            PlaceholderMatch match = findNextPlaceholder( format );
            if ( match._pos != string_view::npos )
            {
                pos = writeFormatPrefix( pBuffer, pos, capacity, format.substr( 0, match._pos ) );
                pos = addValue( pBuffer, pos, capacity, std::forward<T>( value ), match._bHasOverrideFormat ? &match._overrideFormat : nullptr );

                string_view nextFormat = format.substr( match._pos + match._len );
                if constexpr ( sizeof...( args ) > 0 )
                {
                    return formatInternal( pBuffer, pos, capacity, nextFormat, std::forward<Args>( args )... );
                }
                else
                {
                    // 마지막 인자를 썼다 — 나머지에 자리표가 남아 있으면 호출부가 인자를 빠뜨린 것이다(리터럴 `%#` 가 남는다).
#if defined( SW_DEBUG )
                    if ( findNextPlaceholder( nextFormat )._pos != string_view::npos )
                        reportArgumentMismatch( nextFormat, "more placeholders than arguments" );
#endif
                    return writeFormatPrefix( pBuffer, pos, capacity, nextFormat );
                }
            }

            // 자리표는 없는데 인자가 남았다 — 호출부가 인자를 더 넘긴 것이다. Release 는 조용히 버린다.
#if defined( SW_DEBUG )
            reportArgumentMismatch( format, "more arguments than placeholders" );
#endif
            return writeFormatPrefix( pBuffer, pos, capacity, format );
        }

        /** @brief 문자열을 용량 안에서 복사합니다 — 잘리면 앞부분만 남는다. */
        SW_INLINE static uint32 write( utf8* SW_RESTRICT pBuffer, const uint32 pos, const uint32 capacity, string_view str ) noexcept
        {
            if ( pos >= capacity - 1 )
                return pos;

            const uint32 copyLength = MathUtil::min( static_cast<uint32>( str.length() ), capacity - 1 - pos );
            if ( copyLength > 0 )
                Memory::copy( pBuffer + pos, str.data(), copyLength );
            return pos + copyLength;
        }

        /**
         * @brief 값 하나를 버퍼에 붙입니다. pSpec 은 서식 문자열(`%5d` 등)이 준 서식이고, 없으면 nullptr.
         * @details 값 변환(기수·정밀도)은 Fmt 가 있으면 Fmt 의 서식, 없으면 pSpec; 너비·정렬은 pSpec 이 있으면 pSpec, 없으면 Fmt.
         *          문자열류는 임시 버퍼를 거치지 않는다(거치면 긴 문자열이 거기서 잘린다). 널 포인터는 지름길을 타면 안 된다 —
         *          `string_view{ nullptr }` 는 strlen(nullptr) 이다(`nullptr` 리터럴도 C++17 에선 string_view 로 변환 "가능").
         */
        template <typename T>
        static uint32 addValue( utf8* SW_RESTRICT pBuffer, const uint32 pos, const uint32 capacity, T&& value, const Format* pSpec ) noexcept
        {
            if constexpr ( is_formatted_value_v<T> == false && std::is_convertible_v<std::decay_t<T>, string_view> &&
                           std::is_null_pointer_v<std::decay_t<T>> == false )
            {
                string_view text{};
                if constexpr ( std::is_pointer_v<std::decay_t<T>> )
                {
                    text = ( value == nullptr ) ? string_view{ "(null)" } : string_view{ value };
                }
                else
                {
                    text = string_view{ value };
                }
                return pSpec != nullptr ? addPadding( pBuffer, pos, capacity, text, *pSpec ) : write( pBuffer, pos, capacity, text );
            }
            else if constexpr ( is_formatted_value_v<T> )
            {
                const Format& padFormat = pSpec != nullptr ? *pSpec : value.getFormat();
                return appendConverted( pBuffer, pos, capacity, value.getValue(), value.getFormat(), padFormat );
            }
            else
            {
                const Format format = pSpec != nullptr ? *pSpec : Format{};
                return appendConverted( pBuffer, pos, capacity, std::forward<T>( value ), format, format );
            }
        }

        /**
         * @brief 문자열이 아닌 값을 변환해 붙입니다 — 너비 맞춤이 없고 목적지에 자리가 있으면 바로 그 자리에 변환한다.
         * @details 그 밖(패딩이 필요하거나 버퍼 끝에 가까울 때)에만 임시를 거친다. 잘림 규칙(앞부분만 남는다)은 write 가 지킨다.
         */
        template <typename T>
        static uint32 appendConverted( utf8* SW_RESTRICT pBuffer, const uint32 pos, const uint32 capacity, T&& value,
                                       const Format& convertFormat, const Format& padFormat ) noexcept
        {
            const uint32 remaining = ( pos + 1 < capacity ) ? ( capacity - 1 - pos ) : 0;
            if ( padFormat.hasWidth() == false && remaining >= kTempBufferSize )
                return pos + valueToString( pBuffer + pos, std::forward<T>( value ), convertFormat );

            utf8         arrTemp[kTempBufferSize];
            const uint32 valueLength = valueToString( arrTemp, std::forward<T>( value ), convertFormat );
            return addPadding( pBuffer, pos, capacity, string_view{ arrTemp, valueLength }, padFormat );
        }

        /** @brief 값을 문자열로 변환합니다. */
        template <typename T>
        static uint32 valueToString( utf8* pBuf, T&& value, const Format& format ) noexcept
        {
            using DecayT = std::decay_t<T>;

            if constexpr ( std::is_integral_v<DecayT> || std::is_enum_v<DecayT> )
            {
                if constexpr ( std::is_same_v<DecayT, bool> )
                {
                    if ( format.isShowBoolAsString() )
                        return valueToString( pBuf, value ? "True" : "False", format );
                    return integerToString( pBuf, value ? 1 : 0, format );
                }
                else if constexpr ( std::is_same_v<DecayT, utf8> )
                {
                    if ( format.getBase() != Format::Base::Decimal || format.hasWidth() || format.isShowSign() )
                        return integerToString( pBuf, static_cast<int32>( value ), format );
                    pBuf[0] = value;
                    pBuf[1] = '\0';
                    return 1;
                }
                else
                    return integerToString( pBuf, value, format );
            }
            else if constexpr ( std::is_floating_point_v<DecayT> )
                return static_cast<uint32>( floatToString( pBuf, static_cast<float64>( value ), format ) );
            else if constexpr ( std::is_pointer_v<DecayT> )
            {
                if constexpr ( std::is_same_v<std::decay_t<std::remove_pointer_t<DecayT>>, utf8> ||
                               std::is_same_v<std::decay_t<std::remove_pointer_t<DecayT>>, utf16> )
                {
                    const string utf8Str = toUTF8String( value );
                    const uint32 len     = MathUtil::min( static_cast<uint32>( utf8Str.size() ), static_cast<uint32>( kTempBufferSize - 1 ) );
                    Memory::copy( pBuf, utf8Str.data(), len );
                    pBuf[len] = '\0';
                    return len;
                }
                else
                {
                    // 널은 종류와 무관하게 (null).
                    if ( value == nullptr )
                    {
                        Memory::copy( pBuf, "(null)", 7 );
                        return 6;
                    }
                    Format hexFormat = format;
                    hexFormat.hexUpper();
                    return integerToString( pBuf, reinterpret_cast<uintptr_t>( value ), hexFormat );
                }
            }
            else if constexpr ( std::is_null_pointer_v<DecayT> )
            {
                Memory::copy( pBuf, "(null)", 6 );
                pBuf[6] = '\0';
                return 6;
            }
            else
            {
                const string utf8Str = toUTF8String( value );
                const uint32 len     = MathUtil::min( static_cast<uint32>( utf8Str.size() ), static_cast<uint32>( kTempBufferSize - 1 ) );
                Memory::copy( pBuf, utf8Str.data(), len );
                pBuf[len] = '\0';
                return len;
            }
        }

        /** @brief UTF-8 문자열로 변환합니다. */
        template <typename StringType>
        static string toUTF8String( const StringType& str )
        {
            using T = std::decay_t<StringType>;
            if constexpr ( std::is_pointer_v<T> )
            {
                if ( str == nullptr )
                    return "(null)";
            }

            if constexpr ( std::is_constructible_v<string_view, T> )
                return string{ string_view{ str } };
            else if constexpr ( std::is_constructible_v<std::wstring_view, T> )
            {
                const wstring wideTemp{ std::wstring_view{ str } };
                return StringUtil::utf16ToUtf8( wideTemp.c_str() );
            }
            else
            {
                // 문자열로 변환할 수 없는 타입은 컴파일 오류다 — 호출부에서 toString() 을 거친다. sizeof(T)==0 은 T 에 의존하는 항상-거짓.
                static_assert( sizeof( T ) == 0, "formatstring: string 으로 변환할 수 없는 타입입니다 - toString() 을 거치십시오" );
                return {};
            }
        }

        /** @brief 정수를 문자열로 변환합니다. */
        template <typename IntType>
        static uint32 integerToString( utf8* pBuf, IntType value, const Format& format ) noexcept
        {
            utf8*  pPtr = pBuf;
            uint64 absoluteValue{ 0 };

            if constexpr ( std::is_signed_v<IntType> )
            {
                if ( value < 0 )
                {
                    *pPtr++       = '-';
                    absoluteValue = 0 - static_cast<uint64>( value );
                }
                else
                {
                    if ( format.isShowSign() )
                        *pPtr++ = '+';
                    absoluteValue = static_cast<uint64>( value );
                }
            }
            else
            {
                if ( format.isShowSign() )
                    *pPtr++ = '+';
                absoluteValue = static_cast<uint64>( value );
            }

            const uint32 len = tryFastIntConversion( pPtr, absoluteValue, format.getBase() );
            if ( len != invalid_index::kUint32 )
                return static_cast<uint32>( pPtr - pBuf ) + len;

            return static_cast<uint32>( pPtr - pBuf ) + fallbackIntegerToString( pPtr, absoluteValue, format.getBase() );
        }

        /** @brief 2진수를 만듭니다 — tryFastIntConversion 이 맡지 않는 기수. */
        static uint32 fallbackIntegerToString( utf8* pBuf, uint64 value, Format::Base base ) noexcept
        {
            if ( value == 0 )
            {
                pBuf[0] = '0';
                pBuf[1] = '\0';
                return 1;
            }

            const auto&  digits    = ( base == Format::Base::HexUpper ) ? kDigitUpper : kDigitLower;
            const uint64 baseValue = ( base == Format::Base::HexUpper ) ? 16ULL : static_cast<uint64>( base );

            utf8* pStart = pBuf;
            while ( value > 0 )
            {
                *pBuf++ = digits[value % baseValue];
                value /= baseValue;
            }

            *pBuf = '\0';
            std::reverse( pStart, pBuf );
            return static_cast<uint32>( pBuf - pStart );
        }

        /** @brief to_chars 로 변환합니다(2진수 제외). 실패하면 invalid_index. */
        template <typename IntType>
        static uint32 tryFastIntConversion( utf8* pBuf, IntType value, Format::Base base )
        {
            if ( base == Format::Base::Binary )
                return invalid_index::kUint32;

            int32 baseValue = ( base == Format::Base::HexUpper ) ? 16 : static_cast<int32>( base );
            auto [pPtr, ec] = std::to_chars( pBuf, pBuf + kIntegerBufferSize, value, baseValue );

            if ( ec == std::errc{} )
            {
                if ( base == Format::Base::HexUpper )
                {
                    for ( utf8* p = pBuf; p != pPtr; ++p )
                    {
                        if ( 'a' <= *p && *p <= 'f' )
                            *p = static_cast<utf8>( *p - 'a' + 'A' );
                    }
                }
                *pPtr = '\0';
                return static_cast<uint32>( pPtr - pBuf );
            }
            return invalid_index::kUint32;
        }

        /** @brief 실수를 문자열로 변환합니다. */
        static size_t floatToString( utf8* pBuf, float64 value, const Format& format ) noexcept
        {
            if ( MathUtil::isNan( value ) )
            {
                Memory::copy( pBuf, "nan", 4 );
                return 3;
            }
            if ( MathUtil::isInfinite( value ) )
            {
                if ( value > 0 )
                {
                    Memory::copy( pBuf, "inf", 4 );
                    return 3;
                }
                Memory::copy( pBuf, "-inf", 5 );
                return 4;
            }

            utf8* pCurrent = pBuf;
            if ( value < 0 )
            {
                *pCurrent++ = '-';
                value       = -value;
            }
            else if ( format.isShowSign() )
                *pCurrent++ = '+';

            const int32 precision = format.hasPrecision() ? format.getPrecision() : 6;
            auto [pPtr, ec]       = std::to_chars( pCurrent, pBuf + kFloatBufferSize, value, std::chars_format::fixed, precision );

            // kFloatBufferSize 가 고정소수점 최대 길이를 담으므로 실패할 수 없다 — 그래도 나면 조용한 쓰레기 대신 표식을 남긴다.
            if ( static_cast<int32>( ec ) != 0 )
            {
                SW_ASSERT( false && "floatToString: to_chars failed" );
                Memory::copy( pCurrent, "?", 2 );
                return static_cast<size_t>( pCurrent - pBuf ) + 1;
            }
            *pPtr = '\0';
            return static_cast<size_t>( pPtr - pBuf );
        }

        /** @brief 너비·정렬·0채움을 적용해 씁니다. 너비가 없으면 write 와 같다. */
        static uint32 addPadding( utf8* SW_RESTRICT pBuffer, uint32 pos, const uint32 capacity, string_view str, const Format& fmt ) noexcept
        {
            const int32 padding = fmt.hasWidth() ? ( fmt.getWidth() - static_cast<int32>( str.size() ) ) : 0;
            if ( padding <= 0 )
                return write( pBuffer, pos, capacity, str );

            const utf8 padChar = fmt.isZeroPad() ? '0' : ' ';

            if ( fmt.isLeftAlign() == false )
            {
                for ( int32 paddingIndex = 0; paddingIndex < padding && pos < capacity - 1; ++paddingIndex )
                {
                    pBuffer[pos++] = padChar;
                }
                pos = write( pBuffer, pos, capacity, str );
            }
            else
            {
                pos = write( pBuffer, pos, capacity, str );
                for ( int32 paddingIndex = 0; paddingIndex < padding && pos < capacity - 1; ++paddingIndex )
                {
                    pBuffer[pos++] = ' ';
                }
            }

            return pos;
        }
    };

    // ------------------------------------------------------------------------------
    // 4) formatstring — FormatString::formatstring 자유 함수 진입점
    // ------------------------------------------------------------------------------
    /**
     * @brief 형식화된 문자열을 버퍼에 작성합니다.
     * @param pBuffer 결과를 저장할 버퍼
     * @param capacity 버퍼의 최대 크기
     * @param format 서식 문자열 (예: "Value = %d")
     * @param args 가변 인자
     */
    template <typename... Args>
    void formatstring( utf8* pBuffer, uint32 capacity, string_view format, Args&&... args ) noexcept { FormatString::formatstring( pBuffer, capacity, format, std::forward<Args>( args )... ); }
} // namespace sw
