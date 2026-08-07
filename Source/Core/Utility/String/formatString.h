#pragma once
/**
 * @file formatString.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Utility/String/StringUtil.h"

namespace sw
{

	class Format
	{
		friend class FormatString;

	public:

		enum class Alignment : uint8
		{
			Right,
			Left
		};

		enum class Base : uint8
		{
			Binary	 = 2,
			Octal	 = 8,
			Decimal	 = 10,
			Hex		 = 16,
			HexUpper = 17
		};

		enum class Padding : uint8
		{
			Space,
			Zero
		};

	private:
		struct FormatData
		{
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
			uint8 _width	 = 0;
			Flags _flags	 = Flags::None;
			Base  _base		 = Base::Decimal;

			constexpr bool hasFlag( Flags flag ) const noexcept { return ( static_cast<uint8>( _flags ) & static_cast<uint8>( flag ) ) != 0; }
			constexpr void setFlag( Flags flag ) noexcept { _flags = static_cast<Flags>( static_cast<uint8>( _flags ) | static_cast<uint8>( flag ) ); }
			constexpr void clearFlag( Flags flag ) noexcept { _flags = static_cast<Flags>( static_cast<uint8>( _flags ) & ~static_cast<uint8>( flag ) ); }
		};

	public:
		constexpr Format() noexcept = default;

		constexpr explicit Format( const int32 precision ) noexcept
		{
			_data._precision = static_cast<uint8>( std::clamp( precision, 0, 255 ) );
			_data.setFlag( FormatData::Flags::Precision );
		}

		constexpr explicit Format( const Base base ) noexcept
		{
			_data._base = base;
		}

		constexpr Format( const int32 width, const Alignment align ) noexcept
		{
			_data._width = static_cast<uint8>( std::clamp( width, 0, 255 ) );
			_data.setFlag( FormatData::Flags::Width );
			if ( align == Alignment::Left )
				_data.setFlag( FormatData::Flags::LeftAlign );
		}

		constexpr Format( const int32 width, const Padding pad ) noexcept
		{
			_data._width = static_cast<uint8>( std::clamp( width, 0, 255 ) );
			_data.setFlag( FormatData::Flags::Width );
			if ( pad == Padding::Zero )
				_data.setFlag( FormatData::Flags::ZeroPad );
		}

		constexpr Format& precision( const int32 precision ) noexcept
		{
			_data._precision = static_cast<uint8>( std::clamp( precision, 0, 255 ) );
			_data.setFlag( FormatData::Flags::Precision );
			return *this;
		}
		constexpr Format& width( const int32 width ) noexcept
		{
			_data._width = static_cast<uint8>( std::clamp( width, 0, 255 ) );
			_data.setFlag( FormatData::Flags::Width );
			return *this;
		}
		constexpr Format& leftAlign() noexcept
		{
			_data.setFlag( FormatData::Flags::LeftAlign );
			return *this;
		}
		constexpr Format& rightAlign() noexcept
		{
			_data.clearFlag( FormatData::Flags::LeftAlign );
			return *this;
		}
		constexpr Format& zeroPad() noexcept
		{
			_data.setFlag( FormatData::Flags::ZeroPad );
			return *this;
		}
		constexpr Format& spacePad() noexcept
		{
			_data.clearFlag( FormatData::Flags::ZeroPad );
			return *this;
		}
		constexpr Format& showSign() noexcept
		{
			_data.setFlag( FormatData::Flags::ShowSign );
			return *this;
		}
		constexpr Format& hideSign() noexcept
		{
			_data.clearFlag( FormatData::Flags::ShowSign );
			return *this;
		}
		constexpr Format& showBoolAsString() noexcept
		{
			_data.setFlag( FormatData::Flags::ShowBoolAsString );
			return *this;
		}
		constexpr Format& showBoolAsNumber() noexcept
		{
			_data.clearFlag( FormatData::Flags::ShowBoolAsString );
			return *this;
		}

		constexpr Format& hex() noexcept
		{
			_data._base = Base::Hex;
			return *this;
		}
		constexpr Format& hexUpper() noexcept
		{
			_data._base = Base::HexUpper;
			return *this;
		}
		constexpr Format& binary() noexcept
		{
			_data._base = Base::Binary;
			return *this;
		}
		constexpr Format& octal() noexcept
		{
			_data._base = Base::Octal;
			return *this;
		}
		constexpr Format& decimal() noexcept
		{
			_data._base = Base::Decimal;
			return *this;
		}

		constexpr bool	hasPrecision() const noexcept { return _data.hasFlag( FormatData::Flags::Precision ); }
		constexpr bool	hasWidth() const noexcept { return _data.hasFlag( FormatData::Flags::Width ); }
		constexpr bool	isShowSign() const noexcept { return _data.hasFlag( FormatData::Flags::ShowSign ); }
		constexpr bool	isLeftAlign() const noexcept { return _data.hasFlag( FormatData::Flags::LeftAlign ); }
		constexpr bool	isZeroPad() const noexcept { return _data.hasFlag( FormatData::Flags::ZeroPad ); }
		constexpr bool	isShowBoolAsString() const noexcept { return _data.hasFlag( FormatData::Flags::ShowBoolAsString ); }
		constexpr int32 getPrecision() const noexcept { return _data._precision; }
		constexpr int32 getWidth() const noexcept { return _data._width; }
		constexpr Base	getBase() const noexcept { return _data._base; }

	private:
		FormatData _data;
	};

	template <typename T>
	class FormattedValue
	{
		T	   _value;
		Format _format;

	public:
		constexpr FormattedValue( T&& value, const Format& format ) noexcept
			: _value( std::forward<T>( value ) )
			, _format{ format }
		{
		}

		constexpr T				getValue() const noexcept { return _value; }
		constexpr const Format& getFormat() const noexcept { return _format; }

		template <uint32 TIndex>
		constexpr decltype( auto ) get() const noexcept
		{
			if constexpr ( TIndex == 0 )
				return getValue();
			else if constexpr ( TIndex == 1 )
				return getFormat();
		}
	};

	template <typename T>
	constexpr FormattedValue<T> Fmt( T&& value, const Format& format ) noexcept
	{
		return FormattedValue<T>( std::forward<T>( value ), format );
	}

	class FormatString
	{
	public:

		template <typename... Args>
		static void formatstring( utf8* SW_RESTRICT buffer, uint32 capacity, std::string_view format, Args&&... args ) noexcept
		{
			SW_ASSERT( buffer != nullptr && capacity > 0 );

			uint32 pos = 0;
			if constexpr ( sizeof...( args ) > 0 )
				pos = formatInternal( buffer, 0, capacity, format, std::forward<Args>( args )... );
			else
				pos = write( buffer, 0, capacity, format );

			buffer[std::min( pos, capacity - 1 )] = '\0';
		}

	private:
		static constexpr std::string_view kDigitLower		 = "0123456789abcdefghijklmnopqrstuvwxyz";
		static constexpr std::string_view kDigitUpper		 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
		static constexpr size_t			  kIntegerBufferSize = 64;
		static constexpr size_t			  kFloatBufferSize	 = 128;
		static constexpr size_t			  kTempBufferSize	 = 256;

		template <typename T>
		struct is_formatted_value : std::false_type
		{
		};

		template <typename T>
		struct is_formatted_value<FormattedValue<T>> : std::true_type
		{
		};

		template <typename T>
		static constexpr bool is_formatted_value_v = is_formatted_value<std::decay_t<T>>::value;

		struct PlaceholderMatch
		{
			size_t pos = std::string_view::npos;
			size_t len = 0;
			Format overrideFormat{};
			bool   hasOverrideFormat = false;
		};

		static PlaceholderMatch findNextPlaceholder( std::string_view format ) noexcept
		{
			PlaceholderMatch match;
			size_t			 charIndex = 0;
			while ( charIndex < format.size() )
			{
				if ( format[charIndex] == '%' )
				{
					if ( charIndex + 1 < format.size() )
					{
						char nextChar = format[charIndex + 1];
						if ( nextChar == '%' )
						{

							charIndex += 2;
							continue;
						}

						match.pos = charIndex;
						if ( nextChar == '#' || nextChar == 's' || nextChar == 'd' || nextChar == 'i' ||
							 nextChar == 'u' || nextChar == 'f' || nextChar == 'c' || nextChar == 'g' || nextChar == 'e' )
						{
							match.len = 2;
							return match;
						}
						else if ( nextChar == 'x' )
						{
							match.len = 2;
							match.overrideFormat.hex();
							match.hasOverrideFormat = true;
							return match;
						}
						else if ( nextChar == 'X' )
						{
							match.len = 2;
							match.overrideFormat.hexUpper();
							match.hasOverrideFormat = true;
							return match;
						}
						else if ( nextChar == 'p' )
						{
							match.len = 2;
							match.overrideFormat.hex();
							match.hasOverrideFormat = true;
							return match;
						}

						size_t remain = format.size() - ( charIndex + 1 );
						if ( remain >= 3 && format.substr( charIndex + 1, 2 ) == "ll" )
						{
							char spec = format[charIndex + 3];
							if ( spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' || spec == 'X' )
							{
								match.len = 4;
								if ( spec == 'x' )
								{
									match.overrideFormat.hex();
									match.hasOverrideFormat = true;
								}
								else if ( spec == 'X' )
								{
									match.overrideFormat.hexUpper();
									match.hasOverrideFormat = true;
								}
								return match;
							}
						}
						if ( remain >= 2 && ( nextChar == 'l' || nextChar == 'z' || nextChar == 'h' ) )
						{
							char spec = format[charIndex + 2];
							if ( spec == 'd' || spec == 'i' || spec == 'u' || spec == 'f' || spec == 'x' || spec == 'X' )
							{
								match.len = 3;
								if ( spec == 'x' )
								{
									match.overrideFormat.hex();
									match.hasOverrideFormat = true;
								}
								else if ( spec == 'X' )
								{
									match.overrideFormat.hexUpper();
									match.hasOverrideFormat = true;
								}
								return match;
							}
						}

						match.len = 2;
						return match;
					}
				}
				++charIndex;
			}
			return match;
		}

		static uint32 writeFormatPrefix( utf8* SW_RESTRICT buffer, uint32 pos, uint32 capacity, std::string_view prefix ) noexcept
		{
			size_t prefixIndex = 0;
			while ( prefixIndex < prefix.size() && pos < capacity - 1 )
			{
				if ( prefix[prefixIndex] == '%' && prefixIndex + 1 < prefix.size() && prefix[prefixIndex + 1] == '%' )
				{
					buffer[pos++] = '%';
					prefixIndex += 2;
				}
				else
				{
					buffer[pos++] = prefix[prefixIndex++];
				}
			}
			return pos;
		}

		template <typename T, typename... Args>
		static uint32 formatInternal( utf8* SW_RESTRICT buffer, uint32 pos, uint32 capacity, std::string_view format, T&& value, Args&&... args ) noexcept
		{
			PlaceholderMatch match = findNextPlaceholder( format );
			if ( match.pos != std::string_view::npos )
			{
				pos = writeFormatPrefix( buffer, pos, capacity, format.substr( 0, match.pos ) );
				if ( match.hasOverrideFormat )
				{
					pos = addValueWithFormat( buffer, pos, capacity, std::forward<T>( value ), match.overrideFormat );
				}
				else
				{
					pos = addValue( buffer, pos, capacity, std::forward<T>( value ) );
				}

				std::string_view nextFormat = format.substr( match.pos + match.len );
				if constexpr ( sizeof...( args ) > 0 )
					return formatInternal( buffer, pos, capacity, nextFormat, std::forward<Args>( args )... );
				else
					return writeFormatPrefix( buffer, pos, capacity, nextFormat );
			}

			return writeFormatPrefix( buffer, pos, capacity, format );
		}

		SW_INLINE static uint32 write( utf8* SW_RESTRICT buffer, const uint32 pos, const uint32 capacity, const std::string_view str ) noexcept
		{
			if ( pos >= capacity - 1 )
				return pos;

			const uint32 copyLength = std::min( static_cast<uint32>( str.length() ), capacity - 1 - pos );
			if ( copyLength > 0 )
				std::memcpy( buffer + pos, str.data(), copyLength );
			return pos + copyLength;
		}

		template <typename T>
		static uint32 addValue( utf8* SW_RESTRICT buffer, const uint32 pos, const uint32 capacity, T&& value ) noexcept
		{
			utf8 temp[kTempBufferSize];

			if constexpr ( is_formatted_value_v<T> )
			{
				const uint32 valueLength = valueToString( temp, value.getValue(), value.getFormat() );
				return addPadding( buffer, pos, capacity, std::string_view{ temp, valueLength }, value.getFormat() );
			}
			else
			{
				const uint32 valueLength = valueToString( temp, std::forward<T>( value ), Format{} );
				return write( buffer, pos, capacity, std::string_view{ temp, valueLength } );
			}
		}

		template <typename T>
		static uint32 addValueWithFormat( utf8* SW_RESTRICT buffer, const uint32 pos, const uint32 capacity, T&& value, const Format& format ) noexcept
		{
			utf8		 temp[kTempBufferSize];
			const uint32 valueLength = valueToString( temp, std::forward<T>( value ), format );
			return write( buffer, pos, capacity, std::string_view{ temp, valueLength } );
		}

		template <typename T>
		static uint32 valueToString( utf8* buf, T&& value, const Format& format ) noexcept
		{
			using DecayT = std::decay_t<T>;

			if constexpr ( std::is_integral_v<DecayT> || std::is_enum_v<DecayT> )
			{
				if constexpr ( std::is_same_v<DecayT, bool> )
				{
					if ( format.isShowBoolAsString() )
						return valueToString( buf, value ? "True" : "False", format );
					return integerToString( buf, value ? 1 : 0, format );
				}
				else if constexpr ( std::is_same_v<DecayT, utf8> )
				{
					if ( format.getBase() != Format::Base::Decimal || format.hasWidth() || format.isShowSign() )
						return integerToString( buf, static_cast<int32>( value ), format );
					buf[0] = value;
					buf[1] = '\0';
					return 1;
				}
				else
				{
					return integerToString( buf, value, format );
				}
			}
			else if constexpr ( std::is_floating_point_v<DecayT> )
			{
				return static_cast<uint32>( floatToString( buf, static_cast<double>( value ), format ) );
			}
			else if constexpr ( std::is_pointer_v<DecayT> )
			{
				if constexpr ( std::is_same_v<std::decay_t<std::remove_pointer_t<DecayT>>, char> ||
							   std::is_same_v<std::decay_t<std::remove_pointer_t<DecayT>>, wchar_t> )
				{
					const std::string utf8Str = toUTF8String( value );
					const uint32	  len	  = std::min( static_cast<uint32>( utf8Str.size() ), static_cast<uint32>( kTempBufferSize - 1 ) );
					std::memcpy( buf, utf8Str.data(), len );
					buf[len] = '\0';
					return len;
				}
				else
				{
					Format hexFormat = format;
					hexFormat.hexUpper();
					return integerToString( buf, reinterpret_cast<uintptr_t>( value ), hexFormat );
				}
			}
			else if constexpr ( std::is_null_pointer_v<DecayT> )
			{
				std::memcpy( buf, "(null)", 6 );
				buf[6] = '\0';
				return 6;
			}
			else
			{
				const std::string utf8Str = toUTF8String( value );
				const uint32	  len	  = std::min( static_cast<uint32>( utf8Str.size() ), static_cast<uint32>( kTempBufferSize - 1 ) );
				std::memcpy( buf, utf8Str.data(), len );
				buf[len] = '\0';
				return len;
			}
		}

		template <typename StringType>
		static std::string toUTF8String( const StringType& str )
		{
			using T = std::decay_t<StringType>;
			if constexpr ( std::is_pointer_v<T> )
			{
				if ( str == nullptr )
					return "(null)";
			}

			if constexpr ( std::is_constructible_v<std::string_view, T> )
				return std::string{ std::string_view{ str } };
			else if constexpr ( std::is_constructible_v<std::wstring_view, T> )
				return StringUtil::utf16ToUtf8( std::wstring{ std::wstring_view{ str } } );
			else
			{
				SW_ASSERT( false );
				return "[unsupported type]";
			}
		}

		template <typename IntType>
		static uint32 integerToString( utf8* buf, IntType value, const Format& format ) noexcept
		{
			utf8*  ptr			 = buf;
			uint64 absoluteValue = 0;

			if constexpr ( std::is_signed_v<IntType> )
			{
				if ( value < 0 )
				{
					*ptr++		  = '-';
					absoluteValue = static_cast<uint64>( 0 - static_cast<uint64>( value ) );
				}
				else
				{
					if ( format.isShowSign() )
						*ptr++ = '+';
					absoluteValue = static_cast<uint64>( value );
				}
			}
			else
			{
				if ( format.isShowSign() )
					*ptr++ = '+';
				absoluteValue = static_cast<uint64>( value );
			}

			const uint32 len = tryFastIntConversion( ptr, absoluteValue, format.getBase() );
			if ( len != invalid_index::kUint32 )
				return static_cast<uint32>( ptr - buf ) + len;

			return static_cast<uint32>( ptr - buf ) + fallbackIntegerToString( ptr, absoluteValue, format.getBase() );
		}

		static uint32 fallbackIntegerToString( utf8* buf, uint64 value, Format::Base base ) noexcept
		{
			if ( value == 0 )
			{
				buf[0] = '0';
				buf[1] = '\0';
				return 1;
			}

			const auto&	 digits	   = ( base == Format::Base::HexUpper ) ? kDigitUpper : kDigitLower;
			const uint64 baseValue = ( base == Format::Base::HexUpper ) ? 16ULL : static_cast<uint64>( base );

			utf8* start = buf;
			while ( value > 0 )
			{
				*buf++ = digits[value % baseValue];
				value /= baseValue;
			}

			*buf = '\0';
			std::reverse( start, buf );
			return static_cast<uint32>( buf - start );
		}

		template <typename IntType>
		static uint32 tryFastIntConversion( utf8* buf, IntType value, Format::Base base )
		{
			if ( base == Format::Base::Binary )
				return invalid_index::kUint32;

			int32 baseValue = ( base == Format::Base::HexUpper ) ? 16 : static_cast<int32>( base );
			auto [ptr, ec]	= std::to_chars( buf, buf + kIntegerBufferSize, value, baseValue );

			if ( ec == std::errc{} )
			{
				if ( base == Format::Base::HexUpper )
				{
					for ( utf8* p = buf; p != ptr; ++p )
					{
						if ( 'a' <= *p && *p <= 'f' )
							*p = *p - 'a' + 'A';
					}
				}
				*ptr = '\0';
				return static_cast<uint32>( ptr - buf );
			}
			return invalid_index::kUint32;
		}

		static size_t floatToString( utf8* buf, double value, const Format& format ) noexcept
		{
			if ( std::isnan( value ) )
			{
				std::memcpy( buf, "nan", 4 );
				return 3;
			}
			if ( std::isinf( value ) )
			{
				if ( value > 0 )
				{
					std::memcpy( buf, "inf", 4 );
					return 3;
				}
				std::memcpy( buf, "-inf", 5 );
				return 4;
			}

			utf8* p = buf;
			if ( value < 0 )
			{
				*p++  = '-';
				value = -value;
			}
			else if ( format.isShowSign() )
			{
				*p++ = '+';
			}

			const int32 precision = format.hasPrecision() ? format.getPrecision() : 6;
			auto [ptr, ec]		  = std::to_chars( p, buf + kFloatBufferSize, value, std::chars_format::fixed, precision );

			if ( static_cast<int>( ec ) == 0 )
			{
				*ptr = '\0';
				return static_cast<size_t>( ptr - buf );
			}
			return static_cast<size_t>( p - buf ) + fallbackFloatToString( p, value, precision );
		}

		static size_t fallbackFloatToString( utf8* buf, const double value, const int32 precision ) noexcept
		{
			utf8*  p		= buf;
			uint64 int_part = static_cast<uint64>( value );

			if ( int_part == 0 )
				*p++ = '0';
			else
			{
				utf8*  int_start = p;
				uint64 temp		 = int_part;
				while ( temp > 0 )
				{
					*p++ = '0' + ( temp % 10 );
					temp /= 10;
				}
				std::reverse( int_start, p );
			}

			if ( precision > 0 )
			{
				*p++		= '.';
				double frac = value - static_cast<double>( int_part );
				for ( int32 index = 0; index < precision; ++index )
				{
					frac *= 10.0;
					const int32 digit = static_cast<int32>( frac ) % 10;
					*p++			  = static_cast<utf8>( '0' + digit );
					frac -= digit;
				}
			}
			return static_cast<size_t>( p - buf );
		}

		static uint32 addPadding( utf8* SW_RESTRICT buffer, uint32 pos, const uint32 capacity, std::string_view str, const Format& fmt ) noexcept
		{
			const int32 padding = fmt.hasWidth() ? ( fmt.getWidth() - static_cast<int32>( str.size() ) ) : 0;
			if ( padding <= 0 )
				return write( buffer, pos, capacity, str );

			const utf8 padChar = fmt.isZeroPad() ? '0' : ' ';

			if ( fmt.isLeftAlign() == false )
			{
				for ( int32 paddingIndex = 0; paddingIndex < padding && pos < capacity - 1; ++paddingIndex )
					buffer[pos++] = padChar;
				pos = write( buffer, pos, capacity, str );
			}
			else
			{
				pos = write( buffer, pos, capacity, str );
				for ( int32 paddingIndex = 0; paddingIndex < padding && pos < capacity - 1; ++paddingIndex )
					buffer[pos++] = ' ';
			}

			return pos;
		}
	};

	template <typename... Args>
	inline void formatstring( utf8* buffer, uint32 capacity, std::string_view format, Args&&... args ) noexcept
	{
		FormatString::formatstring( buffer, capacity, format, std::forward<Args>( args )... );
	}
}

namespace std
{
	template <typename T>
	struct tuple_size<sw::FormattedValue<T>> : std::integral_constant<uint32, 2>
	{
	};

	template <uint32 TIndex, typename TType>
	struct tuple_element<TIndex, sw::FormattedValue<TType>>
	{

		using type = std::conditional_t<TIndex == 0, TType, sw::Format>;
	};
}
