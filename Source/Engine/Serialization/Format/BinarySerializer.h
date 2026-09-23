/**
 * @file BinarySerializer.h
 * @brief TypeInfo 기반 콤팩트 바이너리 직렬화 · 역직렬화입니다(비압축 · 압축 모두 지원).
 */
#pragma once
#include "Core/Compression/ICompressionCodec.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
    struct TypeInfo;

    class Archive;

    /**
     * @class BinarySerializer
     * @brief TypeInfo 로 객체를 콤팩트 바이너리 버퍼에 쓰고 읽습니다(압축 직렬화도 함께 지원합니다).
     */
    class SW_API BinarySerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 기본 비압축: 엄격하게 읽는다. 실패해도 이미 쓴 프로퍼티는 되돌리지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 객체를 콤팩트 바이너리로 직렬화합니다. */
        static void serialize( const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                               const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief Archive 에 객체를 콤팩트 바이너리로 직렬화합니다. */
        static void serialize( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                               const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 바이너리에서 객체를 역직렬화합니다. **실패해도 되돌리지 않습니다.** 실패하기 전까지 읽은 프로퍼티는 이미 써진 채로 남습니다. */
        static bool deserialize( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                 const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief Archive 에서 객체를 역직렬화합니다. */
        static bool deserialize( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                 const SerializeContext& ctx = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 2) Soft: 타입이 안 맞거나 모르는 필드는 orphan 으로 모으고 계속한다
        // ------------------------------------------------------------------------------
        /**
         * @brief Soft 역직렬화입니다. 타입이 안 맞거나 모르는 필드는 orphan 으로 모으고 계속 진행합니다.
         * @details 읽지 못한 필드도 payload 크기만큼 건너뜁니다. tryCoerceBinaryPayload 로 int32↔string 등의 변환을 자동으로 시도합니다.
         */
        static bool deserializeSoft( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                     vector<SchemaOrphanValue>* pOutOrphans = nullptr,
                                     const SerializeContext&    ctx         = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 3) 버전: 헤더 + migrate, 복제
        // ------------------------------------------------------------------------------
        /** @brief 버전 헤더를 붙여 바이너리로 직렬화합니다. */
        static void serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                                        const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief Archive 에 버전 바이너리로 직렬화합니다. */
        static void serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                        const SerializeContext& ctx = SerializeContext::getDefault() );

        /**
         * @brief 버전 헤더를 읽고 soft 역직렬화한 뒤, 필요하면 migrate 를 부릅니다.
         * @param pLegacyTypeInfo 주면 그 스키마로도 스테이징 로드해 migrate 컨텍스트의 `_pLegacyInstance` 로 넘깁니다.
         * @return migrate 없이 버전이 다르거나 orphan 이 있으면 false 입니다.
         */
        static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                          uint32                  currentVersion  = 0,
                                          SchemaMigrateFn         migrate         = nullptr,
                                          const TypeInfo*         pLegacyTypeInfo = nullptr,
                                          const SerializeContext& ctx             = SerializeContext::getDefault() );

        /** @brief Archive 에서 버전 바이너리를 역직렬화합니다. */
        static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                          uint32                  currentVersion  = 0,
                                          SchemaMigrateFn         migrate         = nullptr,
                                          const TypeInfo*         pLegacyTypeInfo = nullptr,
                                          const SerializeContext& ctx             = SerializeContext::getDefault() );

        /** @brief POD 는 memcpy 로, 그 외는 바이너리 직렬화 · 역직렬화로 객체를 복제합니다. */
        static bool cloneObject( void* pDstData, const void* pSrcData, const TypeInfo& typeInfo );

        // ------------------------------------------------------------------------------
        // 4) 압축 바이너리 직렬화 (CompressionStream 통합)
        // ------------------------------------------------------------------------------
        /** @brief 객체를 바이너리로 직렬화한 뒤 지정된 코덱으로 압축합니다. */
        static bool serializeCompressed( const void*             pInstance,
                                         const TypeInfo&         typeInfo,
                                         vector<uint8>&          outListBuffer,
                                         CompressionCodecType    codecType = CompressionCodecType::RLE,
                                         const SerializeContext& ctx       = SerializeContext::getDefault() );

        /** @brief Archive 에 압축 바이너리로 직렬화합니다. */
        static bool serializeCompressed( const void*             pInstance,
                                         const TypeInfo&         typeInfo,
                                         Archive&                outArchive,
                                         CompressionCodecType    codecType = CompressionCodecType::RLE,
                                         const SerializeContext& ctx       = SerializeContext::getDefault() );

        /** @brief 압축된 바이너리 스트림을 풀어 객체로 역직렬화합니다. */
        static bool deserializeCompressed( void*                   pInstance,
                                           const TypeInfo&         typeInfo,
                                           const uint8*            pData,
                                           size_t                  dataSize,
                                           const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief Archive 에서 압축 바이너리를 역직렬화합니다. */
        static bool deserializeCompressed( void*                   pInstance,
                                           const TypeInfo&         typeInfo,
                                           Archive&                inArchive,
                                           const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 버전 헤더를 붙인 압축 바이너리로 직렬화합니다. */
        static bool serializeVersionedCompressed( uint32                  version,
                                                  const void*             pInstance,
                                                  const TypeInfo&         typeInfo,
                                                  vector<uint8>&          outListBuffer,
                                                  CompressionCodecType    codecType = CompressionCodecType::RLE,
                                                  const SerializeContext& ctx       = SerializeContext::getDefault() );

        /** @brief 압축된 버전 바이너리 스트림을 풀고 필요하면 이관하며 역직렬화합니다. */
        static bool deserializeVersionedCompressed( uint32&                 outVersion,
                                                    void*                   pInstance,
                                                    const TypeInfo&         typeInfo,
                                                    const uint8*            pData,
                                                    size_t                  dataSize,
                                                    uint32                  currentVersion  = 0,
                                                    SchemaMigrateFn         migrate         = nullptr,
                                                    const TypeInfo*         pLegacyTypeInfo = nullptr,
                                                    const SerializeContext& ctx             = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 5) 적응형 컴팩트 바이너리 직렬화 (Presence Bitmask & Sparse VarUInt Index)
        // ------------------------------------------------------------------------------
        /**
         * @brief 적응형 컴팩트 바이너리로 직렬화합니다(Dense: 비트마스크, Sparse: VarUInt 인덱스).
         * @details 프로퍼티마다 12바이트 태그 헤더(이름 해시 · 타입 해시 · 크기)를 쓰는 대신, 어느 프로퍼티가 있는지를
         *          비트마스크(프로퍼티 8개당 1바이트)나 VarUInt 인덱스로 적고 크기도 VarUInt 로 적습니다.
         *          대신 스트림은 이름이 아니라 **프로퍼티 순서(인덱스)** 로 짝을 맞춥니다. 뒤에 덧붙인 프로퍼티는
         *          괜찮지만, 중간에 넣거나 순서를 바꾼 스키마로 읽으면 엉뚱한 필드에 들어갑니다.
         */
        static void serializeCompact( const void*             pInstance,
                                      const TypeInfo&         typeInfo,
                                      vector<uint8>&          outBuffer,
                                      const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief Archive 에 적응형 컴팩트 바이너리로 직렬화합니다. */
        static void serializeCompact( const void*             pInstance,
                                      const TypeInfo&         typeInfo,
                                      Archive&                outArchive,
                                      const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 적응형 컴팩트 바이너리 버퍼에서 객체를 역직렬화합니다. */
        static bool deserializeCompact( void*                   pInstance,
                                        const TypeInfo&         typeInfo,
                                        const uint8*            pData,
                                        size_t                  dataSize,
                                        const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief Archive 에서 적응형 컴팩트 바이너리를 역직렬화합니다. */
        static bool deserializeCompact( void*                   pInstance,
                                        const TypeInfo&         typeInfo,
                                        Archive&                inArchive,
                                        const SerializeContext& ctx = SerializeContext::getDefault() );
    };

} // namespace sw
