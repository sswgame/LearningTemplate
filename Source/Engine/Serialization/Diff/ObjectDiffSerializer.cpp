#include "pch.h"

#include "Engine/Serialization/Diff/ObjectDiffSerializer.h"

#include "Engine/Reflection/Reflection.h"
#include "Engine/Serialization/Serializer/ContainerVisitor.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ObjectDiff" );

    bool ObjectDiffSerializer::serializeDiff( vector<uint8>& outDiffBytes, const void* pCdoInstance, const void* pModifiedInstance,
                                              const TypeInfo& typeInfo, const SerializeContext& context )
    {
        if ( pCdoInstance == nullptr || pModifiedInstance == nullptr )
            return false;

        outDiffBytes.clear();
        vector<uint8> cdoBytes;
        vector<uint8> modifiedBytes;
        bool          bAllWritten = true;

        typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
        {
            if ( SerializerUtil::shouldSerializeProperty( typeInfo, prop, context ) == false || bAllWritten == false )
                return;
            const void* pCdoPtr        = prop.getRawPtr( pCdoInstance );
            const void* pModifiedValue = prop.getRawPtr( pModifiedInstance );
            if ( prop._bIsBitField == SW_FALSE && ( pCdoPtr == nullptr || pModifiedValue == nullptr ) )
                return;

            cdoBytes.clear();
            modifiedBytes.clear();
            bool bWritten = false;
            if ( prop._bIsBitField == SW_TRUE )
            {
                const bool bCdo      = prop.getValue<bool>( pCdoInstance );
                const bool bModified = prop.getValue<bool>( pModifiedInstance );
                bWritten             = SerializerUtil::serializeValueBinary( ValueAddress{ &bCdo }, hashed_string( "bool" ), cdoBytes, context ) &&
                           SerializerUtil::serializeValueBinary( ValueAddress{ &bModified }, hashed_string( "bool" ), modifiedBytes, context );
            }
            else if ( prop._bIsContainer && prop.hasContainerWrapper() )
            {
                bWritten = SerializerUtil::serializeNestedContainerBinary( ValueAddress{ pCdoPtr }, prop.getContainerShape(), cdoBytes, context ) &&
                           SerializerUtil::serializeNestedContainerBinary( ValueAddress{ pModifiedValue }, prop.getContainerShape(), modifiedBytes, context );
            }
            else
            {
                bWritten = SerializerUtil::serializeValueBinary( ValueAddress{ pCdoPtr }, prop._typeName, cdoBytes, context ) &&
                           SerializerUtil::serializeValueBinary( ValueAddress{ pModifiedValue }, prop._typeName, modifiedBytes, context );
            }
            // 바이트로 옮기지 못한 값이 있으면 diff 전체가 실패다. 그 값만 빼면 받는 쪽이 바뀐 값을 놓친다.
            if ( bWritten == false )
            {
                SW_LOG_ERROR( "%#::%# (%#) cannot be written to a diff: it or a value inside it has no binary, enum, struct or text handler", typeInfo._name.c_str(),
                              prop._name.c_str(), prop._typeName.c_str() );
                bAllWritten = false;
                return;
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

        if ( bAllWritten == false )
            outDiffBytes.clear();
        return bAllWritten;
    }

    bool ObjectDiffSerializer::deserializeDiff( void* pTargetInstance, const TypeInfo& typeInfo, const uint8* pDiffData, size_t diffSize,
                                                const SerializeContext& context )
    {
        if ( pTargetInstance == nullptr || pDiffData == nullptr )
            return false;

        size_t offset{ 0 };
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

            const vector<PropertyInfo>& listProp     = typeInfo.getPropertiesWithBase();
            const size_t                matchedIndex = SerializerUtil::findPropertyIndexByNameHash( listProp, nameHash );
            const bool                  bMatched     = matchedIndex < listProp.size() && SerializerUtil::shouldSerializeProperty( typeInfo, listProp[matchedIndex], context );
            const PropertyInfo*         pProp        = bMatched ? &listProp[matchedIndex] : nullptr;
            if ( pProp != nullptr )
            {
                // 값 주소가 없거나 페이로드를 끝까지 읽지 못하면 diff 가 틀린 것이다. 건너뛰지 않고 실패한다(필드 실패와 객체 실패 모두 diff 적용의 실패다).
                if ( SerializerUtil::applyPropertyBinary( *pProp, MutableObjectAddress{ pTargetInstance }, pDiffData + offset, payload, context ) != FieldReadResult::Read )
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
