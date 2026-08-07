#pragma once
/**
 * @file Archive.h
 * @brief Binary archive reader/writer with optional reflection serialization
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonMacros.h"

namespace sw
{

	struct float2;
	struct float3;
	struct float4;
	struct float4x4;
	struct TypeInfo;

	class SW_API Archive
	{
	public:
		struct Header
		{
			uint64 _version = 0;
			union Properties
			{
				struct
				{
					uint64 _reserved : 64;
				} _bits;
				uint64 _rawData = 0;
			} _properties;
		};
		static_assert( sizeof( Header ) == sizeof( uint64 ) * 2 );

	public:
		Archive();
		Archive( const Archive& ) = default;
		Archive( Archive&& )	  = default;
		explicit Archive( const std::string_view fileName, bool bReadMode = true );
		Archive( const uint8* data, uint64 size );
		~Archive();

		Archive& operator=( const Archive& ) = default;
		Archive& operator=( Archive&& )		 = default;

	public:
		void writeData( std::vector<uint8>& outDestination ) const;

		const uint8* getData() const { return _pData; }
		uint64		 getSize() const { return _dataSize; }
		uint64		 getOffset() const { return _offset; }
		bool		 isReadMode() const { return _bReadMode; }

		void setOffset( uint64 offset );
		void setReadModeAndResetPos( bool bReadMode );

		bool saveFile( const std::string_view fileName ) const;

		const std::string& getSourceDirectory() const { return _sourceDirectory; }
		const std::string& getSourceFileName() const { return _sourceFileName; }

		void writeBytes( const void* pBuffer, uint64 byteSize );
		bool readBytes( void* pOutBuffer, uint64 byteSize );

		uint32 calculateCRC32() const;
		uint32 calculateChecksum() const;
		void   writeChecksum();
		bool   validateChecksum() const;

	public:
		Archive& operator<<( bool data );
		Archive& operator<<( uint8 data );
		Archive& operator<<( uint16 data );
		Archive& operator<<( uint32 data );
		Archive& operator<<( uint64 data );
		Archive& operator<<( int8 data );
		Archive& operator<<( int16 data );
		Archive& operator<<( int32 data );
		Archive& operator<<( int64 data );
		Archive& operator<<( float32 data );
		Archive& operator<<( float64 data );
		Archive& operator<<( const std::string& data );
		Archive& operator<<( const float2& data );
		Archive& operator<<( const float3& data );
		Archive& operator<<( const float4& data );
		Archive& operator<<( const float4x4& data );

		Archive& operator>>( bool& outData );
		Archive& operator>>( uint8& outData );
		Archive& operator>>( uint16& outData );
		Archive& operator>>( uint32& outData );
		Archive& operator>>( uint64& outData );
		Archive& operator>>( int8& outData );
		Archive& operator>>( int16& outData );
		Archive& operator>>( int32& outData );
		Archive& operator>>( int64& outData );
		Archive& operator>>( float32& outData );
		Archive& operator>>( float64& outData );
		Archive& operator>>( std::string& outData );
		Archive& operator>>( float2& outData );
		Archive& operator>>( float3& outData );
		Archive& operator>>( float4& outData );
		Archive& operator>>( float4x4& outData );

	public:
		bool serializeObject( void* pInstance, const TypeInfo* pTypeInfo );
		bool serializeObject( void* pInstance, const TypeInfo& typeInfo );
		bool deserializeObject( void* pInstance, const TypeInfo* pTypeInfo );
		bool deserializeObject( void* pInstance, const TypeInfo& typeInfo );

	private:
		std::vector<uint8> _buffer;
		const uint8*	   _pData	  = nullptr;
		uint64			   _dataSize  = 0;
		uint64			   _offset	  = 0;
		bool			   _bReadMode = true;

		std::string _sourceDirectory;
		std::string _sourceFileName;
	};
}
