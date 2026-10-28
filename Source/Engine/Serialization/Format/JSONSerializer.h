/**
 * @file JSONSerializer.h
 * @brief TypeInfo 리플렉션 기반 JSON 직렬화 · 역직렬화입니다.
 * @note 리플렉션이 아닌 콘텐츠(테이블, 툴)는 Serialization/JSON/JSONDocument 를 씁니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/Serializer/SchemaMigrate.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    enum class FieldReadResult : uint8;

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
                                 const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief 들여쓰기한 Pretty JSON 으로 직렬화합니다. */
        static string serializePretty( const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
                                       const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief JSON 문자열에서 객체를 역직렬화합니다. */
        [[nodiscard]] static bool deserialize( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                               const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief Pretty JSON 을 절대 경로에 씁니다. indentSpaces 가 0 이면 serializePretty 와 같이 4 칸을 씁니다. */
        [[nodiscard]] static bool saveFile( string_view absPath, const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
                                            const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );
        /** @brief 절대 · 리소스 경로에서 JSON 을 읽어 역직렬화합니다. */
        [[nodiscard]] static bool loadFile( const AssetID& assetID, void* pInstance, const TypeInfo& typeInfo,
                                            const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief JSONValue 객체에 리플렉션 필드를 씁니다. dst 는 객체여야 합니다. */
        static void writeObject( JSONValue dst, const void* pInstance, const TypeInfo& typeInfo,
                                 const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );
        /**
         * @brief JSONValue 객체에서 리플렉션 필드를 읽습니다.
         * @return 금지된 필드가 있거나 읽지 못한 필드를 되돌리지 못하면(안쪽 구조체 · 컨테이너 원소 포함) `ObjectRejected` 입니다.
         *         객체가 아니거나, 엄격 읽기(@p pOutListOrphan 없음)에서 읽지 못한 필드가 있거나, 선언된 범위 밖이면 `FieldFailed` 입니다(`FieldReadResult`).
         */
        [[nodiscard]] static FieldReadResult readObject( JSONValue src, void* pInstance, const TypeInfo& typeInfo,
                                                         vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
                                                         const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        // ------------------------------------------------------------------------------
        // 2) Soft · 버전: orphan 수집, _schemaVersion
        // ------------------------------------------------------------------------------
        /**
         * @brief Soft 역직렬화입니다. 변환하지 못한 필드를 orphan 으로 모읍니다.
         * @param pOutVersion nullptr 가 아니면 kSchemaVersionKey 값을 적습니다(없으면 0).
         * @return 본문이 JSON 객체가 아니면 `NotParsed`, 범위 밖 값이 있으면 `Rejected`(orphan 은 끝까지 모은다)입니다(`SoftReadResult`).
         *         읽지 못한 필드는 orphan 으로 넘어가고 **읽기 전 값**으로 남습니다(`PropertyReadRollback`). 되돌릴 수 없으면(읽다 만 소유 포인터 목록) 그 자리에서 `Rejected` 입니다.
         *         안쪽(구조체 칸, 컨테이너 원소인 구조체 · 컴포넌트)의 금지된 필드도 그 필드를 넘기지 않고 `Rejected` 입니다(`FieldReadResult::ObjectRejected`).
         */
        [[nodiscard]] static SoftReadResult deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                                             vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
                                                             const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief 루트에 `_schemaVersion` 을 붙여 JSON 으로 직렬화합니다. */
        static string serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
                                          const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief 버전을 읽고 soft 역직렬화합니다. 판이 @p currentVersion 과 다르면 이관하지 않고 false 입니다(정확 일치). */
        [[nodiscard]] static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                                        uint32                  currentVersion = 0,
                                                        const SerializeContext& context        = SerializeContext::getDefault( SerializePurpose::Authoring ) );
    };

} // namespace sw
