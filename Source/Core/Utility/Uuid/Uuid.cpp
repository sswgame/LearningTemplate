/**
 * @file Uuid.cpp
 * @brief UUID v4 generation and formatting.
 */
#include "Uuid.h"

#include <random>

namespace sw
{
	namespace
	{
		int32 hexNibble( char c )
		{
			if ( c >= '0' && c <= '9' )
				return c - '0';
			if ( c >= 'a' && c <= 'f' )
				return 10 + ( c - 'a' );
			if ( c >= 'A' && c <= 'F' )
				return 10 + ( c - 'A' );
			return -1;
		}
	} // namespace

	Uuid Uuid::generate()
	{
		Uuid uuid{};
		thread_local std::mt19937_64 rng{ std::random_device{}() };
		std::uniform_int_distribution<uint32> dist( 0, 255 );

		for ( uint8& b : uuid._bytes )
			b = static_cast<uint8>( dist( rng ) );

		// Version 4
		uuid._bytes[6] = static_cast<uint8>( ( uuid._bytes[6] & 0x0f ) | 0x40 );
		// RFC 4122 variant
		uuid._bytes[8] = static_cast<uint8>( ( uuid._bytes[8] & 0x3f ) | 0x80 );
		return uuid;
	}

	bool Uuid::tryParse( std::string_view text, Uuid& outUuid )
	{
		if ( text.size() != 36 )
			return false;
		if ( text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-' )
			return false;

		Uuid uuid{};
		uint32 byteIndex = 0;
		for ( size_t i = 0; i < text.size(); ++i )
		{
			if ( text[i] == '-' )
				continue;
			if ( i + 1 >= text.size() )
				return false;
			const int32 hi = hexNibble( text[i] );
			const int32 lo = hexNibble( text[i + 1] );
			if ( hi < 0 || lo < 0 || byteIndex >= 16 )
				return false;
			uuid._bytes[byteIndex++] = static_cast<uint8>( ( hi << 4 ) | lo );
			++i;
		}
		if ( byteIndex != 16 )
			return false;
		outUuid = uuid;
		return true;
	}

	std::string Uuid::toString() const
	{
		static constexpr char kHex[] = "0123456789abcdef";
		std::string			  out;
		out.resize( 36 );
		uint32 o = 0;
		for ( uint32 i = 0; i < 16; ++i )
		{
			if ( i == 4 || i == 6 || i == 8 || i == 10 )
				out[o++] = '-';
			out[o++] = kHex[( _bytes[i] >> 4 ) & 0x0f];
			out[o++] = kHex[_bytes[i] & 0x0f];
		}
		return out;
	}

	bool Uuid::isNil() const
	{
		for ( uint8 b : _bytes )
		{
			if ( b != 0 )
				return false;
		}
		return true;
	}

	bool Uuid::operator==( const Uuid& other ) const
	{
		return std::memcmp( _bytes, other._bytes, sizeof( _bytes ) ) == 0;
	}
} // namespace sw
