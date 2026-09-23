/**
 * @file JsonSerializer.h
 * @brief TypeInfo 리플렉션 기반 JSON 직렬화 · 역직렬화입니다.
 * @note 리플렉션이 아닌 콘텐츠(테이블, 툴)는 Utility/Json/JsonDocument 를 씁니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    struct TypeInfo;

    class Archive;

    /**
     * @class JsonSerializer
     * @brief TypeInfo 리플렉션으로 JSON 을 쓰고 읽습니다.
     */
    class SW_API JsonSerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 문자열 도우미: 이스케이프, 필드 추출
        // ------------------------------------------------------------------------------
        /** @brief JSON 따옴표 값 안에 넣을 문자열을 이스케이프합니다. */
        static string escapeString( string_view value );
        /** @brief JSON 문자열 값의 이스케이프를 풉니다(바깥 따옴표는 제외). */
        static string unescapeString( string_view value );
        /**
         * @brief 최상위 `"field": "value"` 문자열을 뽑습니다(단순 객체 형태).
         * @param bIgnoreCaseKeys 필드 이름을 비교할 때 대소문자를 무시할지 여부(기본 true). 값 문자열은 그대로 둡니다.
         */
        static string extractStringField( string_view json, string_view fieldName,
                                          bool bIgnoreCaseKeys = true );

        // ------------------------------------------------------------------------------
        // 2) 직렬화 / 역직렬화
        // ------------------------------------------------------------------------------
        /**
         * @brief 한 줄로 압축한 JSON 으로 직렬화합니다.
         * @details 스칼라는 프로퍼티 키의 값으로 씁니다. 시퀀스는 배열(`"_scores":[1,2]`), 맵은 오브젝트(`"_stat":{"a":1}`)입니다.
         */
        static string serialize( const void* pInstance, const TypeInfo& typeInfo,
                                 const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 들여쓰기한 Pretty JSON 으로 직렬화합니다. */
        static string serializePretty( const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
                                       const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief JSON 문자열에서 객체를 역직렬화합니다. */
        static bool deserialize( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                 const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 객체를 JSON 으로 직렬화해 Archive 에 기록합니다. */
        static bool serializeToArchive( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                        bool bPretty = false, const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief Archive 에서 JSON 문자열을 읽어 객체로 역직렬화합니다. */
        static bool deserializeFromArchive( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                            const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief Pretty JSON 을 절대 경로에 씁니다. indentSpaces 가 0 이면 serializePretty 와 같이 4 칸을 씁니다. */
        static bool saveFile( string_view absPath, const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
                              const SerializeContext& ctx = SerializeContext::getDefault() );
        /** @brief 절대 · 리소스 경로에서 JSON 을 읽어 역직렬화합니다. */
        static bool loadFile( string_view path, void* pInstance, const TypeInfo& typeInfo,
                              const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief JsonValue 객체에 리플렉션 필드를 씁니다. dst 는 객체여야 합니다. */
        static void writeObject( JsonValue dst, const void* pInstance, const TypeInfo& typeInfo,
                                 const SerializeContext& ctx = SerializeContext::getDefault() );
        /** @brief JsonValue 객체에서 리플렉션 필드를 읽습니다. */
        static bool readObject( JsonValue src, void* pInstance, const TypeInfo& typeInfo,
                                vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
                                const SerializeContext& ctx = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 3) Soft · 버전: orphan 수집, _schemaVersion
        // ------------------------------------------------------------------------------
        /**
         * @brief Soft 역직렬화입니다. 변환하지 못한 필드를 orphan 으로 모읍니다.
         * @param pOutVersion nullptr 가 아니면 kSchemaVersionKey 값을 적습니다(없으면 0).
         */
        static bool deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                     vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
                                     const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 루트에 `_schemaVersion` 을 붙여 JSON 으로 직렬화합니다. */
        static string serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
                                          const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 버전을 읽고 soft 역직렬화한 뒤, 필요하면 migrate 를 부릅니다. */
        static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                          uint32 currentVersion = 0, SchemaMigrateFn migrate = nullptr,
                                          const TypeInfo*         pLegacyTypeInfo = nullptr,
                                          const SerializeContext& ctx             = SerializeContext::getDefault() );
    };

} // namespace sw
