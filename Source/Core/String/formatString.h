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
			Binary	 = 2,
			Octal	 = 8,
			Decimal	 = 10,
			Hex		 = 16,
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
				None			 = 0,
				Precision		 = SW_BIT( 0 ),
				Width			 = SW_BIT( 1 ),
				ShowSign		 = SW_BIT( 2 ),
				LeftAlign		 = SW_BIT( 3 ),
				ZeroPad			 = SW_BIT( 4 ),
				ShowBoolAsString = SW_BIT( 5 ),
			};

			uint8 _precision = 6;
			uint8 _width{ 0 };
			Flags _flags = Flags::None;
			Base  _base	 = Base::Decimal;

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

		/** @brief 0 이면 값, 1 이면 Format 입니다 (tuple 프로토콜). */
		template <uint32 TIndex>
		constexpr decltype( auto ) get() const noexcept
		{
			if constexpr ( TIndex == 0 )
				return getValue();
			else if constexpr ( TIndex == 1 )
				return getFormat();
		}

	private:
		T	   _value;
		Format _format;
	};

	/** @brief 주어진 값과 포맷 설정을 래핑하여 생성하는 헬퍼 함수입니다. */
	template <typename T>
	constexpr FormattedValue<T> Fmt( T&& value, const Format& format ) noexcept { return FormattedValue<T>( std::forward<T>( value ), format ); }

	// ------------------------------------------------------------------------------
	// 3) FormatString — %# 를 인자로 치환해 버퍼에 씀
	//    자유 함수 formatstring 이 이 static 을 호출
	// ------------------------------------------------------------------------------
	/** @brief printf 스타일 %# 포맷을 버퍼에 씁니다. */
	class FormatString
	{
	public:
		/** @brief 포맷 문자열을 버퍼에 씁니다. */
		template <typename... Args>
		static void formatstring( utf8* SW_RESTRICT buffer, uint32 capacity, string_view format, Args&&... args ) noexcept
		{
			SW_ASSERT( buffer != nullptr && capacity > 0 );

			uint32 pos{ 0 };
			if constexpr ( sizeof...( args ) > 0 )
				pos = formatInternal( buffer, 0, capacity, format, std::forward<Args>( args )... );
			else
				pos = write( buffer, 0, capacity, format );

			buffer[MathUtil::min( pos, capacity - 1 )] = '\0';
		}

	private:
		static constexpr string_view kDigitLower		= "0123456789abcdefghijklmnopqrstuvwxyz";
		static constexpr string_view kDigitUpper		= "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
		static constexpr size_t		 kIntegerBufferSize = 64;
		static constexpr size_t		 kFloatBufferSize	= 128;
		static constexpr size_t		 kTempBufferSize	= 256;

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

		/** @brief 다음 플레이스홀더를 찾습니다. */
		static PlaceholderMatch findNextPlaceholder( string_view format ) noexcept
		{
			PlaceholderMatch match;
			size_t			 charIndex{ 0 };
			while ( charIndex < format.size() )
			{
				if ( format[charIndex] == '%' )
				{
					if ( charIndex + 1 < format.size() )
					{
						utf8 nextChar = format[charIndex + 1];
						if ( nextChar == '%' )
						{
							charIndex += 2;
							continue;
						}

						match._pos = charIndex;
						if ( nextChar == '#' || nextChar == 's' || nextChar == 'd' || nextChar == 'i' ||
							 nextChar == 'u' || nextChar == 'f' || nextChar == 'c' || nextChar == 'g' || nextChar == 'e' )
						{
							match._len = 2;
							return match;
						}
						if ( nextChar == 'x' )
						{
							match._len = 2;
							match._overrideFormat.hex();
							match._bHasOverrideFormat = true;
							return match;
						}
						if ( nextChar == 'X' )
						{
							match._len = 2;
							match._overrideFormat.hexUpper();
							match._bHasOverrideFormat = true;
							return match;
						}
						if ( nextChar == 'p' )
						{
							match._len = 2;
							match._overrideFormat.hex();
							match._bHasOverrideFormat = true;
							return match;
						}

						size_t remain = format.size() - ( charIndex + 1 );
						if ( remain >= 3 && format.substr( charIndex + 1, 2 ) == "ll" )
						{
							utf8 spec = format[charIndex + 3];
							if ( spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' || spec == 'X' )
							{
								match._len = 4;
								if ( spec == 'x' )
								{
									match._overrideFormat.hex();
									match._bHasOverrideFormat = true;
								}
								else if ( spec == 'X' )
								{
									match._overrideFormat.hexUpper();
									match._bHasOverrideFormat = true;
								}
								return match;
							}
						}
						if ( remain >= 2 && ( nextChar == 'l' || nextChar == 'z' || nextChar == 'h' ) )
						{
							utf8 spec = format[charIndex + 2];
							if ( spec == 'd' || spec == 'i' || spec == 'u' || spec == 'f' || spec == 'x' || spec == 'X' )
							{
								match._len = 3;
								if ( spec == 'x' )
								{
									match._overrideFormat.hex();
									match._bHasOverrideFormat = true;
								}
								else if ( spec == 'X' )
								{
									match._overrideFormat.hexUpper();
									match._bHasOverrideFormat = true;
								}
								return match;
							}
						}

						match._len = 2;
						return match;
					}
				}
				++charIndex;
			}
			return match;
		}

		/** @brief 기록합니다. */
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

		/** @brief 내부 포맷팅을 수행합니다. */
		template <typename T, typename... Args>
		static uint32 formatInternal( utf8* SW_RESTRICT pBuffer, uint32 pos, uint32 capacity, string_view format, T&& value, Args&&... args ) noexcept
		{
			PlaceholderMatch match = findNextPlaceholder( format );
			if ( match._pos != string_view::npos )
			{
				pos = writeFormatPrefix( pBuffer, pos, capacity, format.substr( 0, match._pos ) );
				if ( match._bHasOverrideFormat )
					pos = addValueWithFormat( pBuffer, pos, capacity, std::forward<T>( value ), match._overrideFormat );
				else
					pos = addValue( pBuffer, pos, capacity, std::forward<T>( value ) );

				string_view nextFormat = format.substr( match._pos + match._len );
				if constexpr ( sizeof...( args ) > 0 )
					return formatInternal( pBuffer, pos, capacity, nextFormat, std::forward<Args>( args )... );
				else
					return writeFormatPrefix( pBuffer, pos, capacity, nextFormat );
			}

			return writeFormatPrefix( pBuffer, pos, capacity, format );
		}

		/** @brief 기록합니다. */
		SW_INLINE static uint32 write( utf8* SW_RESTRICT pBuffer, const uint32 pos, const uint32 capacity, string_view str ) noexcept
		{
			if ( pos >= capacity - 1 )
				return pos;

			const uint32 copyLength = MathUtil::min( static_cast<uint32>( str.length() ), capacity - 1 - pos );
			if ( copyLength > 0 )
				Memory::copy( pBuffer + pos, str.data(), copyLength );
			return pos + copyLength;
		}

		/** @brief 추가합니다. */
		template <typename T>
		static uint32 addValue( utf8* SW_RESTRICT pBuffer, const uint32 pos, const uint32 capacity, T&& value ) noexcept
		{
			utf8 pTemp[kTempBufferSize];

			if constexpr ( is_formatted_value_v<T> )
			{
				const uint32 valueLength = valueToString( pTemp, value.getValue(), value.getFormat() );
				return addPadding( pBuffer, pos, capacity, string_view{ pTemp, valueLength }, value.getFormat() );
			}
			else
			{
				const uint32 valueLength = valueToString( pTemp, std::forward<T>( value ), Format{} );
				return write( pBuffer, pos, capacity, string_view{ pTemp, valueLength } );
			}
		}

		/** @brief 추가합니다. */
		template <typename T>
		static uint32 addValueWithFormat( utf8* SW_RESTRICT pBuffer, const uint32 pos, const uint32 capacity, T&& value, const Format& format ) noexcept
		{
			utf8		 pTemp[kTempBufferSize];
			const uint32 valueLength = valueToString( pTemp, std::forward<T>( value ), format );
			return write( pBuffer, pos, capacity, string_view{ pTemp, valueLength } );
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
					const uint32 len	 = MathUtil::min( static_cast<uint32>( utf8Str.size() ), static_cast<uint32>( kTempBufferSize - 1 ) );
					Memory::copy( pBuf, utf8Str.data(), len );
					pBuf[len] = '\0';
					return len;
				}
				else
				{
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
				const uint32 len	 = MathUtil::min( static_cast<uint32>( utf8Str.size() ), static_cast<uint32>( kTempBufferSize - 1 ) );
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
				SW_ASSERT( false );
				return "[unsupported type]";
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
					*pPtr++		  = '-';
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

		/** @brief 정수를 문자열로 변환합니다(폴백). */
		static uint32 fallbackIntegerToString( utf8* pBuf, uint64 value, Format::Base base ) noexcept
		{
			if ( value == 0 )
			{
				pBuf[0] = '0';
				pBuf[1] = '\0';
				return 1;
			}

			const auto&	 digits	   = ( base == Format::Base::HexUpper ) ? kDigitUpper : kDigitLower;
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

		/** @brief 정수 고속 변환을 시도합니다. */
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
							*p = *p - 'a' + 'A';
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
			if ( std::isnan( value ) )
			{
				Memory::copy( pBuf, "nan", 4 );
				return 3;
			}
			if ( std::isinf( value ) )
			{
				if ( value > 0 )
				{
					Memory::copy( pBuf, "inf", 4 );
					return 3;
				}
				Memory::copy( pBuf, "-inf", 5 );
				return 4;
			}

			utf8* p = pBuf;
			if ( value < 0 )
			{
				*p++  = '-';
				value = -value;
			}
			else if ( format.isShowSign() )
				*p++ = '+';

			const int32 precision = format.hasPrecision() ? format.getPrecision() : 6;
			auto [pPtr, ec]		  = std::to_chars( p, pBuf + kFloatBufferSize, value, std::chars_format::fixed, precision );

			if ( static_cast<int32>( ec ) == 0 )
			{
				*pPtr = '\0';
				return static_cast<size_t>( pPtr - pBuf );
			}
			return static_cast<size_t>( p - pBuf ) + fallbackFloatToString( p, value, precision );
		}

		/** @brief 실수를 문자열로 변환합니다(폴백). */
		static size_t fallbackFloatToString( utf8* pBuf, const float64 value, const int32 precision ) noexcept
		{
			utf8*  p		= pBuf;
			uint64 int_part = static_cast<uint64>( value );

			if ( int_part == 0 )
				*p++ = '0';
			else
			{
				utf8*  pIntStart = p;
				uint64 temp		 = int_part;
				while ( temp > 0 )
				{
					*p++ = '0' + ( temp % 10 );
					temp /= 10;
				}
				std::reverse( pIntStart, p );
			}

			if ( precision > 0 )
			{
				*p++		 = '.';
				float64 frac = value - static_cast<float64>( int_part );
				for ( int32 precisionIndex = 0; precisionIndex < precision; ++precisionIndex )
				{
					frac *= 10.0;
					const int32 digit = static_cast<int32>( frac ) % 10;
					*p++			  = static_cast<utf8>( '0' + digit );
					frac -= digit;
				}
			}
			return static_cast<size_t>( p - pBuf );
		}

		/** @brief 추가합니다. */
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

namespace std
{
	template <typename T>
	/** @brief FormattedValue 는 (값, Format) 두 원소입니다. */
	struct tuple_size<sw::FormattedValue<T>> : integral_constant<uint32, 2>
	{
	};

	template <uint32 TIndex, typename TType>
	/** @brief 0 은 값 타입, 1 은 Format 입니다. */
	struct tuple_element<TIndex, sw::FormattedValue<TType>>
	{

		using type = conditional_t<TIndex == 0, TType, sw::Format>;
	};
} // namespace std
