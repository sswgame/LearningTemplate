#include "pch.h"

#include "Engine/Serialization/Core/StringPool.h"

#include "Core/Common/VarIntUtil.h"

#include "Engine/Serialization/Core/BinaryStream.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
	StringPool::StringPool()
		: _listString{}
		, _mapStringToId{}
	{
		initPredefined();
	}

	void StringPool::initPredefined()
	{
		_listString.clear();
		_mapStringToId.clear();
		_listString.reserve( kPredefinedCount + 32 );
		_mapStringToId.reserve( kPredefinedCount + 32 );

#define REGISTER_NAME( index, name )                               \
	{                                                              \
		const string strKey		   = #name;                        \
		const uint32 expectedIndex = static_cast<uint32>( index ); \
		_listString.push_back( strKey );                           \
		_mapStringToId.emplace( strKey, expectedIndex );           \
	}
#include "Core/Predefined/PredefinedNameType.xxx"
#undef REGISTER_NAME
	}

	uint32 StringPool::internString( string_view str )
	{
		const hashed_string hashed( str );
		const uint32		keyIndex = hashed.getIndex();
		if ( keyIndex < kPredefinedCount )
			return keyIndex;

		const string key( str );
		const auto	 it = _mapStringToId.find( key );
		if ( it != _mapStringToId.end() )
			return it->second;

		const uint32 newId = static_cast<uint32>( _listString.size() );
		_listString.push_back( key );
		_mapStringToId.emplace( _listString.back(), newId );
		return newId;
	}

	string_view StringPool::getString( uint32 index ) const
	{
		if ( index >= _listString.size() )
			return {};
		return _listString[index];
	}

	void StringPool::clear()
	{
		initPredefined();
	}

	void StringPool::saveToArchive( Archive& outArchive ) const
	{
		const size_t dynCount = getDynamicCount();
		outArchive.writeVarUInt( static_cast<uint64>( dynCount ) );
		for ( size_t index = kPredefinedCount; index < _listString.size(); ++index )
		{
			outArchive.writeString( _listString[index] );
		}
	}

	bool StringPool::loadFromArchive( Archive& inArchive )
	{
		initPredefined();
		uint64 dynCount = 0;
		if ( inArchive.readVarUInt( dynCount ) == false || dynCount > kMaxDynamicStrings )
			return false;

		_listString.reserve( kPredefinedCount + static_cast<size_t>( dynCount ) );
		_mapStringToId.reserve( kPredefinedCount + static_cast<size_t>( dynCount ) );

		for ( uint64 strIndex = 0; strIndex < dynCount; ++strIndex )
		{
			string str;
			if ( inArchive.readString( str ) == false )
				return false;
			const uint32 id = static_cast<uint32>( _listString.size() );
			_listString.push_back( std::move( str ) );
			_mapStringToId.emplace( _listString.back(), id );
		}
		return true;
	}

	void StringPool::saveToBinaryBuffer( vector<uint8>& outBytes ) const
	{
		BinaryStreamWriter writer( outBytes );
		const size_t	   dynCount = getDynamicCount();
		writer.writeVarUInt( static_cast<uint64>( dynCount ) );
		for ( size_t index = kPredefinedCount; index < _listString.size(); ++index )
		{
			writer.writeString( _listString[index] );
		}
	}

	bool StringPool::loadFromBinaryBuffer( const uint8* pData, size_t dataSize, size_t& inoutOffset )
	{
		initPredefined();
		uint64 dynCount = 0;
		if ( VarIntUtil::decodeVarUInt64( pData, dataSize, inoutOffset, dynCount ) == false || dynCount > kMaxDynamicStrings )
			return false;

		_listString.reserve( kPredefinedCount + static_cast<size_t>( dynCount ) );
		_mapStringToId.reserve( kPredefinedCount + static_cast<size_t>( dynCount ) );

		for ( uint64 strIndex = 0; strIndex < dynCount; ++strIndex )
		{
			if ( inoutOffset + sizeof( uint32 ) > dataSize )
				return false;
			uint32 len = 0;
			Memory::copy( &len, pData + inoutOffset, sizeof( uint32 ) );
			inoutOffset += sizeof( uint32 );

			if ( inoutOffset + len > dataSize )
				return false;
			string str( reinterpret_cast<const utf8*>( pData + inoutOffset ), len );
			inoutOffset += len;

			const uint32 id = static_cast<uint32>( _listString.size() );
			_listString.push_back( str );
			_mapStringToId.emplace( std::move( str ), id );
		}
		return true;
	}
} // namespace sw
