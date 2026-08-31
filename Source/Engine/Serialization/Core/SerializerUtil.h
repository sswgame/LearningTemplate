/**
 * @file SerializerUtil.h
 * @brief 직렬화기(Binary/JSON/XML/ObjectDiff/Archive) 공유 공통 헬퍼, 트랜스코딩 엔진 및 스크래치 RAII
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
	/**
	 * @struct PresenceMaskUtil
	 * @brief 적응형 밀집 비트마스크(Dense Bitmask) 및 희소 인덱스(Sparse Index) 비트 패킹/언패킹 유틸
	 */
	struct PresenceMaskUtil
	{
		static constexpr uint8 kModeDense  = 0x01;
		static constexpr uint8 kModeSparse = 0x02;

		/** @brief 밀집 모드 선택 기준: 수정 프로퍼티 수 >= 3개이고 수정 비율 >= 25% */
		static bool shouldUseDenseMode( size_t modifiedCount, size_t totalCount )
		{
			return ( modifiedCount >= 3 && modifiedCount * 4 >= totalCount );
		}

		/** @brief 비트마스크 바이트 수 계산: ceil(totalBits / 8) */
		static size_t calculateBitmaskBytes( size_t totalBits )
		{
			return ( totalBits + 7 ) / 8;
		}

		/** @brief 비트마스크에서 특정 비트 설정 */
		static void setBit( uint8* pBitmask, size_t bitIndex )
		{
			pBitmask[bitIndex / 8] |= static_cast<uint8>( 1 << ( bitIndex % 8 ) );
		}

		/** @brief 비트마스크에서 특정 비트 테스트 */
		static bool testBit( const uint8* pBitmask, size_t bitIndex )
		{
			return ( pBitmask[bitIndex / 8] & ( 1 << ( bitIndex % 8 ) ) ) != 0;
		}
	};

	/**
	 * @struct ScopedScratchInstance
	 * @brief 임시 스크래치 인스턴스의 생성 및 자동 파괴를 보장하는 RAII 래퍼
	 */
	struct ScopedScratchInstance
	{
		explicit ScopedScratchInstance( const TypeInfo* pTypeInfo )
			: _pTypeInfo{ pTypeInfo }
			, _listStorage{}
			, _pInstance{ ( pTypeInfo != nullptr && pTypeInfo->_size > 0 ) ? createScratchInstance( *pTypeInfo, _listStorage ) : nullptr }
		{
		}

		explicit ScopedScratchInstance( const TypeInfo& typeInfo )
			: ScopedScratchInstance( &typeInfo )
		{
		}

		~ScopedScratchInstance()
		{
			if ( _pInstance != nullptr && _pTypeInfo != nullptr )
			{
				destroyScratchInstance( _pInstance, *_pTypeInfo );
			}
		}

		ScopedScratchInstance( const ScopedScratchInstance& )				 = delete;
		ScopedScratchInstance& operator=( const ScopedScratchInstance& )	 = delete;
		ScopedScratchInstance( ScopedScratchInstance&& ) noexcept			 = delete;
		ScopedScratchInstance& operator=( ScopedScratchInstance&& ) noexcept = delete;

		void* get() const { return _pInstance; }
		bool  isValid() const { return _pInstance != nullptr; }

	private:
		const TypeInfo* _pTypeInfo{ nullptr };
		vector<uint8>	_listStorage;
		void*			_pInstance{ nullptr };
	};

	/** @brief 직렬화기 TU 공유 헬퍼 */
	struct SerializerUtil
	{
		/** @brief 값을 바이너리로 직렬화합니다. */
		SW_API static void serializeValueBinary( const void* pValuePtr, const hashed_string& typeName,
												 vector<uint8>& listBuffer, const SerializeContext& ctx );
		/** @brief 바이너리에서 값을 역직렬화합니다. */
		SW_API static bool deserializeValueBinary( void* pValuePtr, const hashed_string& typeName,
												   const uint8* pData, size_t dataSize, size_t& offset,
												   const SerializeContext& ctx );

		/** @brief 중첩 컨테이너를 바이너리로 직렬화합니다. */
		SW_API static void serializeNestedContainerBinary( const void* pContainerPtr, const NestedContainerInfo& nested,
														   vector<uint8>& listBuffer, const SerializeContext& ctx );
		/** @brief 바이너리에서 중첩 컨테이너를 역직렬화합니다. */
		SW_API static bool deserializeNestedContainerBinary( void* pContainerPtr, const NestedContainerInfo& nested,
															 const uint8* pData, size_t dataSize, size_t& offset,
															 const SerializeContext& ctx );

		/** @brief 평문/중첩 문자열로 씁니다. 주변 따옴표 없음 (XML/JSON/SchemaMigrate 빌딩 블록). */
		static void valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
								 const SerializeContext& ctx );

		/** @brief 텍스트 토큰을 값으로 파싱합니다. */
		static bool parseTextValue( void* pValPtr, const hashed_string& typeName, string_view valStr,
									const SerializeContext& ctx );

		/** @brief 프로퍼티 기본값을 적용합니다. */
		static bool applyPropertyDefault( void* pPropPtr, const PropertyInfo& prop, const SerializeContext& ctx );

		/** @brief 컨테이너 TypeInfo 이름을 태그로 변환합니다 (`vector`, `map`). */
		static const utf8* containerTypeTagName( hashed_string typeName );

		/** @brief 키 문자열이 일치하는지 비교합니다 (대소문자 옵션 지원). */
		static bool keysEqual( string_view a, string_view b, bool bIgnoreCase );

		/** @brief 프로퍼티 목록에서 키에 일치하는 프로퍼티 메타데이터를 검색합니다. */
		static const PropertyInfo* matchProperty( const vector<PropertyInfo>& listProp, string_view keyRaw,
												  bool bIgnoreCaseKeys, bool& bCaseVariant );

		/** @brief 프로퍼티가 직렬화 대상인지 검사합니다 (Transient 제외). */
		static bool shouldSerializeProperty( const PropertyInfo& prop )
		{
			return prop._metadata._bTransient == SW_FALSE;
		}

		/** @brief JSON 문자열을 바이너리 버퍼로 트랜스코딩합니다. */
		SW_API static bool transcodeJsonToBinary( string_view jsonStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
												  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 바이너리 데이터를 JSON 문자열로 트랜스코딩합니다. */
		SW_API static string transcodeBinaryToJson( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty = false,
													const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief XML 문자열을 바이너리 버퍼로 트랜스코딩합니다. */
		SW_API static bool transcodeXmlToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
												 const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 바이너리 데이터를 XML 문자열로 트랜스코딩합니다. */
		SW_API static string transcodeBinaryToXml( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
												   const SerializeContext& ctx = SerializeContext::getDefault() );
	};
} // namespace sw
