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

    /**
     * @class BinarySerializer
     * @brief TypeInfo 로 객체를 콤팩트 바이너리 버퍼에 쓰고 읽습니다(압축 직렬화도 함께 지원합니다).
     * @details 스트림은 프로퍼티마다 이름 해시와 기록 타입을 싣는 자기 기술형이라 스키마가 바뀐 뒤에도 읽습니다.
     *          프로퍼티 순서로 짝을 맞추던 컴팩트 모드는 스키마 확인 없이 엉뚱한 필드에 읽혀 지웠습니다. 같은 빌드끼리
     *          주고받는 짧은 바이트는 스키마 해시로 배치를 확인하는 고정 배치형(`FixedLayout`)을 씁니다.
     */
    class SW_API BinarySerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 기본 비압축: 엄격하게 읽는다. 실패해도 이미 쓴 프로퍼티는 되돌리지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 객체를 콤팩트 바이너리로 직렬화합니다. */
        static void serialize( const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                               const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief Archive 에 객체를 콤팩트 바이너리로 직렬화합니다. */
        static void serialize( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                               const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief 바이너리에서 객체를 역직렬화합니다. **실패해도 되돌리지 않습니다.** 실패하기 전까지 읽은 프로퍼티는 이미 써진 채로 남습니다. */
        [[nodiscard]] static bool deserialize( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                               const SerializeContext& context = SerializeContext::getDefault() );

        /**
         * @brief Archive 의 현재 자리에서 객체를 역직렬화하고, 성공하면 읽기 자리를 그 객체 끝으로 옮깁니다.
         * @details Archive 판 역직렬화(버전 · 압축도)는 모두 같다 — 스트림이 스스로 끝을 알므로 같은 Archive 에 이어 쓴 것을 차례로 읽습니다.
         */
        [[nodiscard]] static bool deserialize( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                               const SerializeContext& context = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 2) Soft: 타입이 안 맞거나 모르는 필드는 orphan 으로 모으고 계속한다
        // ------------------------------------------------------------------------------
        /**
         * @brief Soft 역직렬화입니다. 타입이 안 맞거나 모르는 필드는 orphan 으로 모으고 계속 진행합니다.
         * @details 읽지 못한 필드도 payload 크기만큼 건너뜁니다. tryCoerceBinaryPayload 로 int32↔string 등의 변환을 자동으로 시도합니다.
         */
        [[nodiscard]] static bool deserializeSoft( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                                   vector<SchemaOrphanValue>* pOutOrphans = nullptr,
                                                   const SerializeContext&    context     = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 3) 버전: 헤더 + migrate
        // ------------------------------------------------------------------------------
        /** @brief 버전 헤더를 붙여 바이너리로 직렬화합니다. */
        static void serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                                        const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief Archive 에 버전 바이너리로 직렬화합니다. */
        static void serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                        const SerializeContext& context = SerializeContext::getDefault() );

        /**
         * @brief 버전 헤더를 읽고 soft 역직렬화한 뒤, 필요하면 등록된 이관(`SchemaMigrationRegistry`, (타입, 기록된 버전)으로 찾는다)을 부릅니다.
         * @param pLegacyTypeInfo 주면 그 스키마로도 스테이징 로드해 migrate 컨텍스트의 `_pLegacyInstance` 로 넘깁니다.
         * @return 등록된 이관 없이 버전이 다르거나 orphan 이 있으면 false 입니다.
         */
        [[nodiscard]] static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                                        uint32                  currentVersion  = 0,
                                                        const TypeInfo*         pLegacyTypeInfo = nullptr,
                                                        const SerializeContext& context         = SerializeContext::getDefault() );

        /** @brief Archive 에서 버전 바이너리를 역직렬화합니다. */
        [[nodiscard]] static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                                        uint32                  currentVersion  = 0,
                                                        const TypeInfo*         pLegacyTypeInfo = nullptr,
                                                        const SerializeContext& context         = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 4) 압축 바이너리 직렬화 (CompressionStream 통합)
        // ------------------------------------------------------------------------------
        /** @brief 객체를 바이너리로 직렬화한 뒤 지정된 코덱으로 압축합니다. */
        [[nodiscard]] static bool serializeCompressed( const void*             pInstance,
                                                       const TypeInfo&         typeInfo,
                                                       vector<uint8>&          outListBuffer,
                                                       CompressionCodecType    codecType = CompressionCodecType::RLE,
                                                       const SerializeContext& context   = SerializeContext::getDefault() );

        /** @brief Archive 에 압축 바이너리로 직렬화합니다. */
        [[nodiscard]] static bool serializeCompressed( const void*             pInstance,
                                                       const TypeInfo&         typeInfo,
                                                       Archive&                outArchive,
                                                       CompressionCodecType    codecType = CompressionCodecType::RLE,
                                                       const SerializeContext& context   = SerializeContext::getDefault() );

        /** @brief 압축된 바이너리 스트림을 풀어 객체로 역직렬화합니다. */
        [[nodiscard]] static bool deserializeCompressed( void*                   pInstance,
                                                         const TypeInfo&         typeInfo,
                                                         const uint8*            pData,
                                                         size_t                  dataSize,
                                                         const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief Archive 에서 압축 바이너리를 역직렬화합니다. */
        [[nodiscard]] static bool deserializeCompressed( void*                   pInstance,
                                                         const TypeInfo&         typeInfo,
                                                         Archive&                inArchive,
                                                         const SerializeContext& context = SerializeContext::getDefault() );
    };

} // namespace sw
