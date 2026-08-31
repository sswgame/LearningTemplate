#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "Core/Common/VarIntUtil.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
	Archive::Archive()
		: _stringPool{}
		, _listBuffer{}
		, _sourceDirectory{}
		, _sourceFileName{}
		, _pData{ nullptr }
		, _dataSize{ 0 }
		, _offset{ 0 }
		, _bReadMode{ SW_FALSE }
		, _bError{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	Archive::Archive( string_view fileName, bool bReadMode )
		: _stringPool{}
		, _listBuffer{}
		, _sourceDirectory{}
		, _sourceFileName{}
		, _pData{ nullptr }
		, _dataSize{ 0 }
		, _offset{ 0 }
		, _bReadMode{ bReadMode ? static_cast<uint8>( SW_TRUE ) : static_cast<uint8>( SW_FALSE ) }
		, _bError{ SW_FALSE }
		, _reserved{ 0 }
	{
		if ( bReadMode )
		{
			if ( FileUtil::readFile( fileName, _listBuffer ) )
			{
				_pData	  = _listBuffer.data();
				_dataSize = _listBuffer.size();
			}
			else
			{
				_bError = SW_TRUE;
			}
		}
	}

	Archive::Archive( const uint8* pData, uint64 size )
		: _stringPool{}
		, _listBuffer{}
		, _sourceDirectory{}
		, _sourceFileName{}
		, _pData{ pData }
		, _dataSize{ size }
		, _offset{ 0 }
		, _bReadMode{ SW_TRUE }
		, _bError{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	Archive::~Archive() = default;

	void Archive::writeData( vector<uint8>& outListDestination ) const
	{
		outListDestination.assign( _listBuffer.begin(), _listBuffer.end() );
	}

	void Archive::setOffset( uint64 offset )
	{
		_offset = offset;
	}

	void Archive::setReadModeAndResetPos( const bool bSetReadMode )
	{
		_bReadMode = bSetReadMode ? SW_TRUE : SW_FALSE;
		_bError	   = SW_FALSE;
		_offset	   = 0;
	}

	bool Archive::saveFile( string_view fileName ) const
	{
		if ( _listBuffer.empty() == false )
			return FileUtil::writeFile( fileName, _listBuffer.data(), _listBuffer.size() );
		return false;
	}

	void Archive::writeBytes( const void* pBuffer, uint64 byteSize )
	{
		if ( pBuffer == nullptr || byteSize == 0 )
			return;

		const uint8* pBytes = static_cast<const uint8*>( pBuffer );
		_listBuffer.insert( _listBuffer.end(), pBytes, pBytes + byteSize );
		_pData	  = _listBuffer.data();
		_dataSize = _listBuffer.size();
	}

	bool Archive::readBytes( void* pOutBuffer, uint64 byteSize )
	{
		if ( pOutBuffer == nullptr || byteSize == 0 || _pData == nullptr )
		{
			if ( byteSize > 0 )
				_bError = SW_TRUE;
			return false;
		}

		if ( _offset + byteSize > _dataSize )
		{
			_bError = SW_TRUE;
			return false;
		}

		Memory::copy( pOutBuffer, _pData + _offset, byteSize );
		_offset += byteSize;
		return true;
	}

	const uint8* Archive::readBytesView( uint64 byteSize )
	{
		if ( _pData == nullptr || _offset + byteSize > _dataSize )
		{
			_bError = SW_TRUE;
			return nullptr;
		}

		const uint8* pResult = _pData + _offset;
		_offset += byteSize;
		return pResult;
	}

	void Archive::writeString( string_view str )
	{
		*this << str;
	}

	bool Archive::readString( string& outStr )
	{
		*this >> outStr;
		return isOk();
	}

	bool Archive::readStringView( string_view& outView )
	{
		uint32 len{ 0 };
		( *this ) >> len;
		if ( _bError == SW_TRUE )
		{
			outView = {};
			return false;
		}

		if ( len > 0 )
		{
			if ( _pData == nullptr || _offset + len > _dataSize )
			{
				_bError = SW_TRUE;
				outView = {};
				return false;
			}
			outView = string_view{ reinterpret_cast<const utf8*>( _pData + _offset ), len };
			_offset += len;
			return true;
		}
		outView = {};
		return true;
	}

	Archive Archive::readSubArchive( uint64 byteSize )
	{
		if ( _pData == nullptr || _offset + byteSize > _dataSize )
		{
			_bError = SW_TRUE;
			Archive errArch( nullptr, 0 );
			errArch.setError();
			return errArch;
		}

		const uint8* pSubData = _pData + _offset;
		_offset += byteSize;
		return Archive( pSubData, byteSize );
	}

	void Archive::writeSection( const void* pData, uint32 byteSize )
	{
		( *this ) << byteSize;
		if ( pData != nullptr && byteSize > 0 )
			writeBytes( pData, byteSize );
	}

	bool Archive::readSection( vector<uint8>& outBytes )
	{
		uint32 size = 0;
		( *this ) >> size;
		if ( _bError == SW_TRUE )
			return false;

		if ( size == 0 )
		{
			outBytes.clear();
			return true;
		}

		if ( _offset + size > _dataSize )
		{
			_bError = SW_TRUE;
			outBytes.clear();
			return false;
		}

		outBytes.resize( size );
		return readBytes( outBytes.data(), size );
	}

	uint32 Archive::calculateChecksum() const
	{
		if ( _pData == nullptr || _dataSize == 0 )
			return 0;

		return StringUtil::computeCrc32( _pData, _dataSize );
	}

	void Archive::writeChecksum()
	{
		uint32 checksum = calculateChecksum();
		( *this ) << checksum;
	}

	bool Archive::validateChecksum() const
	{
		if ( _dataSize < sizeof( uint32 ) || _pData == nullptr )
			return false;

		uint32 storedChecksum = 0;
		Memory::copy( &storedChecksum, _pData + _dataSize - sizeof( uint32 ), sizeof( uint32 ) );
		const uint32 computed = StringUtil::computeCrc32( _pData, _dataSize - sizeof( uint32 ) );
		return storedChecksum == computed;
	}

	Archive& Archive::operator<<( bool data )
	{
		writeBytes( &data, sizeof( bool ) );
		return *this;
	}

	Archive& Archive::operator<<( uint8 data )
	{
		writeBytes( &data, sizeof( uint8 ) );
		return *this;
	}

	Archive& Archive::operator<<( uint16 data )
	{
		writeBytes( &data, sizeof( uint16 ) );
		return *this;
	}

	Archive& Archive::operator<<( uint32 data )
	{
		writeBytes( &data, sizeof( uint32 ) );
		return *this;
	}

	Archive& Archive::operator<<( uint64 data )
	{
		writeBytes( &data, sizeof( uint64 ) );
		return *this;
	}

	Archive& Archive::operator<<( int8 data )
	{
		writeBytes( &data, sizeof( int8 ) );
		return *this;
	}

	Archive& Archive::operator<<( int16 data )
	{
		writeBytes( &data, sizeof( int16 ) );
		return *this;
	}

	Archive& Archive::operator<<( int32 data )
	{
		writeBytes( &data, sizeof( int32 ) );
		return *this;
	}

	Archive& Archive::operator<<( int64 data )
	{
		writeBytes( &data, sizeof( int64 ) );
		return *this;
	}

	Archive& Archive::operator<<( float32 data )
	{
		writeBytes( &data, sizeof( float32 ) );
		return *this;
	}

	Archive& Archive::operator<<( float64 data )
	{
		writeBytes( &data, sizeof( float64 ) );
		return *this;
	}

	Archive& Archive::operator<<( string_view data )
	{
		const uint32 len = static_cast<uint32>( data.length() );
		( *this ) << len;
		if ( len > 0 )
			writeBytes( data.data(), len );
		return *this;
	}

	Archive& Archive::operator<<( const vector<uint8>& bytes )
	{
		const uint32 len = static_cast<uint32>( bytes.size() );
		( *this ) << len;
		if ( len > 0 )
			writeBytes( bytes.data(), len );
		return *this;
	}

	Archive& Archive::operator<<( const float2& data )
	{
		writeBytes( &data, sizeof( float2 ) );
		return *this;
	}

	Archive& Archive::operator<<( const float3& data )
	{
		writeBytes( &data, sizeof( float3 ) );
		return *this;
	}

	Archive& Archive::operator<<( const float4& data )
	{
		writeBytes( &data, sizeof( float4 ) );
		return *this;
	}

	Archive& Archive::operator<<( const float4x4& data )
	{
		writeBytes( &data, sizeof( float4x4 ) );
		return *this;
	}

	Archive& Archive::operator>>( bool& outData )
	{
		readBytes( &outData, sizeof( bool ) );
		return *this;
	}

	Archive& Archive::operator>>( uint8& outData )
	{
		readBytes( &outData, sizeof( uint8 ) );
		return *this;
	}

	Archive& Archive::operator>>( uint16& outData )
	{
		readBytes( &outData, sizeof( uint16 ) );
		return *this;
	}

	Archive& Archive::operator>>( uint32& outData )
	{
		readBytes( &outData, sizeof( uint32 ) );
		return *this;
	}

	Archive& Archive::operator>>( uint64& outData )
	{
		readBytes( &outData, sizeof( uint64 ) );
		return *this;
	}

	Archive& Archive::operator>>( int8& outData )
	{
		readBytes( &outData, sizeof( int8 ) );
		return *this;
	}

	Archive& Archive::operator>>( int16& outData )
	{
		readBytes( &outData, sizeof( int16 ) );
		return *this;
	}

	Archive& Archive::operator>>( int32& outData )
	{
		readBytes( &outData, sizeof( int32 ) );
		return *this;
	}

	Archive& Archive::operator>>( int64& outData )
	{
		readBytes( &outData, sizeof( int64 ) );
		return *this;
	}

	Archive& Archive::operator>>( float32& outData )
	{
		readBytes( &outData, sizeof( float32 ) );
		return *this;
	}

	Archive& Archive::operator>>( float64& outData )
	{
		readBytes( &outData, sizeof( float64 ) );
		return *this;
	}

	Archive& Archive::operator>>( string& outData )
	{
		uint32 len{ 0 };
		( *this ) >> len;
		if ( _bError == SW_TRUE )
		{
			outData.clear();
			return *this;
		}

		if ( len > 0 )
		{
			if ( _pData == nullptr || _offset + len > _dataSize )
			{
				_bError = SW_TRUE;
				outData.clear();
				return *this;
			}
			outData.resize( len );
			readBytes( outData.data(), len );
		}
		else
		{
			outData.clear();
		}
		return *this;
	}

	Archive& Archive::operator>>( vector<uint8>& outBytes )
	{
		uint32 len{ 0 };
		( *this ) >> len;
		if ( _bError == SW_TRUE )
		{
			outBytes.clear();
			return *this;
		}

		if ( len > 0 )
		{
			if ( _pData == nullptr || _offset + len > _dataSize )
			{
				_bError = SW_TRUE;
				outBytes.clear();
				return *this;
			}
			outBytes.resize( len );
			readBytes( outBytes.data(), len );
		}
		else
		{
			outBytes.clear();
		}
		return *this;
	}

	Archive& Archive::operator>>( float2& outData )
	{
		readBytes( &outData, sizeof( float2 ) );
		return *this;
	}

	Archive& Archive::operator>>( float3& outData )
	{
		readBytes( &outData, sizeof( float3 ) );
		return *this;
	}

	Archive& Archive::operator>>( float4& outData )
	{
		readBytes( &outData, sizeof( float4 ) );
		return *this;
	}

	Archive& Archive::operator>>( float4x4& outData )
	{
		readBytes( &outData, sizeof( float4x4 ) );
		return *this;
	}

	bool Archive::serializeObject( void* pInstance, const TypeInfo* pTypeInfo )
	{
		if ( pInstance == nullptr || pTypeInfo == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		BinarySerializer::serialize( pInstance, *pTypeInfo, _listBuffer );
		_pData	  = _listBuffer.data();
		_dataSize = _listBuffer.size();
		return true;
	}

	bool Archive::serializeObject( void* pInstance, const TypeInfo& typeInfo )
	{
		return serializeObject( pInstance, &typeInfo );
	}

	bool Archive::deserializeObject( void* pInstance, const TypeInfo* pTypeInfo )
	{
		if ( pInstance == nullptr || pTypeInfo == nullptr || _pData == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		if ( _offset >= _dataSize )
		{
			_bError = SW_TRUE;
			return false;
		}

		return BinarySerializer::deserialize( pInstance, *pTypeInfo, _pData + _offset, _dataSize - _offset );
	}

	bool Archive::deserializeObject( void* pInstance, const TypeInfo& typeInfo )
	{
		return deserializeObject( pInstance, &typeInfo );
	}

	bool Archive::writeCompressedSection( const void* pData, uint32 byteSize, CompressionCodecType codecType )
	{
		if ( pData == nullptr || byteSize == 0 )
		{
			writeSection( nullptr, 0 );
			return true;
		}

		vector<uint8> compressedBytes;
		if ( CompressionStream::compressBuffer( pData, byteSize, compressedBytes, codecType ) == false )
		{
			_bError = SW_TRUE;
			return false;
		}

		writeSection( compressedBytes.data(), static_cast<uint32>( compressedBytes.size() ) );
		return true;
	}

	bool Archive::readCompressedSection( vector<uint8>& outBytes )
	{
		vector<uint8> compressedBytes;
		if ( readSection( compressedBytes ) == false )
			return false;

		if ( compressedBytes.empty() )
		{
			outBytes.clear();
			return true;
		}

		if ( CompressionStream::decompressBuffer( compressedBytes.data(), compressedBytes.size(), outBytes ) == false )
		{
			_bError = SW_TRUE;
			outBytes.clear();
			return false;
		}

		return true;
	}

	bool Archive::serializeCompressedObject( const void* pInstance, const TypeInfo& typeInfo, CompressionCodecType codecType )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		vector<uint8> rawBytes;
		BinarySerializer::serialize( pInstance, typeInfo, rawBytes );
		return writeCompressedSection( rawBytes.data(), static_cast<uint32>( rawBytes.size() ), codecType );
	}

	bool Archive::deserializeCompressedObject( void* pInstance, const TypeInfo& typeInfo )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		vector<uint8> rawBytes;
		if ( readCompressedSection( rawBytes ) == false || rawBytes.empty() )
			return false;

		return BinarySerializer::deserialize( pInstance, typeInfo, rawBytes.data(), rawBytes.size() );
	}

	bool Archive::serializeVersionedObject( uint32 version, const void* pInstance, const TypeInfo& typeInfo )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		vector<uint8> versionedBytes;
		BinarySerializer::serializeVersioned( version, pInstance, typeInfo, versionedBytes );
		writeSection( versionedBytes.data(), static_cast<uint32>( versionedBytes.size() ) );
		return true;
	}

	bool Archive::deserializeVersionedObject( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
											  uint32 currentVersion, SchemaMigrateFn migrate, const TypeInfo* pLegacyTypeInfo )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		vector<uint8> versionedBytes;
		if ( readSection( versionedBytes ) == false || versionedBytes.empty() )
			return false;

		return BinarySerializer::deserializeVersioned( outVersion, pInstance, typeInfo,
													   versionedBytes.data(), versionedBytes.size(),
													   currentVersion, migrate, pLegacyTypeInfo );
	}

	bool Archive::serializeJsonObject( const void* pInstance, const TypeInfo& typeInfo, bool bPretty )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		const string jsonStr = bPretty ? JsonSerializer::serializePretty( pInstance, typeInfo )
									   : JsonSerializer::serialize( pInstance, typeInfo );
		( *this ) << jsonStr;
		return true;
	}

	bool Archive::deserializeJsonObject( void* pInstance, const TypeInfo& typeInfo )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		string jsonStr;
		( *this ) >> jsonStr;
		if ( _bError == SW_TRUE || jsonStr.empty() )
			return false;

		return JsonSerializer::deserialize( pInstance, typeInfo, jsonStr );
	}

	bool Archive::convertJsonToBinary( string_view jsonStr, const TypeInfo& typeInfo )
	{
		if ( jsonStr.empty() || typeInfo._size == 0 )
		{
			_bError = SW_TRUE;
			return false;
		}

		vector<uint8> outBytes;
		if ( SerializerUtil::transcodeJsonToBinary( jsonStr, typeInfo, outBytes ) == false )
		{
			_bError = SW_TRUE;
			return false;
		}

		writeBytes( outBytes.data(), outBytes.size() );
		return true;
	}

	string Archive::convertBinaryToJson( const TypeInfo& typeInfo, bool bPretty )
	{
		if ( _pData == nullptr || _offset >= _dataSize || typeInfo._size == 0 )
			return {};

		return SerializerUtil::transcodeBinaryToJson( _pData + _offset, _dataSize - _offset, typeInfo, bPretty );
	}

	bool Archive::serializeXmlObject( const void* pInstance, const TypeInfo& typeInfo )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		const string xmlStr = XmlSerializer::serialize( pInstance, typeInfo );
		( *this ) << xmlStr;
		return true;
	}

	bool Archive::deserializeXmlObject( void* pInstance, const TypeInfo& typeInfo )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}

		string xmlStr;
		( *this ) >> xmlStr;
		if ( _bError == SW_TRUE || xmlStr.empty() )
			return false;

		return XmlSerializer::deserialize( pInstance, typeInfo, xmlStr );
	}

	bool Archive::convertXmlToBinary( string_view xmlStr, const TypeInfo& typeInfo )
	{
		if ( xmlStr.empty() || typeInfo._size == 0 )
		{
			_bError = SW_TRUE;
			return false;
		}

		vector<uint8> outBytes;
		if ( SerializerUtil::transcodeXmlToBinary( xmlStr, typeInfo, outBytes ) == false )
		{
			_bError = SW_TRUE;
			return false;
		}

		writeBytes( outBytes.data(), outBytes.size() );
		return true;
	}

	string Archive::convertBinaryToXml( const TypeInfo& typeInfo )
	{
		if ( _pData == nullptr || _offset >= _dataSize || typeInfo._size == 0 )
			return {};

		return SerializerUtil::transcodeBinaryToXml( _pData + _offset, _dataSize - _offset, typeInfo );
	}

	void Archive::writeVarUInt( uint64 value )
	{
		VarIntUtil::encodeVarUInt64( value, _listBuffer );
		_pData	  = _listBuffer.data();
		_dataSize = _listBuffer.size();
		_offset	  = _dataSize;
	}

	void Archive::writeVarInt( int64 value )
	{
		VarIntUtil::encodeVarInt64( value, _listBuffer );
		_pData	  = _listBuffer.data();
		_dataSize = _listBuffer.size();
		_offset	  = _dataSize;
	}

	bool Archive::readVarUInt( uint64& outValue )
	{
		if ( _pData == nullptr || _offset >= _dataSize )
		{
			_bError = SW_TRUE;
			return false;
		}
		size_t inoutOffset = static_cast<size_t>( _offset );
		if ( VarIntUtil::decodeVarUInt64( _pData, static_cast<size_t>( _dataSize ), inoutOffset, outValue ) == false )
		{
			_bError = SW_TRUE;
			return false;
		}
		_offset = inoutOffset;
		return true;
	}

	bool Archive::readVarUInt( uint32& outValue )
	{
		uint64 val64 = 0;
		if ( readVarUInt( val64 ) == false )
			return false;
		outValue = static_cast<uint32>( val64 );
		return true;
	}

	bool Archive::readVarInt( int64& outValue )
	{
		if ( _pData == nullptr || _offset >= _dataSize )
		{
			_bError = SW_TRUE;
			return false;
		}
		size_t inoutOffset = static_cast<size_t>( _offset );
		if ( VarIntUtil::decodeVarInt64( _pData, static_cast<size_t>( _dataSize ), inoutOffset, outValue ) == false )
		{
			_bError = SW_TRUE;
			return false;
		}
		_offset = inoutOffset;
		return true;
	}

	bool Archive::readVarInt( int32& outValue )
	{
		int64 val64 = 0;
		if ( readVarInt( val64 ) == false )
			return false;
		outValue = static_cast<int32>( val64 );
		return true;
	}

	void Archive::writePooledString( string_view str )
	{
		const uint32 poolId = _stringPool.internString( str );
		writeVarUInt( static_cast<uint64>( poolId ) );
	}

	bool Archive::readPooledString( string& outStr )
	{
		uint64 poolId = 0;
		if ( readVarUInt( poolId ) == false || poolId >= _stringPool.getCount() )
		{
			_bError = SW_TRUE;
			return false;
		}
		const string_view sv = _stringPool.getString( static_cast<uint32>( poolId ) );
		outStr.assign( sv.data(), sv.size() );
		return true;
	}

	void Archive::saveStringPool()
	{
		_stringPool.saveToArchive( *this );
	}

	bool Archive::loadStringPool()
	{
		const bool bOk = _stringPool.loadFromArchive( *this );
		if ( bOk == false )
		{
			_bError = SW_TRUE;
		}
		return bOk;
	}

	bool Archive::serializeCompactObject( const void* pInstance, const TypeInfo& typeInfo, const SerializeContext& ctx )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}
		vector<uint8> compactBytes;
		BinarySerializer::serializeCompact( pInstance, typeInfo, compactBytes, ctx );
		writeSection( compactBytes.data(), static_cast<uint32>( compactBytes.size() ) );
		return isOk();
	}

	bool Archive::deserializeCompactObject( void* pInstance, const TypeInfo& typeInfo, const SerializeContext& ctx )
	{
		if ( pInstance == nullptr )
		{
			_bError = SW_TRUE;
			return false;
		}
		vector<uint8> compactBytes;
		if ( readSection( compactBytes ) == false || compactBytes.empty() )
		{
			_bError = SW_TRUE;
			return false;
		}
		const bool bOk = BinarySerializer::deserializeCompact( pInstance, typeInfo, compactBytes.data(), compactBytes.size(), ctx );
		if ( bOk == false )
			_bError = SW_TRUE;
		return bOk;
	}
} // namespace sw
