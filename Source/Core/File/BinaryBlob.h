/**
 * @file BinaryBlob.h
 * @brief 리틀엔디언 u32/i32/문자열 blob 읽기·쓰기 (씬/프리팹/세이브 공용)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/** @brief 길이 접두 바이너리 blob 읽기·쓰기 */
	struct BinaryBlob
	{
		/** @brief 리틀엔디언 uint32를 읽고 offset을 진행합니다. */
		static bool readU32( const vector<uint8>& listBlob, size_t& offset, uint32& outValue )
		{
			if ( offset + 4 > listBlob.size() )
				return false;
			outValue = static_cast<uint32>( listBlob[offset] ) | ( static_cast<uint32>( listBlob[offset + 1] ) << 8 ) |
					   ( static_cast<uint32>( listBlob[offset + 2] ) << 16 ) | ( static_cast<uint32>( listBlob[offset + 3] ) << 24 );
			offset += 4;
			return true;
		}

		/** @brief 리틀엔디언 int32를 읽고 offset을 진행합니다. */
		static bool readI32( const vector<uint8>& listBlob, size_t& offset, int32& outValue )
		{
			uint32 val{ 0 };
			if ( readU32( listBlob, offset, val ) == false )
				return false;
			outValue = static_cast<int32>( val );
			return true;
		}

		/** @brief 길이 접두 문자열을 읽고 offset을 진행합니다. */
		static bool readString( const vector<uint8>& listBlob, size_t& offset, string& outString )
		{
			uint32 len{ 0 };
			if ( readU32( listBlob, offset, len ) == false || offset + len > listBlob.size() )
				return false;
			outString.assign( reinterpret_cast<const utf8*>( listBlob.data() + offset ), len );
			offset += len;
			return true;
		}

		/** @brief 리틀엔디언 uint32를 붙입니다. */
		static void appendU32( vector<uint8>& listBlob, uint32 value )
		{
			listBlob.push_back( static_cast<uint8>( value & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 16 ) & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 24 ) & 0xFFu ) );
		}

		/** @brief 리틀엔디언 int32를 붙입니다. */
		static void appendI32( vector<uint8>& listBlob, int32 value )
		{
			appendU32( listBlob, static_cast<uint32>( value ) );
		}

		/** @brief 길이 접두 문자열을 붙입니다. */
		static void appendString( vector<uint8>& listBlob, string_view text )
		{
			appendU32( listBlob, static_cast<uint32>( text.size() ) );
			if ( text.empty() == false )
			{
				const uint8* pBytes = reinterpret_cast<const uint8*>( text.data() );
				listBlob.insert( listBlob.end(), pBytes, pBytes + text.size() );
			}
		}
	};
} // namespace sw
