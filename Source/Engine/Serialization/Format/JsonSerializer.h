/**
 * @file JsonSerializer.h
 * @brief TypeInfo 리플렉션 기반 JSON 직렬화/역직렬화
 * @note 리플렉션이 아닌 콘텐츠(테이블, 툴)는 Utility/Json/JsonDocument를 사용합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
	struct TypeInfo;

	/**
	 * @class JsonSerializer
	 * @brief TypeInfo 리플렉션으로 JSON을 쓰고 읽습니다
	 */
	class SW_API JsonSerializer
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 문자열 유틸 — escape, 필드 추출
		// ------------------------------------------------------------------------------
		/** @brief JSON 따옴표 값 안에 넣을 문자열을 이스케이프합니다. */
		static string escapeString( string_view value );
		/** @brief JSON 문자열 값의 이스케이프를 해제합니다 (주변 따옴표 제외). */
		static string unescapeString( string_view value );
		/**
		 * @brief 최상위 `"field": "value"` 문자열을 추출합니다 (단순 객체 형태).
		 * @param bIgnoreCaseKeys 필드 이름 비교 ignore-case (기본 true). 값 문자열은 그대로 유지.
		 */
		static string extractStringField( string_view json, string_view fieldName,
										  bool bIgnoreCaseKeys = true );

		// ------------------------------------------------------------------------------
		// 2) 직렬화 / 역직렬화
		// ------------------------------------------------------------------------------
		/**
		 * @brief 한 줄 단축 JSON으로 직렬화합니다.
		 * @details 스칼라는 프로퍼티 키. 컨테이너는 `"vector":[{ "_name":"_scores", "item":[...] }]`.
		 */
		static string serialize( const void* pInstance, const TypeInfo& typeInfo,
								 const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 들여쓰기가 있는 Pretty JSON으로 직렬화합니다. */
		static string serializePretty( const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
									   const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief JSON 문자열에서 객체를 역직렬화합니다. */
		static bool deserialize( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
								 const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief Pretty JSON을 절대 경로에 씁니다. indentSpaces==0 이면 serializePretty 기본(4). */
		static bool saveFile( string_view absPath, const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
							  const SerializeContext& ctx = SerializeContext::getDefault() );
		/** @brief 절대/리소스 경로에서 JSON을 읽어 역직렬화합니다. */
		static bool loadFile( string_view path, void* pInstance, const TypeInfo& typeInfo,
							  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief JsonValue 객체에 리플렉션 필드를 씁니다. dst는 객체여야 합니다. */
		static void writeObject( JsonValue dst, const void* pInstance, const TypeInfo& typeInfo,
								 const SerializeContext& ctx = SerializeContext::getDefault() );
		/** @brief JsonValue 객체에서 리플렉션 필드를 읽습니다. */
		static bool readObject( JsonValue src, void* pInstance, const TypeInfo& typeInfo,
								vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
								const SerializeContext& ctx = SerializeContext::getDefault() );

		// ------------------------------------------------------------------------------
		// 3) Soft · 버전 — orphan 수집, _schemaVersion
		// ------------------------------------------------------------------------------
		/**
		 * @brief Soft 역직렬화 — coerce 실패 필드를 orphan으로 수집.
		 * @param pOutVersion null이 아니면 kSchemaVersionKey 값을 기록 (없으면 0).
		 */
		static bool deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
									 vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
									 const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 루트에 `_schemaVersion`을 포함한 JSON 직렬화. */
		static string serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
										  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 버전 헤더 + soft deserialize, 필요 시 migrate. */
		static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
										  uint32 currentVersion = 0, SchemaMigrateFn migrate = nullptr,
										  const TypeInfo*		  pLegacyTypeInfo = nullptr,
										  const SerializeContext& ctx			  = SerializeContext::getDefault() );
	};

} // namespace sw
