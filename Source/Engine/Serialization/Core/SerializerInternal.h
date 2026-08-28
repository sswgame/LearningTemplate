/**
 * @file SerializerInternal.h
 * @brief 직렬화기(Binary/JSON/XML/ObjectDiff) 공유 공통 헬퍼 (Engine TU 전용)
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 바이너리 — 스칼라/중첩 컨테이너
	// ------------------------------------------------------------------------------
	/** @brief 값을 바이너리로 직렬화합니다. */
	void serializeValueBinary( const void* pValuePtr, const hashed_string& typeName,
							   vector<uint8>& listBuffer, const SerializeContext& ctx );
	/** @brief 바이너리에서 값을 역직렬화합니다. */
	bool deserializeValueBinary( void* pValuePtr, const hashed_string& typeName,
								 const uint8* pData, size_t dataSize, size_t& offset,
								 const SerializeContext& ctx );

	/** @brief 중첩 컨테이너를 바이너리로 직렬화합니다. */
	void serializeNestedContainerBinary( const void* pContainerPtr, const NestedContainerInfo& nested,
										 vector<uint8>& listBuffer, const SerializeContext& ctx );
	/** @brief 바이너리에서 중첩 컨테이너를 역직렬화합니다. */
	bool deserializeNestedContainerBinary( void* pContainerPtr, const NestedContainerInfo& nested,
										   const uint8* pData, size_t dataSize, size_t& offset,
										   const SerializeContext& ctx );

	// ------------------------------------------------------------------------------
	// 2) 텍스트 변환 및 파싱 · 프로퍼티 기본값
	// ------------------------------------------------------------------------------
	/** @brief 평문/중첩 문자열로 씁니다. 주변 따옴표 없음 (XML/JSON/SchemaMigrate 빌딩 블록). */
	void valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
					  const SerializeContext& ctx );

	/** @brief 텍스트 토큰을 값으로 파싱합니다. */
	bool parseTextValue( void* pValPtr, const hashed_string& typeName, string_view valStr,
						 const SerializeContext& ctx );

	/** @brief 프로퍼티 기본값을 적용합니다. */
	bool applyPropertyDefault( void* pPropPtr, const PropertyInfo& prop, const SerializeContext& ctx );

	/** @brief 컨테이너 TypeInfo 이름을 태그로 변환합니다 (`vector`, `map`). */
	const utf8* containerTypeTagName( hashed_string typeName );

	// ------------------------------------------------------------------------------
	// 3) 프로퍼티/키 매칭 공통 헬퍼
	// ------------------------------------------------------------------------------
	/** @brief 키 문자열이 일치하는지 비교합니다 (대소문자 옵션 지원). */
	bool keysEqual( string_view a, string_view b, bool bIgnoreCase );

	/** @brief 프로퍼티 목록에서 키에 일치하는 프로퍼티 메타데이터를 검색합니다. */
	const PropertyInfo* matchProperty( const vector<PropertyInfo>& listProps, string_view keyRaw,
									   bool bIgnoreCaseKeys, bool& bCaseVariant );

} // namespace sw
