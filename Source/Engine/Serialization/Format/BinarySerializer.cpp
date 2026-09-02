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
            static constexpr size_t kFastPropBitmaskThreshold = 64;

            static bool deserializeUntransacted( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize, const SerializeContext& ctx )
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
                        if ( ctx.allowUnknownProperties() )
                        {
                            reader.skip( payloadSize );
                            continue;
                        }
                        return false;
                    }

                    const PropertyInfo& prop = *pTargetProp;
                    if ( numProps <= kFastPropBitmaskThreshold )
                        seenBitmask |= ( 1ULL << matchedIndex );
                    else
                        uniqueSeenPropHashes.insert( prop.getNameHash() );

                    void* pPropPtr = prop.getRawPtr( pInstance );

                    if ( wireTypeHash != 0 && wireTypeHash != prop._typeName.getHash() )
                    {
                        if ( tryCoerceBinaryPayload( pPropPtr, prop._typeName, pData + payloadStart, payloadSize, ctx ) == false )
                            return false;
                        reader.skip( payloadSize );
                        continue;
                    }

                    if ( prop._bIsBitField == SW_TRUE )
                    {
                        bool   bVal  = false;
                        size_t local = payloadStart;
                        if ( SerializerUtil::deserializeValueBinary( &bVal, hashed_string( "bool" ), pData, payloadStart + payloadSize, local, ctx ) == false )
                            return false;
                        prop.setValue<bool>( pInstance, bVal );
                    }
                    else if ( prop._bIsContainer && prop.hasContainerWrapper() )
                    {
                        size_t local = payloadStart;
                        if ( SerializerUtil::deserializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), pData, payloadStart + payloadSize, local, ctx ) == false )
                            return false;
                        if ( local != payloadStart + payloadSize )
                            return false;
                    }
                    else
                    {
                        size_t local = payloadStart;
                        if ( SerializerUtil::deserializeValueBinary( pPropPtr, prop._typeName, pData, payloadStart + payloadSize, local, ctx ) == false )
                            return false;
                        if ( local != payloadStart + payloadSize )
                            return false;
                    }

                    reader.skip( payloadSize );
                }

                for ( size_t propIdx = 0; propIdx < numProps; ++propIdx )
                {
                    if ( numProps <= kFastPropBitmaskThreshold )
                    {
                        if ( ( seenBitmask & ( 1ULL << propIdx ) ) == 0 )
                            SerializerUtil::applyPropertyDefault( listProp[propIdx].getRawPtr( pInstance ), listProp[propIdx], ctx );
                    }
                    else
                    {
                        if ( uniqueSeenPropHashes.find( listProp[propIdx].getNameHash() ) == uniqueSeenPropHashes.end() )
                            SerializerUtil::applyPropertyDefault( listProp[propIdx].getRawPtr( pInstance ), listProp[propIdx], ctx );
                    }
                }
                return true;
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
        outListBuffer.reserve( outListBuffer.size() + sizeof( uint32 ) + propCount * 32 );
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
        return BinarySerializerInternal::deserializeUntransacted( pInstance, typeInfo, pData, dataSize, ctx );
    }

    bool BinarySerializer::deserializeSoft( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                            vector<SchemaOrphanValue>* pOutOrphans, const SerializeContext& ctx )
    {
        if ( pInstance == nullptr || pData == nullptr || dataSize == 0 )
            return false;

        BinaryStreamReader reader( pData, dataSize );
        uint32             propCount{ 0 };
        if ( reader.read( propCount ) == false )
            return false;

        const vector<PropertyInfo>& listProp = typeInfo.getPropertiesWithBase();
        const size_t                numProps = listProp.size();
        uint64                      seenBitmask{ 0 };
        unordered_set<uint32>       uniqueSeenPropHashes;
        if ( numProps > 64 )
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
                BinarySerializerInternal::pushOrphanVal( pOutOrphans, {}, tagHash, wireTypeHash, pData + payloadStart, payloadSize );
                reader.skip( payloadSize );
                continue;
            }

            const PropertyInfo& prop = *pTargetProp;
            if ( numProps <= 64 )
                seenBitmask |= ( 1ULL << matchedIndex );
            else
                uniqueSeenPropHashes.insert( prop.getNameHash() );

            void*  pPropPtr = prop.getRawPtr( pInstance );
            bool   applied{ false };
            size_t local = payloadStart;

            if ( prop._bIsBitField == SW_TRUE )
            {
                bool bVal = false;
                applied   = SerializerUtil::deserializeValueBinary( &bVal, hashed_string( "bool" ), pData, payloadStart + payloadSize, local, ctx );
                if ( applied )
                    prop.setValue<bool>( pInstance, bVal );
            }
            else if ( prop._bIsContainer && prop.hasContainerWrapper() )
                applied = SerializerUtil::deserializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), pData, payloadStart + payloadSize, local, ctx );
            else
            {
                applied = SerializerUtil::deserializeValueBinary( pPropPtr, prop._typeName, pData, payloadStart + payloadSize, local, ctx );
                if ( applied == false )
                    applied = tryCoerceBinaryPayload( pPropPtr, prop._typeName, pData + payloadStart, payloadSize, ctx );
            }

            if ( applied == false )
                BinarySerializerInternal::pushOrphanVal( pOutOrphans, prop._name, tagHash, wireTypeHash, pData + payloadStart, payloadSize );

            reader.skip( payloadSize );
        }

        for ( size_t propIdx = 0; propIdx < numProps; ++propIdx )
        {
            if ( numProps <= 64 )
            {
                if ( ( seenBitmask & ( 1ULL << propIdx ) ) != 0 )
                    continue;
            }
            else
            {
                if ( uniqueSeenPropHashes.find( listProp[propIdx].getNameHash() ) != uniqueSeenPropHashes.end() )
                    continue;
            }
            SerializerUtil::applyPropertyDefault( listProp[propIdx].getValuePtr<void>( pInstance ), listProp[propIdx], ctx );
        }

        return true;
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

        vector<SchemaOrphanValue> listOrphan;
        ScopedScratchInstance     scratchLegacy( pLegacyTypeInfo );
        void*                     pLegacyPtr = scratchLegacy.get();

        if ( pLegacyTypeInfo != nullptr && pLegacyTypeInfo->_size > 0 )
        {
            if ( pLegacyPtr == nullptr ||
                 deserializeSoft( pLegacyPtr, *pLegacyTypeInfo, pBody, bodySize, &listOrphan, ctx ) == false )
                return false;
        }

        if ( deserializeSoft( pInstance, typeInfo, pBody, bodySize, &listOrphan, ctx ) == false )
            return false;

        return runSchemaMigrateStep( outVersion, currentVersion, pInstance, typeInfo, pLegacyPtr, pLegacyTypeInfo,
                                     listOrphan, migrate, outVersion != currentVersion || listOrphan.empty() == false, ctx );
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
            writer.writeVarUInt( static_cast<uint64>( totalProps ) );

            const size_t               bitmaskBytes = PresenceMaskUtil::calculateBitmaskBytes( totalProps );
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
                writer.writeVarUInt( static_cast<uint64>( rec._size ) );
                if ( rec._size > 0 )
                {
                    writer.writeRawBytes( t_scratchPayload.data() + rec._offset, rec._size );
                }
            }
        }
        else
        {
            writer.write( PresenceMaskUtil::kModeSparse );
            writer.writeVarUInt( static_cast<uint64>( modCount ) );

            for ( const auto& rec : t_listRecord )
            {
                writer.writeVarUInt( static_cast<uint64>( rec._index ) );
                writer.writeVarUInt( static_cast<uint64>( rec._size ) );
                if ( rec._size > 0 )
                {
                    writer.writeRawBytes( t_scratchPayload.data() + rec._offset, rec._size );
                }
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
        const size_t                numProps = listProp.size();

        uint8 modeByte = 0;
        if ( reader.read( modeByte ) == false )
            return false;

        if ( modeByte == PresenceMaskUtil::kModeDense )
        {
            uint64 totalProps = 0;
            if ( reader.readVarUInt( totalProps ) == false )
                return false;

            const size_t  bitmaskBytes = PresenceMaskUtil::calculateBitmaskBytes( totalProps );
            vector<uint8> bitmask( bitmaskBytes, 0 );
            for ( size_t byteIndex = 0; byteIndex < bitmaskBytes; ++byteIndex )
            {
                if ( reader.read( bitmask[byteIndex] ) == false )
                    return false;
            }

            for ( uint32 propIndex = 0; propIndex < totalProps; ++propIndex )
            {
                const bool bPresent = PresenceMaskUtil::testBit( bitmask.data(), propIndex );
                if ( bPresent == false )
                    continue;

                uint64 payloadSize = 0;
                if ( reader.readVarUInt( payloadSize ) == false )
                    return false;

                const size_t payloadStart = reader.getOffset();
                if ( payloadStart + payloadSize > dataSize )
                    return false;

                if ( propIndex < numProps )
                {
                    const PropertyInfo& prop     = listProp[propIndex];
                    void*               pPropPtr = prop.getRawPtr( pInstance );

                    if ( prop._bIsBitField == SW_TRUE )
                    {
                        bool   bVal  = false;
                        size_t local = payloadStart;
                        if ( SerializerUtil::deserializeValueBinary( &bVal, hashed_string( "bool" ), pData, payloadStart + payloadSize, local, ctx ) == false )
                            return false;
                        prop.setValue<bool>( pInstance, bVal );
                    }
                    else if ( prop._bIsContainer && prop.hasContainerWrapper() )
                    {
                        size_t local = payloadStart;
                        if ( SerializerUtil::deserializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), pData, payloadStart + payloadSize, local, ctx ) == false )
                            return false;
                    }
                    else
                    {
                        size_t local = payloadStart;
                        if ( SerializerUtil::deserializeValueBinary( pPropPtr, prop._typeName, pData, payloadStart + payloadSize, local, ctx ) == false )
                            return false;
                    }
                }
                reader.skip( payloadSize );
            }
            return true;
        }
        else if ( modeByte == PresenceMaskUtil::kModeSparse )
        {
            uint64 modCount = 0;
            if ( reader.readVarUInt( modCount ) == false )
                return false;

            for ( uint64 modIndex = 0; modIndex < modCount; ++modIndex )
            {
                uint64 propIndex = 0;
                if ( reader.readVarUInt( propIndex ) == false )
                    return false;

                uint64 payloadSize = 0;
                if ( reader.readVarUInt( payloadSize ) == false )
                    return false;

                const size_t payloadStart = reader.getOffset();
                if ( payloadStart + payloadSize > dataSize )
                    return false;

                if ( propIndex < numProps )
                {
                    const PropertyInfo& prop     = listProp[static_cast<size_t>( propIndex )];
                    void*               pPropPtr = prop.getRawPtr( pInstance );

                    if ( prop._bIsBitField == SW_TRUE )
                    {
                        bool   bVal  = false;
                        size_t local = payloadStart;
                        if ( SerializerUtil::deserializeValueBinary( &bVal, hashed_string( "bool" ), pData, payloadStart + payloadSize, local, ctx ) == false )
                            return false;
                        prop.setValue<bool>( pInstance, bVal );
                    }
                    else if ( prop._bIsContainer && prop.hasContainerWrapper() )
                    {
                        size_t local = payloadStart;
                        if ( SerializerUtil::deserializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), pData, payloadStart + payloadSize, local, ctx ) == false )
                            return false;
                    }
                    else
                    {
                        size_t local = payloadStart;
                        if ( SerializerUtil::deserializeValueBinary( pPropPtr, prop._typeName, pData, payloadStart + payloadSize, local, ctx ) == false )
                            return false;
                    }
                }
                reader.skip( payloadSize );
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
