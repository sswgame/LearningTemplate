#include "pch.h"

#include "Engine/Serialization/Object/ObjectDiffSerializer.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SerializerUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ObjectDiff" );

    bool ObjectDiffSerializer::serializeDiff( vector<uint8>& outDiffBytes, const void* pCdoInstance, const void* pModifiedInstance,
                                              const TypeInfo& typeInfo )
    {
        if ( pCdoInstance == nullptr || pModifiedInstance == nullptr )
            return false;

        outDiffBytes.clear();
        const SerializeContext& ctx = SerializeContext::getDefault();
        vector<uint8>           cdoBytes;
        vector<uint8>           modifiedBytes;

        typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
        {
            if ( prop._metadata._bTransient == SW_TRUE )
                return;
            const void* pCdoPtr        = prop.getRawPtr( pCdoInstance );
            const void* pModifiedValue = prop.getRawPtr( pModifiedInstance );
            if ( prop._bIsBitField == SW_FALSE && ( pCdoPtr == nullptr || pModifiedValue == nullptr ) )
                return;

            cdoBytes.clear();
            modifiedBytes.clear();
            if ( prop._bIsBitField == SW_TRUE )
            {
                const bool bCdo      = prop.getValue<bool>( pCdoInstance );
                const bool bModified = prop.getValue<bool>( pModifiedInstance );
                SerializerUtil::serializeValueBinary( &bCdo, hashed_string( "bool" ), cdoBytes, ctx );
                SerializerUtil::serializeValueBinary( &bModified, hashed_string( "bool" ), modifiedBytes, ctx );
            }
            else if ( prop._bIsContainer && prop.hasContainerWrapper() )
            {
                SerializerUtil::serializeNestedContainerBinary( pCdoPtr, prop.getContainerShape(), cdoBytes, ctx );
                SerializerUtil::serializeNestedContainerBinary( pModifiedValue, prop.getContainerShape(), modifiedBytes, ctx );
            }
            else
            {
                SerializerUtil::serializeValueBinary( pCdoPtr, prop._typeName, cdoBytes, ctx );
                SerializerUtil::serializeValueBinary( pModifiedValue, prop._typeName, modifiedBytes, ctx );
            }
            if ( cdoBytes == modifiedBytes )
                return;

            const uint32 nameHash   = prop.getNameHash();
            const uint32 size       = static_cast<uint32>( modifiedBytes.size() );
            const uint8* pHashBytes = reinterpret_cast<const uint8*>( &nameHash );
            const uint8* pSizeBytes = reinterpret_cast<const uint8*>( &size );
            outDiffBytes.insert( outDiffBytes.end(), pHashBytes, pHashBytes + sizeof( uint32 ) );
            outDiffBytes.insert( outDiffBytes.end(), pSizeBytes, pSizeBytes + sizeof( uint32 ) );
            outDiffBytes.insert( outDiffBytes.end(), modifiedBytes.begin(), modifiedBytes.end() );
        }, true /* 상속 PROPERTY 포함 */ );

        return true;
    }

    bool ObjectDiffSerializer::deserializeDiff( void* pTargetInstance, const TypeInfo& typeInfo, const uint8* pDiffData, size_t diffSize )
    {
        if ( pTargetInstance == nullptr || pDiffData == nullptr )
            return false;

        const SerializeContext& ctx = SerializeContext::getDefault();
        size_t                  offset{ 0 };
        while ( offset + sizeof( uint32 ) * 2 <= diffSize )
        {
            uint32 nameHash{ 0 };
            uint32 payload{ 0 };
            Memory::copy( &nameHash, pDiffData + offset, sizeof( uint32 ) );
            offset += sizeof( uint32 );
            Memory::copy( &payload, pDiffData + offset, sizeof( uint32 ) );
            offset += sizeof( uint32 );
            if ( offset + payload > diffSize )
                return false;

            const PropertyInfo* pProp = nullptr;
            for ( const PropertyInfo& propInfo : typeInfo.getPropertiesWithBase() )
            {
                if ( propInfo.matchesNameHash( nameHash ) )
                {
                    if ( propInfo._metadata._bTransient == SW_TRUE )
                        break;
                    pProp = &propInfo;
                    break;
                }
            }
            if ( pProp != nullptr )
            {
                void*  pDest = pProp->getRawPtr( pTargetInstance );
                size_t local{ 0 };
                bool   bOk{ true };
                if ( pProp->_bIsBitField == SW_TRUE )
                {
                    bool bVal = false;
                    bOk       = SerializerUtil::deserializeValueBinary( &bVal, hashed_string( "bool" ), pDiffData + offset, payload, local, ctx );
                    if ( bOk )
                        pProp->setValue<bool>( pTargetInstance, bVal );
                }
                else if ( pDest != nullptr )
                {
                    if ( pProp->_bIsContainer && pProp->hasContainerWrapper() )
                    {
                        bOk = SerializerUtil::deserializeNestedContainerBinary( pDest, pProp->getContainerShape(), pDiffData + offset, payload,
                                                                                local, ctx );
                    }
                    else
                        bOk = SerializerUtil::deserializeValueBinary( pDest, pProp->_typeName, pDiffData + offset, payload, local, ctx );
                }
                if ( bOk == false )
                    return false;
            }
            else
            {
                SW_LOG_WARNING( "unknown property hash %#", nameHash );
                return false;
            }
            offset += payload;
        }
        return offset == diffSize;
    }

} // namespace sw
