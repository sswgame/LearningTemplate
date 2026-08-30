#include "pch.h"

#include "Core/Uuid/Uuid.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
	namespace
	{
		struct UuidInternal
		{
			static int32 hexNibble( utf8 c )
			{
				if ( c >= '0' && c <= '9' )
					return c - '0';
				if ( c >= 'a' && c <= 'f' )
					return 10 + ( c - 'a' );
				if ( c >= 'A' && c <= 'F' )
					return 10 + ( c - 'A' );
				return -1;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	Uuid Uuid::generate()
	{
		Uuid uuid{};
		for ( uint8& byteVal : uuid._arrBytes )
		{
			byteVal = static_cast<uint8>( MathUtil::getRandomRange<uint32>( 0, 255 ) );
		}

		// 버전 4
		uuid._arrBytes[6] = static_cast<uint8>( ( uuid._arrBytes[6] & 0x0f ) | 0x40 );
		// RFC 4122 변형 비트
		uuid._arrBytes[8] = static_cast<uint8>( ( uuid._arrBytes[8] & 0x3f ) | 0x80 );
		return uuid;
	}

	bool Uuid::tryParse( string_view text, Uuid& outUuid )
	{
		if ( text.size() != 36 )
			return false;
		if ( text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-' )
			return false;

		Uuid   uuid{};
		uint32 byteIndex{ 0 };
		for ( size_t charIndex = 0; charIndex < text.size(); ++charIndex )
		{
			if ( text[charIndex] == '-' )
				continue;
			if ( charIndex + 1 >= text.size() )
				return false;
			const int32 hi = UuidInternal::hexNibble( text[charIndex] );
			const int32 lo = UuidInternal::hexNibble( text[charIndex + 1] );
			if ( hi < 0 || lo < 0 || byteIndex >= 16 )
				return false;
			uuid._arrBytes[byteIndex++] = static_cast<uint8>( ( hi << 4 ) | lo );
			++charIndex;
		}
		if ( byteIndex != 16 )
			return false;
		outUuid = uuid;
		return true;
	}

	string Uuid::toString() const
	{
		static constexpr utf8 kArrHex[] = "0123456789abcdef";
		string				  out;
		out.resize( 36 );
		uint32 outIndex{ 0 };
		for ( uint32 byteIndex = 0; byteIndex < 16; ++byteIndex )
		{
			if ( byteIndex == 4 || byteIndex == 6 || byteIndex == 8 || byteIndex == 10 )
				out[outIndex++] = '-';
			out[outIndex++] = kArrHex[( _arrBytes[byteIndex] >> 4 ) & 0x0f];
			out[outIndex++] = kArrHex[_arrBytes[byteIndex] & 0x0f];
		}
		return out;
	}

	bool Uuid::isNull() const
	{
		for ( uint8 byteVal : _arrBytes )
		{
			if ( byteVal != 0 )
				return false;
		}
		return true;
	}

	bool Uuid::operator==( const Uuid& other ) const { return Memory::compare( _arrBytes, other._arrBytes, sizeof( _arrBytes ) ) == 0; }

	bool Uuid::operator<( const Uuid& other ) const
	{
		return Memory::compare( _arrBytes, other._arrBytes, sizeof( _arrBytes ) ) < 0;
	}
} // namespace sw
