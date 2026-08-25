/**
 * @file SerializerInternal.h
 * @brief Binary/JSON/XML/ObjectDiff 직렬화기 공유 헬퍼 (Engine TU 전용)
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Utility/Json/JsonDocument.h"

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
	// 2) 텍스트 파싱 · 프로퍼티 기본값
	// ------------------------------------------------------------------------------
	/** @brief 텍스트 토큰을 값으로 파싱합니다. */
	bool parseTextValue( void* pValPtr, const hashed_string& typeName, string_view valStr,
						 const SerializeContext& ctx );
	/** @brief 프로퍼티 기본값을 적용합니다. */
	bool applyPropertyDefault( void* pPropPtr, const PropertyInfo& prop, const SerializeContext& ctx );

	// ------------------------------------------------------------------------------
	// 3) 텍스트/JSON 출력 — XML용 평문, JSON 토큰, 중첩 컨테이너
	// ------------------------------------------------------------------------------
	/** @brief 평문/중첩 JSON 객체로 씁니다. 주변 따옴표 없음 (XML 및 JSON 빌딩 블록). */
	void valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
					  const SerializeContext& ctx );
	/** @brief JSON 토큰으로 씁니다. 문자열/enum은 따옴표, 숫자/bool/객체는 JSON 리터럴. */
	void valueToJson( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
					  const SerializeContext& ctx );
	/** @brief dst 노드에 JSON 값을 씁니다. */
	void writeJsonValue( JsonValue dst, const void* pValPtr, const hashed_string& typeName, const SerializeContext& ctx );
	/** @brief 중첩 컨테이너를 JSON으로 이어 붙입니다. */
	void appendNestedContainerJson( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pContainerPtr,
									const NestedContainerInfo& nested, const SerializeContext& ctx );
	/** @brief 중첩 컨테이너를 dst 노드에 씁니다. */
	void writeNestedContainerJson( JsonValue dst, const void* pContainerPtr, const NestedContainerInfo& nested,
								   const SerializeContext& ctx );
	/** @brief JSON에서 중첩 컨테이너를 파싱합니다. */
	bool parseNestedContainerFromJson( void* pContainerPtr, const NestedContainerInfo& nested,
									   string_view json, const SerializeContext& ctx );
	/** @brief JsonValue에서 중첩 컨테이너를 읽습니다. */
	bool readNestedContainerJson( void* pContainerPtr, const NestedContainerInfo& nested, const JsonValue& src,
								  const SerializeContext& ctx );
	/** @brief JsonValue를 값으로 읽습니다. */
	bool readJsonValue( void* pValPtr, const hashed_string& typeName, const JsonValue& src, const SerializeContext& ctx );

	// ------------------------------------------------------------------------------
	// 4) 프로퍼티/키 매칭 공통 헬퍼
	// ------------------------------------------------------------------------------
	/** @brief 키 문자열이 일치하는지 비교합니다 (대소문자 옵션 지원). */
	bool keysEqual( string_view a, string_view b, bool bIgnoreCase );

	/** @brief 프로퍼티 목록에서 키에 일치하는 프로퍼티 메타데이터를 검색합니다. */
	const PropertyInfo* matchProperty( const vector<PropertyInfo>& listProps, string_view keyRaw,
									   bool bIgnoreCaseKeys, bool& bCaseVariant );

} // namespace sw
