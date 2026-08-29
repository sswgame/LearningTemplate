#include "pch.h"

#include "Engine/Serialization/Object/ObjectDiffSerializer.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SerializerUtil.h"

namespace sw
{
	SW_LOG_CALLER( "ObjectDiff" );

	bool ObjectDiffSerializer::serializeDiff( vector<uint8>& listOutDiffBuffer, const void* pCdoInstance, const void* pModifiedInstance, const TypeInfo& typeInfo )
	{
		if ( pCdoInstance == nullptr || pModifiedInstance == nullptr )
			return false;

		listOutDiffBuffer.clear();
		const SerializeContext& ctx = SerializeContext::getDefault();
		vector<uint8>			listCdoBytes;
		vector<uint8>			listModBytes;

		typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
		{
			if ( prop._metadata._bTransient == SW_TRUE )
				return;
			const void* pCdoPtr = prop.getRawPtr( pCdoInstance );
			const void* pModPtr = prop.getRawPtr( pModifiedInstance );
			if ( prop._bIsBitField == SW_FALSE && ( pCdoPtr == nullptr || pModPtr == nullptr ) )
				return;

			listCdoBytes.clear();
			listModBytes.clear();
			if ( prop._bIsBitField == SW_TRUE )
			{
				const bool bCdo = prop.getValue<bool>( pCdoInstance );
				const bool bMod = prop.getValue<bool>( pModifiedInstance );
				SerializerUtil::serializeValueBinary( &bCdo, hashed_string( "bool" ), listCdoBytes, ctx );
				SerializerUtil::serializeValueBinary( &bMod, hashed_string( "bool" ), listModBytes, ctx );
			}
			else if ( prop._bIsContainer && prop.hasContainerWrapper() )
			{
				SerializerUtil::serializeNestedContainerBinary( pCdoPtr, prop.getContainerShape(), listCdoBytes, ctx );
				SerializerUtil::serializeNestedContainerBinary( pModPtr, prop.getContainerShape(), listModBytes, ctx );
			}
			else
			{
				SerializerUtil::serializeValueBinary( pCdoPtr, prop._typeName, listCdoBytes, ctx );
				SerializerUtil::serializeValueBinary( pModPtr, prop._typeName, listModBytes, ctx );
			}
			if ( listCdoBytes == listModBytes )
				return;

			const uint32 nameHash	= prop.getNameHash();
			const uint32 size		= static_cast<uint32>( listModBytes.size() );
			const uint8* pHashBytes = reinterpret_cast<const uint8*>( &nameHash );
			const uint8* pSizeBytes = reinterpret_cast<const uint8*>( &size );
			listOutDiffBuffer.insert( listOutDiffBuffer.end(), pHashBytes, pHashBytes + sizeof( uint32 ) );
			listOutDiffBuffer.insert( listOutDiffBuffer.end(), pSizeBytes, pSizeBytes + sizeof( uint32 ) );
			listOutDiffBuffer.insert( listOutDiffBuffer.end(), listModBytes.begin(), listModBytes.end() );
		} );

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
				bool   ok{ true };
				if ( pProp->_bIsBitField == SW_TRUE )
				{
					bool bVal = false;
					ok		  = SerializerUtil::deserializeValueBinary( &bVal, hashed_string( "bool" ), pDiffData + offset, payload, local, ctx );
					if ( ok )
						pProp->setValue<bool>( pTargetInstance, bVal );
				}
				else if ( pDest != nullptr )
				{
					if ( pProp->_bIsContainer && pProp->hasContainerWrapper() )
					{
						ok = SerializerUtil::deserializeNestedContainerBinary( pDest, pProp->getContainerShape(), pDiffData + offset, payload,
																			   local, ctx );
					}
					else
						ok = SerializerUtil::deserializeValueBinary( pDest, pProp->_typeName, pDiffData + offset, payload, local, ctx );
				}
				if ( ok == false )
					return false;
			}
			else
			{
				SW_LOG_WARNING( "unknown property hash %#", nameHash );
				return false;
			}
			offset += payload;
		}
		return true;
	}

} // namespace sw
