/**
 * @file BinarySerializer.h
 * @brief TypeInfo 기반 콤팩트 바이너리 직렬화 · 역직렬화입니다(비압축 · 압축 모두 지원).
 */
#pragma once
#include "Core/Compression/ICompressionCodec.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/SerializeContext.h"

namespace sw
{
    struct TypeInfo;

    class Archive;

    /** @brief `BinarySerializer::deserializeExactVersion` 의 결과입니다. */
    enum class [[nodiscard]] BinaryExactVersionResult : uint8
    {
        Read,            ///< 버전이 같고 본문을 읽었습니다
        VersionMismatch, ///< 버전 머리가 기대한 값과 다릅니다. 본문은 읽지 않았습니다
        Unreadable       ///< 버전 머리가 없거나 본문을 컨텍스트의 정책으로 읽지 못했습니다
    };

    /**
     * @class BinarySerializer
     * @brief TypeInfo 로 객체를 콤팩트 바이너리 버퍼에 쓰고 읽습니다(압축 직렬화도 함께 지원합니다).
     * @details 스트림은 프로퍼티마다 이름 해시와 기록 타입을 싣는 자기 기술형이라 스키마가 바뀐 뒤에도 읽습니다.
     *          프로퍼티 순서로 짝을 맞추던 컴팩트 모드는 스키마 확인 없이 엉뚱한 필드에 읽혀 지웠습니다. 같은 빌드끼리
     *          주고받는 짧은 바이트는 스키마 해시로 배치를 확인하는 고정 배치형(`FixedLayout`)을 씁니다.
     *          빌드가 섞인 서버끼리의 메시지와 캐시도 이 형식이므로(docs/plans/ServerBusFormat.md), 인코딩 버전(`kCurrentBinaryWireVersion`)을 올릴 때는
     *          한 배포 동안 옛 버전도 읽어야 합니다. 이 빌드는 지금 버전 하나만 읽으므로 그냥 올리면 섞인 동안 모든 버스 메시지와 캐시 레코드를 읽지 못합니다.
     */
    class SW_API BinarySerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 기본 비압축: 엄격하게 읽는다. 실패해도 이미 쓴 프로퍼티는 되돌리지 않는다
        // ------------------------------------------------------------------------------
        /**
         * @brief 객체를 콤팩트 바이너리로 직렬화합니다.
         * @return 프로퍼티 값 하나라도 실을 수 없으면(`SerializerUtil::serializeValueBinary`) 그 자리에서 멈추고 오류를 알리며 false 입니다. 그때 @p outListBuffer 는 쓰면 안 됩니다.
         */
        [[nodiscard]] static bool serialize( const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                                             const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief Archive 에 객체를 콤팩트 바이너리로 직렬화합니다. 실패하면 아무것도 싣지 않고 @p outArchive 를 오류 상태로 두며 false 입니다. */
        [[nodiscard]] static bool serialize( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                             const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief 바이너리에서 객체를 역직렬화합니다. **실패해도 되돌리지 않습니다.** 실패하기 전까지 읽은 프로퍼티는 이미 써진 채로 남습니다. */
        [[nodiscard]] static bool deserialize( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                               const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /**
         * @brief Archive 의 현재 자리에서 객체를 역직렬화하고, 성공하면 읽기 자리를 그 객체 끝으로 옮깁니다.
         * @details Archive 판 역직렬화(버전 · 압축도)는 모두 같다 — 스트림이 스스로 끝을 알므로 같은 Archive 에 이어 쓴 것을 차례로 읽습니다.
         */
        [[nodiscard]] static bool deserialize( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                               const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        // ------------------------------------------------------------------------------
        // 2) Soft: 타입이 안 맞거나 모르는 필드는 orphan 으로 모으고 계속한다
        // ------------------------------------------------------------------------------
        /**
         * @brief Soft 역직렬화입니다. 타입이 안 맞거나 모르는 필드는 orphan 으로 모으고 계속 진행합니다.
         * @details 읽지 못한 필드도 payload 크기만큼 건너뜁니다. 기록 타입이 다르면 tryCoerceBinaryPayload 로 int32↔string 등의 변환을 시도합니다.
         *          기록 타입이 같은데 payload 를 끝까지 읽지 못한 필드는 이관하지 않고 orphan 입니다(엄격 읽기와 같은 판정).
         * @return 스트림이 비었거나 깨졌으면(머리 · 태그 · 크기) `NotParsed`, 범위 밖 값이 있으면 `Rejected`(orphan 은 끝까지 모은다)입니다(`SoftReadResult`).
         */
        [[nodiscard]] static SoftReadResult deserializeSoft( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                                             vector<SchemaOrphanValue>* pOutOrphans = nullptr,
                                                             const SerializeContext&    context     = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        // ------------------------------------------------------------------------------
        // 3) 버전: 헤더 + migrate
        // ------------------------------------------------------------------------------
        /** @brief 버전 헤더를 붙여 바이너리로 직렬화합니다. 실패 규칙은 `serialize` 와 같습니다. */
        [[nodiscard]] static bool serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                                                      const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief Archive 에 버전 바이너리로 직렬화합니다. 실패 규칙은 `serialize( ..., Archive& )` 와 같습니다. */
        [[nodiscard]] static bool serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                                      const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /**
         * @brief 버전 헤더를 읽고 soft 역직렬화한 뒤, 필요하면 등록된 이관(`SchemaMigrationRegistry`, (타입, 기록된 버전)으로 찾는다)을 부릅니다.
         * @param pLegacyTypeInfo 주면 그 스키마로도 스테이징 로드해 migrate 컨텍스트의 `_pLegacyInstance` 로 넘깁니다.
         * @return 등록된 이관 없이 버전이 다르거나 orphan 이 있으면 false 입니다.
         */
        [[nodiscard]] static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                                        uint32                  currentVersion  = 0,
                                                        const TypeInfo*         pLegacyTypeInfo = nullptr,
                                                        const SerializeContext& context         = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief Archive 에서 버전 바이너리를 역직렬화합니다. */
        [[nodiscard]] static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                                        uint32                  currentVersion  = 0,
                                                        const TypeInfo*         pLegacyTypeInfo = nullptr,
                                                        const SerializeContext& context         = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /**
         * @brief `serializeVersioned` 가 쓴 바이트를 버전 머리가 @p expectedVersion 과 같을 때만 **엄격하게** 읽습니다. 이관 레지스트리와 orphan 수집을 지나지 않습니다.
         * @details 혼합 빌드 사이의 메시지와 캐시가 씁니다(docs/plans/ServerBusFormat.md 3.3). 모르는 이름 · 읽지 못한 필드의 처리는 @p context 의 두 플래그
         *          (`setSkipUnknownProperties` · `setSkipUnreadableProperties`)가 정하고, 중첩 구조체까지 같은 정책입니다. 기본 컨텍스트를 받지 않습니다 —
         *          부르는 쪽이 정책을 고릅니다. 실패해도 이미 쓴 프로퍼티는 되돌리지 않으므로 임시 객체에 읽습니다.
         * @param outReadVersion 읽은 버전 머리입니다. 머리가 없으면 0 입니다.
         */
        static BinaryExactVersionResult deserializeExactVersion( uint32 expectedVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData,
                                                                 size_t dataSize, uint32& outReadVersion, const SerializeContext& context );

        // ------------------------------------------------------------------------------
        // 4) 압축 바이너리 직렬화 (CompressionStream 통합)
        // ------------------------------------------------------------------------------
        /** @brief 객체를 바이너리로 직렬화한 뒤 지정된 코덱으로 압축합니다. */
        [[nodiscard]] static bool serializeCompressed( const void*             pInstance,
                                                       const TypeInfo&         typeInfo,
                                                       vector<uint8>&          outListBuffer,
                                                       CompressionCodecType    codecType = CompressionCodecType::RLE,
                                                       const SerializeContext& context   = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief Archive 에 압축 바이너리로 직렬화합니다. */
        [[nodiscard]] static bool serializeCompressed( const void*             pInstance,
                                                       const TypeInfo&         typeInfo,
                                                       Archive&                outArchive,
                                                       CompressionCodecType    codecType = CompressionCodecType::RLE,
                                                       const SerializeContext& context   = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief 압축된 바이너리 스트림을 풀어 객체로 역직렬화합니다. */
        [[nodiscard]] static bool deserializeCompressed( void*                   pInstance,
                                                         const TypeInfo&         typeInfo,
                                                         const uint8*            pData,
                                                         size_t                  dataSize,
                                                         const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief Archive 에서 압축 바이너리를 역직렬화합니다. */
        [[nodiscard]] static bool deserializeCompressed( void*                   pInstance,
                                                         const TypeInfo&         typeInfo,
                                                         Archive&                inArchive,
                                                         const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );
    };

} // namespace sw
