#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/BinarySerializer.h"

namespace sw
{
	Archive::Archive()
		: _listBuffer{}
		, _sourceDirectory{}
		, _sourceFileName{}
		, _pData{ nullptr }
		, _dataSize{ 0 }
		, _offset{ 0 }
		, _bReadMode{ false }
	{
	}

	Archive::Archive( string_view fileName, bool bReadMode )
		: _listBuffer{}
		, _sourceDirectory{}
		, _sourceFileName{}
		, _pData{ nullptr }
		, _dataSize{ 0 }
		, _offset{ 0 }
		, _bReadMode{ bReadMode }
	{
		if ( bReadMode )
		{
			FileUtil::readFile( fileName, _listBuffer );
			_pData	  = _listBuffer.data();
			_dataSize = _listBuffer.size();
		}
	}

	Archive::Archive( const uint8* pData, uint64 size )
		: _listBuffer{}
		, _sourceDirectory{}
		, _sourceFileName{}
		, _pData{ pData }
		, _dataSize{ size }
		, _offset{ 0 }
		, _bReadMode{ true }
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
		_bReadMode = bSetReadMode;
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
			return false;

		if ( _offset + byteSize > _dataSize )
			return false;

		Memory::copy( pOutBuffer, _pData + _offset, byteSize );
		_offset += byteSize;
		return true;
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
		return true;
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
		if ( len > 0 )
		{
			outData.resize( len );
			readBytes( outData.data(), len );
		}
		else
			outData.clear();
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
			return false;

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
			return false;

		if ( _offset >= _dataSize )
			return false;

		return BinarySerializer::deserialize( pInstance, *pTypeInfo, _pData + _offset, _dataSize - _offset );
	}

	bool Archive::deserializeObject( void* pInstance, const TypeInfo& typeInfo )
	{
		return deserializeObject( pInstance, &typeInfo );
	}
} // namespace sw
