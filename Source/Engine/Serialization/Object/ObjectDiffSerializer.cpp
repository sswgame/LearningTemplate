#include "pch.h"

#include "Engine/Serialization/Object/ObjectDiffSerializer.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SerializerInternal.h"

namespace sw
{
	bool ObjectDiffSerializer::serializeDiff( vector<uint8>& listOutDiffBuffer, const void* pCdoInstance, const void* pModifiedInstance, const TypeInfo& typeInfo )
	{
		if ( pCdoInstance == nullptr || pModifiedInstance == nullptr )
			return false;

		listOutDiffBuffer.clear();
		const SerializeContext& ctx = SerializeContext::getDefault();

		for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
		{
			const void* pCdoPtr = prop.getValuePtr<void>( pCdoInstance );
			const void* pModPtr = prop.getValuePtr<void>( pModifiedInstance );
			if ( pCdoPtr == nullptr || pModPtr == nullptr )
				continue;

			thread_local vector<uint8> t_listCdoBytes;
			thread_local vector<uint8> t_listModBytes;
			t_listCdoBytes.clear();
			t_listModBytes.clear();
			if ( prop._bIsContainer && prop.hasContainerWrapper() )
			{
				serializeNestedContainerBinary( pCdoPtr, prop.getContainerShape(), t_listCdoBytes, ctx );
				serializeNestedContainerBinary( pModPtr, prop.getContainerShape(), t_listModBytes, ctx );
			}
			else
			{
				serializeValueBinary( pCdoPtr, prop._typeName, t_listCdoBytes, ctx );
				serializeValueBinary( pModPtr, prop._typeName, t_listModBytes, ctx );
			}
			if ( t_listCdoBytes == t_listModBytes )
				continue;

			const uint32 nameHash	= prop.getNameHash();
			const uint32 size		= static_cast<uint32>( t_listModBytes.size() );
			const uint8* pHashBytes = reinterpret_cast<const uint8*>( &nameHash );
			const uint8* pSizeBytes = reinterpret_cast<const uint8*>( &size );
			listOutDiffBuffer.insert( listOutDiffBuffer.end(), pHashBytes, pHashBytes + sizeof( uint32 ) );
			listOutDiffBuffer.insert( listOutDiffBuffer.end(), pSizeBytes, pSizeBytes + sizeof( uint32 ) );
			listOutDiffBuffer.insert( listOutDiffBuffer.end(), t_listModBytes.begin(), t_listModBytes.end() );
		}

		return true;
	}

	bool ObjectDiffSerializer::deserializeDiff( void* pTargetInstance, const TypeInfo& typeInfo, const uint8* pDiffData, size_t diffSize )
	{
		if ( pTargetInstance == nullptr || pDiffData == nullptr )
			return false;

		const SerializeContext& ctx = SerializeContext::getDefault();
		size_t					offset{ 0 };
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
					pProp = &propInfo;
					break;
				}
			}
			if ( pProp != nullptr )
			{
				void*  pDest = pProp->getValuePtr<void>( pTargetInstance );
				size_t local{ 0 };
				bool   ok{ true };
				if ( pDest != nullptr )
				{
					if ( pProp->_bIsContainer && pProp->hasContainerWrapper() )
					{
						ok = deserializeNestedContainerBinary( pDest, pProp->getContainerShape(), pDiffData + offset, payload,
															   local, ctx );
					}
					else
						ok = deserializeValueBinary( pDest, pProp->_typeName, pDiffData + offset, payload, local, ctx );
				}
				if ( ok == false )
					return false;
			}
			else
			{
				SW_LOG_WARNING( "[ObjectDiff] unknown property hash %#", nameHash );
				return false;
			}
			offset += payload;
		}
		return true;
	}

} // namespace sw
