/**
 * @file Archive.h
 * @brief Binary archive reader/writer with optional reflection serialization
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{

	struct float2;
	struct float3;
	struct float4;
	struct float4x4;
	struct TypeInfo;

	/// @brief 바이너리 아카이브 읽기/쓰기 (선택 리플렉션 직렬화)
	class SW_API Archive
	{
	public:
		/// @brief 매직/버전 헤더
		struct Header
		{
			uint64 _version{ 0 };

			union Properties
			{
				struct
				{
					uint64 _reserved : 64;
				} _bits;

				uint64 _rawData{ 0 };
			} _properties;
		};

		static_assert( sizeof( Header ) == sizeof( uint64 ) * 2 );

		/** @brief 빈 쓰기 버퍼로 시작합니다. */
		Archive();
		/** @brief 버퍼를 복사합니다. */
		Archive( const Archive& ) = default;
		/** @brief 버퍼 소유권을 넘깁니다. */
		Archive( Archive&& ) = default;
		/** @brief 파일에서 읽거나 쓸 아카이브를 엽니다. */
		explicit Archive( string_view fileName, bool bReadMode = true );
		/** @brief 메모리 버퍼를 읽기 전용으로 감쌉니다. */
		Archive( const uint8* pData, uint64 size );
		/** @brief 쓰기 모드면 버퍼를 정리합니다. */
		~Archive();

		/** @brief 버퍼를 복사 대입합니다. */
		Archive& operator=( const Archive& ) = default;
		/** @brief 이동 대입입니다. */
		Archive& operator=( Archive&& ) = default;

		/** @brief 아카이브의 버퍼 데이터를 대상 벡터에 복사합니다. */
		void writeData( vector<uint8>& outListDestination ) const;

		/** @brief 데이터 포인터를 반환합니다. */
		const uint8* getData() const { return _pData; }
		/** @brief 전체 데이터 크기를 반환합니다. */
		uint64 getSize() const { return _dataSize; }
		/** @brief 현재 읽기/쓰기 오프셋을 반환합니다. */
		uint64 getOffset() const { return _offset; }
		/** @brief 읽기 모드인지 반환합니다. */
		bool isReadMode() const { return _bReadMode; }

		/** @brief 오프셋을 직접 설정합니다. */
		void setOffset( uint64 offset );
		/** @brief 읽기/쓰기 모드를 변경하고 오프셋을 초기화합니다. */
		void setReadModeAndResetPos( bool bReadMode );

		/** @brief 현재 버퍼 내용을 파일로 저장합니다. */
		bool saveFile( string_view fileName ) const;

		/** @brief 소스 파일의 디렉터리 경로를 반환합니다. */
		const string& getSourceDirectory() const { return _sourceDirectory; }
		/** @brief 소스 파일의 이름을 반환합니다. */
		const string& getSourceFileName() const { return _sourceFileName; }

		/** @brief 지정된 크기만큼 바이트를 씁니다. */
		void writeBytes( const void* pBuffer, uint64 byteSize );
		/** @brief 지정된 크기만큼 바이트를 읽어옵니다. */
		bool readBytes( void* pOutBuffer, uint64 byteSize );

		/** @brief 현재 버퍼 데이터의 CRC32 체크섬을 계산합니다. */
		uint32 calculateChecksum() const;
		/** @brief 계산된 체크섬을 아카이브에 기록합니다. */
		void writeChecksum();
		/** @brief 읽어들인 체크섬이 올바른지 검증합니다. */
		bool validateChecksum() const;

		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( bool data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( uint8 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( uint16 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( uint32 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( uint64 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( int8 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( int16 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( int32 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( int64 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( float32 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( float64 data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( string_view data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( const float2& data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( const float3& data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( const float4& data );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator<<( const float4x4& data );

		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( bool& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( uint8& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( uint16& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( uint32& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( uint64& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( int8& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( int16& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( int32& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( int64& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( float32& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( float64& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( string& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( float2& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( float3& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( float4& outData );
		/** @brief 스트림/시프트 연산자입니다. */
		Archive& operator>>( float4x4& outData );

		/** @brief 타입 정보를 이용해 객체를 직렬화합니다. */
		bool serializeObject( void* pInstance, const TypeInfo* pTypeInfo );
		/** @brief 타입 정보를 이용해 객체를 직렬화합니다. */
		bool serializeObject( void* pInstance, const TypeInfo& typeInfo );
		/** @brief 타입 정보를 이용해 객체를 역직렬화합니다. */
		bool deserializeObject( void* pInstance, const TypeInfo* pTypeInfo );
		/** @brief 타입 정보를 이용해 객체를 역직렬화합니다. */
		bool deserializeObject( void* pInstance, const TypeInfo& typeInfo );

	private:
		vector<uint8> _listBuffer;
		string		  _sourceDirectory;
		string		  _sourceFileName;
		const uint8*  _pData;
		uint64		  _dataSize;
		uint64		  _offset;
		bool		  _bReadMode;
	};
} // namespace sw
