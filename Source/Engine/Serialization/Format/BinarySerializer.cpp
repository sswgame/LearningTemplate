#include "pch.h"

#include "Engine/Serialization/Format/BinarySerializer.h"

#include "Core/Compression/CompressionStream.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/BinaryStream.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        struct BinarySerializerInternal
        {
            /**
             * @brief 프로퍼티 중복 검사를 **비트마스크로 할 수 있는 한계** — `uint64` 의 비트 수다.
             * @details 이보다 많으면 해시 집합으로 넘어간다. 엄격·소프트 두 역직렬화 경로가 **같은 값을
             *          써야 한다** — 한쪽만 바꾸면 그 경로만 다른 자료구조로 중복을 세게 되고, 프로퍼티가
             *          64개 언저리인 타입에서만 갈리는 재현하기 어려운 차이가 된다. 예전에는 소프트 쪽이
             *          리터럴 `64` 를 적고 있었다(값은 같아 증상은 없었다).
             */
            static constexpr size_t kFastPropBitmaskThreshold = 64;

            /**
             * @brief 프로퍼티 하나의 페이로드를 인스턴스에 씁니다. 실패하면 false.
             * @details 비트필드·컨테이너·그 외 값의 세 갈래를 가른다. 아카이브 경로 둘이 이 스무 줄을
             *          **각자** 갖고 있었다 — 갈래를 하나 더하면(예: 새 컨테이너 모양) 한쪽만 고치기 쉽고,
             *          그러면 **그 경로로 읽은 객체만 필드가 비는** 재현하기 어려운 차이가 된다.
             */
            static bool applyPropertyPayload( void* pInstance, const PropertyInfo& prop, const uint8* pData,
                                              size_t payloadStart, size_t payloadSize, const SerializeContext& ctx,
                                              bool bRequireExactConsume )
            {
                void* pPropPtr = prop.getRawPtr( pInstance );

                if ( prop._bIsBitField == SW_TRUE )
                {
                    // 비트필드는 주소를 가질 수 없어 값으로 읽고 setter 로 넣는다.
                    bool   bVal  = false;
                    size_t local = payloadStart;
                    if ( SerializerUtil::deserializeValueBinary( &bVal, hashed_string( "bool" ), pData, payloadStart + payloadSize, local, ctx ) == false )
                        return false;
                    prop.setValue<bool>( pInstance, bVal );
                    return true;
                }

                // `bRequireExactConsume` — 엄격 역직렬화는 페이로드를 한 바이트도 남기지 않고 읽었는지까지 본다(남으면 스트림이
                // 이 필드를 다른 모양으로 적은 것이다). 소프트·아카이브 경로는 읽힌 만큼만 믿는다(예전 동작 그대로).
                size_t local = payloadStart;
                bool   bRead = false;
                if ( prop._bIsContainer && prop.hasContainerWrapper() )
                    bRead = SerializerUtil::deserializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), pData, payloadStart + payloadSize, local, ctx );
                else
                    bRead = SerializerUtil::deserializeValueBinary( pPropPtr, prop._typeName, pData, payloadStart + payloadSize, local, ctx );
                if ( bRead == false )
                    return false;
                return bRequireExactConsume == false || local == payloadStart + payloadSize;
            }

            static void pushOrphanVal( vector<SchemaOrphanValue>* pOutListOrphan, hashed_string name, uint32 nameHash, uint32 wireTypeHash, const uint8* pPayload, uint32 payloadSize )
            {
                if ( pOutListOrphan == nullptr )
                    return;
                SchemaOrphanValue orphan;
                orphan._name         = name;
                orphan._nameHash     = nameHash != 0 ? nameHash : name.getHash();
                orphan._wireTypeHash = wireTypeHash;
                orphan._listBinary.assign( pPayload, pPayload + payloadSize );
                pOutListOrphan->push_back( std::move( orphan ) );
            }

            /**
             * @brief 태그 스트림(개수 · [태그 해시 · 전선 타입 해시 · 크기 · 페이로드]…)을 읽어 인스턴스에 쓰는 **하나의** 루프.
             * @details 엄격(`deserialize`)과 소프트(`deserializeSoft`)가 이 60여 줄을 **각자** 들고 있었고, 셋째 사본
             *          (`applyPropertyPayload` 의 세 갈래)까지 있었다. 둘이 다른 것은 정책 셋뿐이다 —
             *          (1) 모르는 프로퍼티: 엄격은 `allowsUnknownProperties` 면 건너뛰고 아니면 실패, 소프트는 orphan 으로 싣는다.
             *          (2) 못 읽은 프로퍼티: 엄격은 실패, 소프트는 orphan.
             *          (3) 페이로드를 끝까지 읽었는지: 엄격만 본다.
             *          전선 타입이 다르면 둘 다 이관(`tryCoerceBinaryPayload`)으로 간다 — 이관은 제 타입으로 끝까지 읽히는지부터
             *          보므로 소프트의 예전 순서(제 타입 읽기 → 이관)와 결과가 같다. 소프트는 이관도 안 되면 예전처럼 끝까지
             *          읽히지 않아도 읽힌 만큼은 받는다(레거시 관용은 남긴다).
             *          신뢰할 수 없는 스트림의 경계 검사가 이 안에 있다 — 사본이 하나라야 그 검사가 한쪽에서만 빠지는 일이 없다.
             */
            static bool deserializeTagged( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                           const SerializeContext& ctx, vector<SchemaOrphanValue>* pOutListOrphan, bool bStrict )
            {
                BinaryStreamReader reader( pData, dataSize );
                uint32             propCount{ 0 };
                if ( reader.read( propCount ) == false )
                    return false;

                const vector<PropertyInfo>& listProp = typeInfo.getPropertiesWithBase();
                const size_t                numProps = listProp.size();
                uint64                      seenBitmask{ 0 };
                unordered_set<uint32>       uniqueSeenPropHashes;
                if ( numProps > kFastPropBitmaskThreshold )
                    uniqueSeenPropHashes.reserve( numProps );

                for ( uint32 propIndex = 0; propIndex < propCount; ++propIndex )
                {
                    uint32 tagHash{ 0 };
                    uint32 wireTypeHash{ 0 };
                    uint32 payloadSize{ 0 };
                    if ( reader.read( tagHash ) == false || reader.read( wireTypeHash ) == false || reader.read( payloadSize ) == false )
                        return false;

                    const size_t payloadStart = reader.getOffset();
                    if ( payloadStart + payloadSize > dataSize )
                        return false;

                    const PropertyInfo* pTargetProp  = nullptr;
                    size_t              matchedIndex = 0;
                    for ( size_t propSearchIdx = 0; propSearchIdx < numProps; ++propSearchIdx )
                    {
                        if ( listProp[propSearchIdx].matchesNameHash( tagHash ) )
                        {
                            if ( listProp[propSearchIdx]._metadata._bTransient == SW_TRUE )
                                break;
                            pTargetProp  = &listProp[propSearchIdx];
                            matchedIndex = propSearchIdx;
                            break;
                        }
                    }

                    if ( pTargetProp == nullptr )
                    {
                        if ( bStrict && ctx.allowsUnknownProperties() == false )
                            return false;
                        if ( bStrict == false )
                            pushOrphanVal( pOutListOrphan, {}, tagHash, wireTypeHash, pData + payloadStart, payloadSize );
                        reader.skip( payloadSize );
                        continue;
                    }

                    const PropertyInfo& prop = *pTargetProp;
                    if ( numProps <= kFastPropBitmaskThreshold )
                        seenBitmask |= ( 1ULL << matchedIndex );
                    else
                        uniqueSeenPropHashes.insert( prop.getNameHash() );

                    // **전선 타입을 같이 넘긴다.** 태그가 그것을 들고 있는데 넘기지 않으면 POD -> string 이관이 크기로만
                    // 타입을 짐작한다(정수와 실수를 못 가른다).
                    const bool bWireMismatch = ( wireTypeHash != 0 && wireTypeHash != prop._typeName.getHash() );
                    bool       bApplied      = false;
                    if ( bWireMismatch )
                    {
                        void*               pPropPtr     = prop.getRawPtr( pInstance );
                        const hashed_string wireTypeName = engine::getTypeRegistry().canonicalTypeNameByHash( wireTypeHash );
                        bApplied                         = tryCoerceBinaryPayload( pPropPtr, prop._typeName, pData + payloadStart, payloadSize, ctx, wireTypeName );
                        if ( bApplied == false && bStrict == false )
                            bApplied = applyPropertyPayload( pInstance, prop, pData, payloadStart, payloadSize, ctx, false );
                    }
                    else
                    {
                        bApplied = applyPropertyPayload( pInstance, prop, pData, payloadStart, payloadSize, ctx, bStrict );
                        if ( bApplied == false && bStrict == false )
                        {
                            void*               pPropPtr     = prop.getRawPtr( pInstance );
                            const hashed_string wireTypeName = engine::getTypeRegistry().canonicalTypeNameByHash( wireTypeHash );
                            bApplied                         = tryCoerceBinaryPayload( pPropPtr, prop._typeName, pData + payloadStart, payloadSize, ctx, wireTypeName );
                        }
                    }

                    if ( bApplied == false )
                    {
                        if ( bStrict )
                            return false;
                        pushOrphanVal( pOutListOrphan, prop._name, tagHash, wireTypeHash, pData + payloadStart, payloadSize );
                    }

                    reader.skip( payloadSize );
                }

                // 스트림에 없던 프로퍼티는 기본값으로 — 두 경로가 같은 규칙이다.
                for ( size_t propIdx = 0; propIdx < numProps; ++propIdx )
                {
                    bool bSeen = false;
                    if ( numProps <= kFastPropBitmaskThreshold )
                        bSeen = ( seenBitmask & ( 1ULL << propIdx ) ) != 0;
                    else
                        bSeen = uniqueSeenPropHashes.find( listProp[propIdx].getNameHash() ) != uniqueSeenPropHashes.end();
                    if ( bSeen == false )
                        SerializerUtil::applyPropertyDefault( listProp[propIdx].getRawPtr( pInstance ), listProp[propIdx], ctx );
                }
                return true;
            }

            /**
             * @brief 프로퍼티 하나의 페이로드를 읽어 인스턴스에 쓰고, 스트림을 그 뒤로 넘깁니다.
             * @details 컴팩트 스트림의 두 모드(비트마스크 · 희소)가 이 열두 줄을 **각자** 갖고 있었다.
             *          다른 것은 `propIndex` 를 어디서 얻는가 뿐이다(비트 검사 vs varint 읽기).
             *          그런데 이 안에는 **신뢰할 수 없는 스트림에 대한 경계 검사**가 들어 있다 —
             *          한쪽이 그것을 잃으면 손상된 파일 하나로 버퍼 밖을 읽는다. 저장소에서 가장
             *          위험한 파싱 코드를 두 벌로 두지 않는다.
             */
            static bool readAndApplyProperty( void* pInstance, uint64 propIndex, const vector<PropertyInfo>& listProp,
                                              BinaryStreamReader& reader, const uint8* pData, size_t dataSize,
                                              const SerializeContext& ctx )
            {
                uint64 payloadSize = 0;
                if ( reader.readVarUint( payloadSize ) == false )
                    return false;

                const size_t payloadStart = reader.getOffset();
                // 뺄셈으로 비교한다 — 스트림에서 읽은 크기가 크면 덧셈이 넘친다.
                if ( payloadSize > dataSize - payloadStart )
                    return false;

                // 프로퍼티가 더 많은 새 스키마로 쓴 스트림도 읽는다 — 모르는 인덱스는 건너뛴다.
                if ( propIndex < listProp.size() )
                {
                    if ( applyPropertyPayload( pInstance, listProp[static_cast<size_t>( propIndex )],
                                               pData, payloadStart, payloadSize, ctx, false ) == false )
                        return false;
                }

                reader.skip( payloadSize );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "BinarySerializer" );

    void BinarySerializer::serialize( const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                                      const SerializeContext& ctx )
    {
        BinaryStreamWriter          writer( outListBuffer );
        const vector<PropertyInfo>& listProp = typeInfo.getPropertiesWithBase();
        uint32                      propCount{ 0 };
        for ( const PropertyInfo& prop : listProp )
        {
            if ( prop._metadata._bTransient == SW_TRUE )
                continue;
            ++propCount;
        }
        outListBuffer.reserve( outListBuffer.size() + sizeof( uint32 ) + static_cast<size_t>( propCount ) * 32 );
        writer.write( propCount );

        for ( const PropertyInfo& prop : listProp )
        {
            if ( prop._metadata._bTransient == SW_TRUE )
                continue;
            const void* pPropPtr = prop.getRawPtr( pInstance );

            uint32 hashVal = prop.getNameHash();
            writer.write( hashVal );

            uint32 typeHash = prop._typeName.getHash();
            writer.write( typeHash );

            size_t sizeHeaderPos = writer.getOffset();
            uint32 dummySize{ 0 };
            writer.write( dummySize );

            size_t payloadStart = writer.getOffset();

            if ( prop._bIsBitField == SW_TRUE )
            {
                const bool bVal = prop.getValue<bool>( pInstance );
                SerializerUtil::serializeValueBinary( &bVal, hashed_string( "bool" ), outListBuffer, ctx );
            }
            else if ( prop._bIsContainer && prop.hasContainerWrapper() )
                SerializerUtil::serializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), outListBuffer, ctx );
            else
                SerializerUtil::serializeValueBinary( pPropPtr, prop._typeName, outListBuffer, ctx );

            uint32 payloadSize = static_cast<uint32>( writer.getOffset() - payloadStart );
            writer.writeAt( sizeHeaderPos, payloadSize );
        }
    }

    bool BinarySerializer::cloneObject( void* pDstData, const void* pSrcData, const TypeInfo& typeInfo )
    {
        if ( pDstData == nullptr || pSrcData == nullptr )
            return false;

        if ( typeInfo._size > 0 && typeInfo.usesPodCopyFastPath() )
        {
            Memory::copy( pDstData, pSrcData, typeInfo._size );
            return true;
        }

        thread_local vector<uint8> t_listCloneBuffer;
        t_listCloneBuffer.clear();
        const size_t targetCapacity = typeInfo._size > 0 ? typeInfo._size * 2 : 256;
        if ( t_listCloneBuffer.capacity() < targetCapacity )
            t_listCloneBuffer.reserve( targetCapacity );

        SerializeContext ctx;
        serialize( pSrcData, typeInfo, t_listCloneBuffer, ctx );
        return deserialize( pDstData, typeInfo, t_listCloneBuffer.data(), t_listCloneBuffer.size(), ctx );
    }

    bool BinarySerializer::deserialize( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                        const SerializeContext& ctx )
    {
        return BinarySerializerInternal::deserializeTagged( pInstance, typeInfo, pData, dataSize, ctx, nullptr, true );
    }

    bool BinarySerializer::deserializeSoft( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                            vector<SchemaOrphanValue>* pOutOrphans, const SerializeContext& ctx )
    {
        if ( pInstance == nullptr || pData == nullptr || dataSize == 0 )
            return false;
        return BinarySerializerInternal::deserializeTagged( pInstance, typeInfo, pData, dataSize, ctx, pOutOrphans, false );
    }

    void BinarySerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                                               const SerializeContext& ctx )
    {
        BinaryStreamWriter writer( outListBuffer );
        writer.write( version );
        serialize( pInstance, typeInfo, outListBuffer, ctx );
    }

    bool BinarySerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                                 uint32 currentVersion, SchemaMigrateFn migrate, const TypeInfo* pLegacyTypeInfo,
                                                 const SerializeContext& ctx )
    {
        BinaryStreamReader reader( pData, dataSize );
        if ( reader.read( outVersion ) == false )
            return false;

        const uint8* pBody    = pData + reader.getOffset();
        const size_t bodySize = dataSize - reader.getOffset();

        // 절차는 JSON·XML 과 공통이다(`runVersionedDeserialize`). 바이너리만 다른 것은 두 가지 —
        // 버전을 **본문 앞에서 이미 읽었고**(위 `reader.read`), orphan 이 하나라도 있으면 migrate 없이는
        // 거절한다. 바이너리는 손으로 고치는 포맷이 아니라, 모르는 필드가 있다는 것은 스키마가
        // 바뀌었다는 뜻이기 때문이다.
        return runVersionedDeserialize(
            outVersion, pInstance, typeInfo, currentVersion, migrate, pLegacyTypeInfo, ctx,
            SchemaVersionSource::Stream, SchemaOrphanPolicy::Reject,
            SW_DELEGATE_LAMBDA( SoftDeserializeFn,
                                [&]( void* pTarget, const TypeInfo& targetType, vector<SchemaOrphanValue>& listOrphan, uint32& ) -> bool
        {
            return deserializeSoft( pTarget, targetType, pBody, bodySize, &listOrphan, ctx );
        } ) );
    }

    bool BinarySerializer::serializeCompressed( const void*             pInstance,
                                                const TypeInfo&         typeInfo,
                                                vector<uint8>&          outListBuffer,
                                                CompressionCodecType    codecType,
                                                const SerializeContext& ctx )
    {
        outListBuffer.clear();
        if ( pInstance == nullptr )
            return false;

        vector<uint8> listRawBinary;
        BinarySerializer::serialize( pInstance, typeInfo, listRawBinary, ctx );
        if ( listRawBinary.empty() )
            return false;

        return CompressionStream::compressBuffer( listRawBinary.data(), listRawBinary.size(), outListBuffer, codecType );
    }

    bool BinarySerializer::deserializeCompressed( void*                   pInstance,
                                                  const TypeInfo&         typeInfo,
                                                  const uint8*            pData,
                                                  size_t                  dataSize,
                                                  const SerializeContext& ctx )
    {
        if ( pInstance == nullptr || pData == nullptr || dataSize == 0 )
            return false;

        vector<uint8> listRawBinary;
        const bool    bDecompressOk = CompressionStream::decompressBuffer( pData, dataSize, listRawBinary );
        if ( bDecompressOk == false || listRawBinary.empty() )
            return false;

        return BinarySerializer::deserialize( pInstance, typeInfo, listRawBinary.data(), listRawBinary.size(), ctx );
    }

    bool BinarySerializer::serializeVersionedCompressed( uint32                  version,
                                                         const void*             pInstance,
                                                         const TypeInfo&         typeInfo,
                                                         vector<uint8>&          outListBuffer,
                                                         CompressionCodecType    codecType,
                                                         const SerializeContext& ctx )
    {
        outListBuffer.clear();
        if ( pInstance == nullptr )
            return false;

        vector<uint8> listRawBinary;
        BinarySerializer::serializeVersioned( version, pInstance, typeInfo, listRawBinary, ctx );
        if ( listRawBinary.empty() )
            return false;

        return CompressionStream::compressBuffer( listRawBinary.data(), listRawBinary.size(), outListBuffer, codecType );
    }

    bool BinarySerializer::deserializeVersionedCompressed( uint32&                 outVersion,
                                                           void*                   pInstance,
                                                           const TypeInfo&         typeInfo,
                                                           const uint8*            pData,
                                                           size_t                  dataSize,
                                                           uint32                  currentVersion,
                                                           SchemaMigrateFn         migrate,
                                                           const TypeInfo*         pLegacyTypeInfo,
                                                           const SerializeContext& ctx )
    {
        if ( pInstance == nullptr || pData == nullptr || dataSize == 0 )
            return false;

        vector<uint8> listRawBinary;
        const bool    bDecompressOk = CompressionStream::decompressBuffer( pData, dataSize, listRawBinary );
        if ( bDecompressOk == false || listRawBinary.empty() )
            return false;

        return BinarySerializer::deserializeVersioned( outVersion, pInstance, typeInfo, listRawBinary.data(), listRawBinary.size(),
                                                       currentVersion, migrate, pLegacyTypeInfo, ctx );
    }

    void BinarySerializer::serialize( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                      const SerializeContext& ctx )
    {
        vector<uint8> buffer;
        serialize( pInstance, typeInfo, buffer, ctx );
        if ( buffer.empty() == false )
            outArchive.writeBytes( buffer.data(), buffer.size() );
    }

    bool BinarySerializer::deserialize( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                        const SerializeContext& ctx )
    {
        if ( inArchive.isError() || inArchive.getData() == nullptr || inArchive.getOffset() >= inArchive.getSize() )
            return false;

        const uint8* pData    = inArchive.getData() + inArchive.getOffset();
        const size_t dataSize = inArchive.getSize() - inArchive.getOffset();
        return deserialize( pInstance, typeInfo, pData, dataSize, ctx );
    }

    void BinarySerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                               const SerializeContext& ctx )
    {
        vector<uint8> buffer;
        serializeVersioned( version, pInstance, typeInfo, buffer, ctx );
        if ( buffer.empty() == false )
            outArchive.writeBytes( buffer.data(), buffer.size() );
    }

    bool BinarySerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                                 uint32 currentVersion, SchemaMigrateFn migrate,
                                                 const TypeInfo* pLegacyTypeInfo, const SerializeContext& ctx )
    {
        if ( inArchive.isError() || inArchive.getData() == nullptr || inArchive.getOffset() >= inArchive.getSize() )
            return false;

        const uint8* pData    = inArchive.getData() + inArchive.getOffset();
        const size_t dataSize = inArchive.getSize() - inArchive.getOffset();
        return deserializeVersioned( outVersion, pInstance, typeInfo, pData, dataSize, currentVersion, migrate, pLegacyTypeInfo, ctx );
    }

    bool BinarySerializer::serializeCompressed( const void*             pInstance,
                                                const TypeInfo&         typeInfo,
                                                Archive&                outArchive,
                                                CompressionCodecType    codecType,
                                                const SerializeContext& ctx )
    {
        vector<uint8> buffer;
        if ( serializeCompressed( pInstance, typeInfo, buffer, codecType, ctx ) == false )
            return false;

        if ( buffer.empty() == false )
            outArchive.writeBytes( buffer.data(), buffer.size() );
        return true;
    }

    bool BinarySerializer::deserializeCompressed( void*                   pInstance,
                                                  const TypeInfo&         typeInfo,
                                                  Archive&                inArchive,
                                                  const SerializeContext& ctx )
    {
        if ( inArchive.isError() || inArchive.getData() == nullptr || inArchive.getOffset() >= inArchive.getSize() )
            return false;

        const uint8* pData    = inArchive.getData() + inArchive.getOffset();
        const size_t dataSize = inArchive.getSize() - inArchive.getOffset();
        return deserializeCompressed( pInstance, typeInfo, pData, dataSize, ctx );
    }

    void BinarySerializer::serializeCompact( const void*             pInstance,
                                             const TypeInfo&         typeInfo,
                                             vector<uint8>&          outBuffer,
                                             const SerializeContext& ctx )
    {
        if ( pInstance == nullptr )
            return;

        BinaryStreamWriter          writer( outBuffer );
        const vector<PropertyInfo>& listProp   = typeInfo.getPropertiesWithBase();
        const size_t                totalProps = listProp.size();

        struct FlatPropRecord
        {
            uint32 _index;
            uint32 _offset;
            uint32 _size;
        };

        thread_local vector<FlatPropRecord> t_listRecord;
        thread_local vector<uint8>          t_scratchPayload;
        t_listRecord.clear();
        t_scratchPayload.clear();

        for ( size_t propIndex = 0; propIndex < totalProps; ++propIndex )
        {
            const PropertyInfo& prop = listProp[propIndex];
            if ( SerializerUtil::shouldSerializeProperty( prop ) == false )
                continue;

            const void*  pPropPtr     = prop.getRawPtr( pInstance );
            const size_t payloadStart = t_scratchPayload.size();

            if ( prop._bIsBitField == SW_TRUE )
            {
                const bool bVal = prop.getValue<bool>( pInstance );
                SerializerUtil::serializeValueBinary( &bVal, hashed_string( "bool" ), t_scratchPayload, ctx );
            }
            else if ( prop._bIsContainer && prop.hasContainerWrapper() )
            {
                SerializerUtil::serializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), t_scratchPayload, ctx );
            }
            else
            {
                SerializerUtil::serializeValueBinary( pPropPtr, prop._typeName, t_scratchPayload, ctx );
            }

            const uint32 payloadSize = static_cast<uint32>( t_scratchPayload.size() - payloadStart );
            t_listRecord.push_back( { static_cast<uint32>( propIndex ), static_cast<uint32>( payloadStart ), payloadSize } );
        }

        const size_t modCount  = t_listRecord.size();
        const bool   bUseDense = PresenceMaskUtil::shouldUseDenseMode( modCount, totalProps );

        if ( bUseDense )
        {
            writer.write( PresenceMaskUtil::kModeDense );
            writer.writeVarUint( static_cast<uint64>( totalProps ) );

            const size_t               bitmaskBytes = PresenceMaskUtil::computeBitmaskBytes( totalProps );
            thread_local vector<uint8> t_bitmask;
            t_bitmask.assign( bitmaskBytes, 0 );

            for ( const auto& rec : t_listRecord )
            {
                PresenceMaskUtil::setBit( t_bitmask.data(), rec._index );
            }
            for ( size_t byteIndex = 0; byteIndex < bitmaskBytes; ++byteIndex )
            {
                writer.write( t_bitmask[byteIndex] );
            }
            for ( const auto& rec : t_listRecord )
            {
                writer.writeVarUint( static_cast<uint64>( rec._size ) );
                if ( rec._size > 0 )
                    writer.writeRawBytes( t_scratchPayload.data() + rec._offset, rec._size );
            }
        }
        else
        {
            writer.write( PresenceMaskUtil::kModeSparse );
            writer.writeVarUint( static_cast<uint64>( modCount ) );

            for ( const auto& rec : t_listRecord )
            {
                writer.writeVarUint( static_cast<uint64>( rec._index ) );
                writer.writeVarUint( static_cast<uint64>( rec._size ) );
                if ( rec._size > 0 )
                    writer.writeRawBytes( t_scratchPayload.data() + rec._offset, rec._size );
            }
        }
    }

    void BinarySerializer::serializeCompact( const void*             pInstance,
                                             const TypeInfo&         typeInfo,
                                             Archive&                outArchive,
                                             const SerializeContext& ctx )
    {
        vector<uint8> buffer;
        serializeCompact( pInstance, typeInfo, buffer, ctx );
        if ( buffer.empty() == false )
            outArchive.writeBytes( buffer.data(), buffer.size() );
    }

    bool BinarySerializer::deserializeCompact( void*                   pInstance,
                                               const TypeInfo&         typeInfo,
                                               const uint8*            pData,
                                               size_t                  dataSize,
                                               const SerializeContext& ctx )
    {
        if ( pInstance == nullptr || pData == nullptr || dataSize == 0 )
            return false;

        BinaryStreamReader          reader( pData, dataSize );
        const vector<PropertyInfo>& listProp = typeInfo.getPropertiesWithBase();

        uint8 modeByte = 0;
        if ( reader.read( modeByte ) == false )
            return false;

        if ( modeByte == PresenceMaskUtil::kModeDense )
        {
            uint64 totalProps = 0;
            if ( reader.readVarUint( totalProps ) == false )
                return false;

            // **스트림이 말하는 프로퍼티 수를 그대로 믿지 않는다.** 검사 없이 쓰면 셋이 한꺼번에 깨진다:
            //  ① `(totalProps + 7) / 8` 이 거대한 할당이 되고, uint64 끝자락에서는 덧셈이 넘쳐
            //     **0 바이트** 마스크가 나온다.
            //  ② 그 0 바이트 마스크를 `testBit` 이 그대로 읽는다 — 첫 바퀴에 버퍼 밖이다.
            //  ③ 순회 변수가 `uint32` 였다 — 4,294,967,295 를 넘으면 되감겨 **끝나지 않았다.**
            // 비트마스크는 프로퍼티 여덟 개당 한 바이트이므로, 남은 바이트로 마스크조차 채울 수
            // 없는 수는 어떤 스키마에서도 거짓이다(더 많은 프로퍼티를 가진 새 스키마는 허용된다).
            const uint64 remainingBytes = static_cast<uint64>( dataSize - reader.getOffset() );
            if ( totalProps > remainingBytes * 8 )
                return false;

            const size_t  bitmaskBytes = PresenceMaskUtil::computeBitmaskBytes( static_cast<size_t>( totalProps ) );
            vector<uint8> bitmask( bitmaskBytes, 0 );
            for ( size_t byteIndex = 0; byteIndex < bitmaskBytes; ++byteIndex )
            {
                if ( reader.read( bitmask[byteIndex] ) == false )
                    return false;
            }

            for ( uint64 propIndex = 0; propIndex < totalProps; ++propIndex )
            {
                const bool bPresent = PresenceMaskUtil::testBit( bitmask.data(), static_cast<size_t>( propIndex ) );
                if ( bPresent == false )
                    continue;

                if ( BinarySerializerInternal::readAndApplyProperty( pInstance, propIndex, listProp, reader, pData, dataSize, ctx ) == false )
                    return false;
            }
            return true;
        }
        else if ( modeByte == PresenceMaskUtil::kModeSparse )
        {
            uint64 modCount = 0;
            if ( reader.readVarUint( modCount ) == false )
                return false;

            for ( uint64 modIndex = 0; modIndex < modCount; ++modIndex )
            {
                uint64 propIndex = 0;
                if ( reader.readVarUint( propIndex ) == false )
                    return false;

                if ( BinarySerializerInternal::readAndApplyProperty( pInstance, propIndex, listProp, reader, pData, dataSize, ctx ) == false )
                    return false;
            }
            return true;
        }

        return false;
    }

    bool BinarySerializer::deserializeCompact( void*                   pInstance,
                                               const TypeInfo&         typeInfo,
                                               Archive&                inArchive,
                                               const SerializeContext& ctx )
    {
        if ( inArchive.isError() || inArchive.getData() == nullptr || inArchive.getOffset() >= inArchive.getSize() )
            return false;

        const uint8* pData    = inArchive.getData() + inArchive.getOffset();
        const size_t dataSize = inArchive.getSize() - inArchive.getOffset();
        return deserializeCompact( pInstance, typeInfo, pData, dataSize, ctx );
    }
} // namespace sw
