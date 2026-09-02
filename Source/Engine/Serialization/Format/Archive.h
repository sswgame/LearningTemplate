/**
 * @file Archive.h
 * @brief Binary archive reader/writer with optional reflection serialization
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/StringPool.h"

namespace sw
{

    struct float2;
    struct float3;
    struct float4;
    struct float4x4;
    struct TypeInfo;
    class SerializeContext;

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
        /** @brief 메모리 버퍼로부터 데이터 복사 없이 포인터 뷰를 읽어옵니다. (읽기 모드 전용) */
        const uint8* readBytesView( uint64 byteSize );

        /** @brief 문자열을 길이 접두사와 함께 아카이브에 기록합니다. */
        void writeString( string_view str );
        /** @brief 아카이브로부터 길이 접두사가 포함된 문자열을 읽어옵니다. */
        bool readString( string& outStr );
        /** @brief 아카이브로부터 길이 접두사가 포함된 문자열 뷰를 복사 없이 읽어옵니다 (읽기 모드 전용). */
        bool readStringView( string_view& outView );

        /** @brief 현재 버퍼 데이터의 CRC32 체크섬을 계산합니다. */
        uint32 calculateChecksum() const;
        /** @brief 계산된 체크섬을 아카이브에 기록합니다. */
        void writeChecksum();
        /** @brief 읽어들인 체크섬이 올바른지 검증합니다. */
        bool validateChecksum() const;

        /** @brief 에러 발생 여부를 반환합니다. */
        bool isError() const { return _bError == SW_TRUE; }
        /** @brief 정상 상태인지 반환합니다. */
        bool isOk() const { return _bError == SW_FALSE; }
        /** @brief 에러 상태를 설정합니다. */
        void setError() { _bError = SW_TRUE; }
        /** @brief 에러 상태를 해제합니다. */
        void clearError() { _bError = SW_FALSE; }
        /** @brief 유효한 상태인지 불리언으로 변환합니다. */
        explicit operator bool() const { return isOk(); }

        /** @brief 남은 읽기 가능 바이트 수를 반환합니다. */
        uint64 getRemainingBytes() const { return ( _pData != nullptr && _offset < _dataSize ) ? ( _dataSize - _offset ) : 0; }
        /** @brief 지정 바이트만큼 읽을 수 있는지 확인합니다. */
        bool hasBytesAvailable( uint64 byteSize ) const { return ( _pData != nullptr && _offset + byteSize <= _dataSize ); }

        /** @brief 지정 바이트 크기만큼의 하위 읽기 전용 아카이브를 분할 생성합니다. */
        Archive readSubArchive( uint64 byteSize );

        /** @brief 크기 헤더와 함께 페이로드 바이트 블록을 기록합니다. */
        void writeSection( const void* pData, uint32 byteSize );
        /** @brief 크기 헤더를 읽고 해당 크기만큼 페이로드 바이트 블록을 읽어옵니다. */
        bool readSection( vector<uint8>& outBytes );

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
        /** @brief 바이트 벡터(길이 접두사 포함) 직렬화 연산자입니다. */
        Archive& operator<<( const vector<uint8>& bytes );
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
        /** @brief 바이트 벡터(길이 접두사 포함) 역직렬화 연산자입니다. */
        Archive& operator>>( vector<uint8>& outBytes );
        /** @brief 스트림/시프트 연산자입니다. */
        Archive& operator>>( float2& outData );
        /** @brief 스트림/시프트 연산자입니다. */
        Archive& operator>>( float3& outData );
        /** @brief 스트림/시프트 연산자입니다. */
        Archive& operator>>( float4& outData );
        /** @brief 스트림/시프트 연산자입니다. */
        Archive& operator>>( float4x4& outData );

        // ------------------------------------------------------------------------------
        // 1) 기본 바이너리 직렬화/역직렬화 (BinarySerializer 연계)
        // ------------------------------------------------------------------------------
        /** @brief 타입 정보를 이용해 객체를 직렬화합니다. */
        bool serializeObject( void* pInstance, const TypeInfo* pTypeInfo );
        /** @brief 타입 정보를 이용해 객체를 직렬화합니다. */
        bool serializeObject( void* pInstance, const TypeInfo& typeInfo );
        /** @brief 타입 정보를 이용해 객체를 역직렬화합니다. */
        bool deserializeObject( void* pInstance, const TypeInfo* pTypeInfo );
        /** @brief 타입 정보를 이용해 객체를 역직렬화합니다. */
        bool deserializeObject( void* pInstance, const TypeInfo& typeInfo );

        template <typename T>
        bool serializeObject( const T& instance )
        {
            return serializeObject( const_cast<T*>( &instance ), T::StaticType() );
        }

        template <typename T>
        bool deserializeObject( T& instance )
        {
            return deserializeObject( &instance, T::StaticType() );
        }

        template <typename T>
        bool serializeObject( const T* pInstance )
        {
            return ( pInstance != nullptr ) ? serializeObject( const_cast<T*>( pInstance ), T::StaticType() ) : false;
        }

        template <typename T>
        bool deserializeObject( T* pInstance )
        {
            return ( pInstance != nullptr ) ? deserializeObject( pInstance, T::StaticType() ) : false;
        }

        // ------------------------------------------------------------------------------
        // 2) 압축 섹션 및 압축 객체 직렬화 (CompressionStream / BinarySerializer 연계)
        // ------------------------------------------------------------------------------
        /** @brief 압축 코덱을 적용하여 페이로드 블록을 아카이브에 기록합니다. */
        bool writeCompressedSection( const void* pData, uint32 byteSize, CompressionCodecType codecType = CompressionCodecType::RLE );
        /** @brief 압축된 페이로드 블록을 읽고 원본 바이트로 복원합니다. */
        bool readCompressedSection( vector<uint8>& outBytes );

        /** @brief 객체를 압축 바이너리로 직렬화하여 아카이브에 기록합니다. */
        bool serializeCompressedObject( const void* pInstance, const TypeInfo& typeInfo, CompressionCodecType codecType = CompressionCodecType::RLE );
        /** @brief 아카이브의 압축 바이너리로부터 객체를 역직렬화합니다. */
        bool deserializeCompressedObject( void* pInstance, const TypeInfo& typeInfo );

        template <typename T>
        bool serializeCompressedObject( const T& instance, CompressionCodecType codecType = CompressionCodecType::RLE )
        {
            return serializeCompressedObject( &instance, *T::StaticType(), codecType );
        }

        template <typename T>
        bool deserializeCompressedObject( T& instance )
        {
            return deserializeCompressedObject( &instance, *T::StaticType() );
        }

        // ------------------------------------------------------------------------------
        // 3) 버전 관리 객체 직렬화 (BinarySerializer + SchemaMigrate 연계)
        // ------------------------------------------------------------------------------
        /** @brief 버전 헤더를 포함하여 객체를 직렬화합니다. */
        bool serializeVersionedObject( uint32 version, const void* pInstance, const TypeInfo& typeInfo );
        /** @brief 버전 헤더를 검증/마이그레이션하며 객체를 역직렬화합니다. */
        bool deserializeVersionedObject( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
                                         uint32 currentVersion = 0, SchemaMigrateFn migrate = nullptr, const TypeInfo* pLegacyTypeInfo = nullptr );

        template <typename T>
        bool serializeVersionedObject( uint32 version, const T& instance )
        {
            return serializeVersionedObject( version, &instance, *T::StaticType() );
        }

        template <typename T>
        bool deserializeVersionedObject( uint32& outVersion, T& instance, uint32 currentVersion = 0, SchemaMigrateFn migrate = nullptr, const TypeInfo* pLegacyTypeInfo = nullptr )
        {
            return deserializeVersionedObject( outVersion, &instance, *T::StaticType(), currentVersion, migrate, pLegacyTypeInfo );
        }

        // ------------------------------------------------------------------------------
        // 4) JSON 임베딩 및 상호 변환 (JsonSerializer 연계)
        // ------------------------------------------------------------------------------
        /** @brief 객체를 JSON 문자열로 직렬화하여 아카이브에 임베딩합니다. */
        bool serializeJsonObject( const void* pInstance, const TypeInfo& typeInfo, bool bPretty = false );
        /** @brief 아카이브의 임베디드 JSON 문자열로부터 객체를 역직렬화합니다. */
        bool deserializeJsonObject( void* pInstance, const TypeInfo& typeInfo );

        template <typename T>
        bool serializeJsonObject( const T& instance, bool bPretty = false )
        {
            return serializeJsonObject( &instance, *T::StaticType(), bPretty );
        }

        template <typename T>
        bool deserializeJsonObject( T& instance )
        {
            return deserializeJsonObject( &instance, *T::StaticType() );
        }

        /** @brief JSON 문자열을 콤팩트 바이너리로 변환하여 아카이브에 기록합니다. */
        bool convertJsonToBinary( string_view jsonStr, const TypeInfo& typeInfo );
        /** @brief 아카이브의 바이너리 객체를 JSON 문자열로 변환하여 반환합니다. */
        string convertBinaryToJson( const TypeInfo& typeInfo, bool bPretty = false );

        // ------------------------------------------------------------------------------
        // 5) XML 임베딩 및 상호 변환 (XmlSerializer 연계)
        // ------------------------------------------------------------------------------
        /** @brief 객체를 XML 문자열로 직렬화하여 아카이브에 임베딩합니다. */
        bool serializeXmlObject( const void* pInstance, const TypeInfo& typeInfo );
        /** @brief 아카이브의 임베디드 XML 문자열로부터 객체를 역직렬화합니다. */
        bool deserializeXmlObject( void* pInstance, const TypeInfo& typeInfo );

        template <typename T>
        bool serializeXmlObject( const T& instance )
        {
            return serializeXmlObject( &instance, *T::StaticType() );
        }

        template <typename T>
        bool deserializeXmlObject( T& instance )
        {
            return deserializeXmlObject( &instance, *T::StaticType() );
        }

        /** @brief XML 문자열을 콤팩트 바이너리로 변환하여 아카이브에 기록합니다. */
        bool convertXmlToBinary( string_view xmlStr, const TypeInfo& typeInfo );
        /** @brief 아카이브의 바이너리 객체를 XML 문자열로 변환하여 반환합니다. */
        string convertBinaryToXml( const TypeInfo& typeInfo );

        // ------------------------------------------------------------------------------
        // 6) 가변 길이 정수 (VarInt / ZigZag) 스트리밍
        // ------------------------------------------------------------------------------
        /** @brief 64비트 부호 없는 정수를 LEB128 가변 길이 정수로 기록합니다. */
        void writeVarUInt( uint64 value );
        /** @brief 64비트 부호 있는 정수를 ZigZag + LEB128 가변 길이 정수로 기록합니다. */
        void writeVarInt( int64 value );

        /** @brief LEB128 인코딩된 64비트 부호 없는 정수를 읽습니다. */

        bool readVarUInt( uint64& outValue );
        /** @brief LEB128 인코딩된 32비트 부호 없는 정수를 읽습니다. */
        bool readVarUInt( uint32& outValue );
        /** @brief ZigZag + LEB128 인코딩된 64비트 부호 있는 정수를 읽습니다. */
        bool readVarInt( int64& outValue );
        /** @brief ZigZag + LEB128 인코딩된 32비트 부호 있는 정수를 읽습니다. */
        bool readVarInt( int32& outValue );

        // ------------------------------------------------------------------------------
        // 7) 문자열 풀링 (StringPool / Interning)
        // ------------------------------------------------------------------------------
        /** @brief 문자열을 풀에 등록하고 풀 인덱스(VarUInt)를 기록합니다. */
        void writePooledString( string_view str );
        /** @brief 풀 인덱스를 읽어 해당 문자열을 반환합니다. */
        bool readPooledString( string& outStr );

        /** @brief 아카이브 내장 StringPool 참조 */
        StringPool&       getStringPool() { return _stringPool; }
        const StringPool& getStringPool() const { return _stringPool; }

        /** @brief StringPool 테이블 전체를 아카이브에 기록합니다. */
        void saveStringPool();
        /** @brief 아카이브로부터 StringPool 테이블 전체를 로드합니다. */
        bool loadStringPool();

        // ------------------------------------------------------------------------------
        // 8) 적응형 컴팩트 직렬화 (Presence Bitmask & Sparse Index)
        // ------------------------------------------------------------------------------
        /** @brief 객체를 적응형 컴팩트 바이너리(Dense Bitmask 또는 Sparse Index)로 직렬화합니다. */
        bool serializeCompactObject( const void* pInstance, const TypeInfo& typeInfo, const SerializeContext& ctx = SerializeContext::getDefault() );
        /** @brief 아카이브에서 적응형 컴팩트 바이너리를 역직렬화합니다. */
        bool deserializeCompactObject( void* pInstance, const TypeInfo& typeInfo, const SerializeContext& ctx = SerializeContext::getDefault() );

        template <typename T>
        bool serializeCompactObject( const T& instance, const SerializeContext& ctx = SerializeContext::getDefault() )
        {
            return serializeCompactObject( &instance, *T::StaticType(), ctx );
        }

        template <typename T>
        bool deserializeCompactObject( T& instance, const SerializeContext& ctx = SerializeContext::getDefault() )
        {
            return deserializeCompactObject( &instance, *T::StaticType(), ctx );
        }

    private:
        StringPool             _stringPool;
        vector<uint8>          _listBuffer;
        string                 _sourceDirectory;
        string                 _sourceFileName;
        const uint8*           _pData;
        uint64                 _dataSize;
        uint64                 _offset;
        uint8                  _bReadMode : 1;
        uint8                  _bError    : 1;
        [[maybe_unused]] uint8 _reserved  : 6;
    };
} // namespace sw
