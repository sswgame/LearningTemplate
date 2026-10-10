/**
 * @file JSONSerializer.h
 * @brief TypeInfo 리플렉션 기반 JSON 직렬화 · 역직렬화입니다.
 * @note 리플렉션이 아닌 콘텐츠(테이블, 툴)는 Serialization/JSON/JSONDocument 를 씁니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @class JSONSerializer
     * @brief TypeInfo 리플렉션으로 JSON 을 쓰고 읽습니다.
     */
    class SW_API JSONSerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 직렬화 / 역직렬화
        // ------------------------------------------------------------------------------
        /**
         * @brief 한 줄로 압축한 JSON 으로 직렬화합니다.
         * @details 스칼라는 프로퍼티 키의 값으로 씁니다. 시퀀스는 배열(`"_scores":[1,2]`), 맵은 오브젝트(`"_stat":{"a":1}`)입니다.
         */
        static string serialize( const void* pInstance, const TypeInfo& typeInfo,
                                 const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief 들여쓰기한 Pretty JSON 으로 직렬화합니다. */
        static string serializePretty( const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
                                       const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief JSON 문자열에서 객체를 역직렬화합니다. */
        [[nodiscard]] static bool deserialize( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                               const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief Pretty JSON 을 절대 경로에 씁니다. indentSpaces 가 0 이면 serializePretty 와 같이 4 칸을 씁니다. */
        [[nodiscard]] static bool saveFile( string_view absPath, const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
                                            const SerializeContext& context = SerializeContext::getDefault() );
        /** @brief 절대 · 리소스 경로에서 JSON 을 읽어 역직렬화합니다. */
        [[nodiscard]] static bool loadFile( string_view path, void* pInstance, const TypeInfo& typeInfo,
                                            const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief JSONValue 객체에 리플렉션 필드를 씁니다. dst 는 객체여야 합니다. */
        static void writeObject( JSONValue dst, const void* pInstance, const TypeInfo& typeInfo,
                                 const SerializeContext& context = SerializeContext::getDefault() );
        /** @brief JSONValue 객체에서 리플렉션 필드를 읽습니다. */
        [[nodiscard]] static bool readObject( JSONValue src, void* pInstance, const TypeInfo& typeInfo,
                                              vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
                                              const SerializeContext& context = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 2) Soft · 버전: orphan 수집, _schemaVersion
        // ------------------------------------------------------------------------------
        /**
         * @brief Soft 역직렬화입니다. 변환하지 못한 필드를 orphan 으로 모읍니다.
         * @param pOutVersion nullptr 가 아니면 kSchemaVersionKey 값을 적습니다(없으면 0).
         */
        [[nodiscard]] static bool deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                                   vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
                                                   const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief 루트에 `_schemaVersion` 을 붙여 JSON 으로 직렬화합니다. */
        static string serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
                                          const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief 버전을 읽고 soft 역직렬화한 뒤, 필요하면 migrate 를 부릅니다. */
        [[nodiscard]] static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                                        uint32 currentVersion = 0, SchemaMigrateFn migrate = nullptr,
                                                        const TypeInfo*         pLegacyTypeInfo = nullptr,
                                                        const SerializeContext& context         = SerializeContext::getDefault() );
    };

} // namespace sw
