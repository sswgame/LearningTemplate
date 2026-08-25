#include "pch.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerInternal.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

namespace sw
{
	namespace
	{
		bool deserializeUntransacted( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize, const SerializeContext& ctx )
		{
			size_t offset{ 0 };
			if ( offset + sizeof( uint32 ) > dataSize )
				return false;

			uint32 propCount{ 0 };
			Memory::copy( &propCount, pData + offset, sizeof( uint32 ) );
			offset += sizeof( uint32 );

			const vector<PropertyInfo>& listProps = typeInfo.getPropertiesWithBase();
			unordered_set<uint32>		uniqueSeenPropHashes;
			uniqueSeenPropHashes.reserve( listProps.size() );

			for ( uint32 propIndex = 0; propIndex < propCount; ++propIndex )
			{
				if ( offset + sizeof( uint32 ) * 3 > dataSize )
					return false;

				uint32 tagHash{ 0 };
				uint32 wireTypeHash{ 0 };
				uint32 payloadSize{ 0 };
				Memory::copy( &tagHash, pData + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );
				Memory::copy( &wireTypeHash, pData + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );
				Memory::copy( &payloadSize, pData + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );

				const size_t payloadStart = offset;
				if ( offset + payloadSize > dataSize )
					return false;

				const PropertyInfo* pTargetProp = nullptr;
				for ( const PropertyInfo& prop : listProps )
				{
					if ( prop.matchesNameHash( tagHash ) )
					{
						pTargetProp = &prop;
						break;
					}
				}

				if ( pTargetProp == nullptr )
				{
					if ( ctx.allowUnknownProperties() )
					{
						offset = payloadStart + payloadSize;
						continue;
					}
					return false;
				}

				const PropertyInfo& prop = *pTargetProp;
				uniqueSeenPropHashes.insert( prop.getNameHash() );
				void* pPropPtr = prop.getValuePtr<void>( pInstance );

				if ( wireTypeHash != 0 && wireTypeHash != prop._typeName.getHash() )
				{
					if ( tryCoerceBinaryPayload( pPropPtr, prop._typeName, pData + payloadStart, payloadSize, ctx ) == false )
						return false;
					offset = payloadStart + payloadSize;
					continue;
				}

				if ( prop._bIsContainer && prop._nestedContainer != nullptr )
				{
					size_t local = payloadStart;
					if ( deserializeNestedContainerBinary( pPropPtr, *prop._nestedContainer, pData, payloadStart + payloadSize, local, ctx ) == false )
						return false;
					if ( local != payloadStart + payloadSize )
						return false;
				}
				else if ( prop._bIsContainer && prop._containerWrapper != nullptr )
				{
					prop._containerWrapper->clear( pPropPtr );

					size_t					   local	= payloadStart;
					ISequenceContainerWrapper* pSeq		= prop._containerWrapper->asSequence();
					IMapContainerWrapper*	   pMapWrap = prop._containerWrapper->asMap();
					if ( pSeq != nullptr )
					{
						if ( local + sizeof( uint32 ) > payloadStart + payloadSize )
							return false;
						uint32 count{ 0 };
						Memory::copy( &count, pData + local, sizeof( uint32 ) );
						local += sizeof( uint32 );

						pSeq->reserve( pPropPtr, count );

						for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
						{
							pSeq->addElementDefault( pPropPtr );
							void* pElemPtr = pSeq->getElement( pPropPtr, elemIndex );
							if ( deserializeValueBinary( pElemPtr, prop._elementTypeName, pData, payloadStart + payloadSize, local, ctx ) == false )
								return false;
						}
					}
					else if ( pMapWrap != nullptr )
					{
						if ( local + sizeof( uint32 ) > payloadStart + payloadSize )
							return false;
						uint32 count{ 0 };
						Memory::copy( &count, pData + local, sizeof( uint32 ) );
						local += sizeof( uint32 );

						vector<uint8> listKBuf( pMapWrap->getKeySize() );
						vector<uint8> listVBuf( pMapWrap->getValueSize() );

						for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
						{
							pMapWrap->defaultConstructKey( listKBuf.data() );
							pMapWrap->defaultConstructValue( listVBuf.data() );

							bool kOk = deserializeValueBinary( listKBuf.data(), prop._keyTypeName, pData, payloadStart + payloadSize, local, ctx );
							bool vOk = deserializeValueBinary( listVBuf.data(), prop._elementTypeName, pData, payloadStart + payloadSize, local, ctx );

							if ( kOk && vOk )
								pMapWrap->insertKeyValue( pPropPtr, listKBuf.data(), listVBuf.data() );

							pMapWrap->destroyKey( listKBuf.data() );
							pMapWrap->destroyValue( listVBuf.data() );

							if ( kOk == false || vOk == false )
								return false;
						}
					}
					if ( local != payloadStart + payloadSize )
						return false;
				}
				else
				{
					size_t local = payloadStart;
					if ( deserializeValueBinary( pPropPtr, prop._typeName, pData, payloadStart + payloadSize, local, ctx ) == false )
						return false;
					if ( local != payloadStart + payloadSize )
						return false;
				}

				offset = payloadStart + payloadSize;
			}

			// Omitted tags: apply PROPERTY(Default) when present (Binary is tag-driven, not TypeInfo-driven).
			for ( const PropertyInfo& prop : listProps )
			{
				if ( uniqueSeenPropHashes.find( prop.getNameHash() ) != uniqueSeenPropHashes.end() )
					continue;
				applyPropertyDefault( prop.getValuePtr<void>( pInstance ), prop, ctx );
			}

			return true;
		}

	} // namespace

	void BinarySerializer::serialize( const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& listOutBuffer,
									  const SerializeContext& ctx )
	{
		const vector<PropertyInfo>& listProps = typeInfo.getPropertiesWithBase();
		uint32						propCount = static_cast<uint32>( listProps.size() );
		listOutBuffer.reserve( listOutBuffer.size() + sizeof( uint32 ) + propCount * 32 );
		const uint8* pCountBytes = reinterpret_cast<const uint8*>( &propCount );
		listOutBuffer.insert( listOutBuffer.end(), pCountBytes, pCountBytes + sizeof( uint32 ) );

		for ( const PropertyInfo& prop : listProps )
		{
			const void* pPropPtr = prop.getValuePtr<void>( pInstance );

			uint32		 hashVal	= prop.getNameHash();
			const uint8* pHashBytes = reinterpret_cast<const uint8*>( &hashVal );
			listOutBuffer.insert( listOutBuffer.end(), pHashBytes, pHashBytes + sizeof( uint32 ) );

			uint32		 typeHash		= prop._typeName.getHash();
			const uint8* pTypeHashBytes = reinterpret_cast<const uint8*>( &typeHash );
			listOutBuffer.insert( listOutBuffer.end(), pTypeHashBytes, pTypeHashBytes + sizeof( uint32 ) );

			size_t		 sizeHeaderPos = listOutBuffer.size();
			uint32		 dummySize{ 0 };
			const uint8* pDummyBytes = reinterpret_cast<const uint8*>( &dummySize );
			listOutBuffer.insert( listOutBuffer.end(), pDummyBytes, pDummyBytes + sizeof( uint32 ) );

			size_t payloadStart = listOutBuffer.size();

			if ( prop._bIsContainer && prop._nestedContainer != nullptr )
				serializeNestedContainerBinary( pPropPtr, *prop._nestedContainer, listOutBuffer, ctx );
			else if ( prop._bIsContainer && prop._containerWrapper != nullptr )
			{
				ISequenceContainerWrapper* pSeq		= prop._containerWrapper->asSequence();
				IMapContainerWrapper*	   pMapWrap = prop._containerWrapper->asMap();
				if ( pSeq != nullptr )
				{
					uint32		 count	 = static_cast<uint32>( pSeq->getSize( pPropPtr ) );
					const uint8* pCBytes = reinterpret_cast<const uint8*>( &count );
					listOutBuffer.insert( listOutBuffer.end(), pCBytes, pCBytes + sizeof( uint32 ) );

					for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
					{
						const void* pElemPtr = pSeq->getElementConst( pPropPtr, elemIndex );
						serializeValueBinary( pElemPtr, prop._elementTypeName, listOutBuffer, ctx );
					}
				}
				else if ( pMapWrap != nullptr )
				{
					uint32		 count	 = static_cast<uint32>( pMapWrap->getSize( pPropPtr ) );
					const uint8* pCBytes = reinterpret_cast<const uint8*>( &count );
					listOutBuffer.insert( listOutBuffer.end(), pCBytes, pCBytes + sizeof( uint32 ) );

					pMapWrap->forEach( pPropPtr, [&]( const void* pKPtr, const void* pVPtr )
					{
						serializeValueBinary( pKPtr, prop._keyTypeName, listOutBuffer, ctx );
						serializeValueBinary( pVPtr, prop._elementTypeName, listOutBuffer, ctx );
					} );
				}
			}
			else
				serializeValueBinary( pPropPtr, prop._typeName, listOutBuffer, ctx );

			uint32 payloadSize = static_cast<uint32>( listOutBuffer.size() - payloadStart );
			Memory::copy( &listOutBuffer[sizeHeaderPos], &payloadSize, sizeof( uint32 ) );
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

		serialize( pSrcData, typeInfo, t_listCloneBuffer );
		if ( t_listCloneBuffer.empty() )
			return false;

		return deserializeUntransacted( pDstData, typeInfo, t_listCloneBuffer.data(), t_listCloneBuffer.size(),
										SerializeContext::getDefault() );
	}

	bool BinarySerializer::deserialize( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
										const SerializeContext& ctx )
	{
		if ( pInstance == nullptr || pData == nullptr )
			return false;

		vector<uint8> listBackup;
		serialize( pInstance, typeInfo, listBackup, ctx );
		if ( deserializeUntransacted( pInstance, typeInfo, pData, dataSize, ctx ) )
			return true;
		if ( listBackup.empty() == false )
			deserializeUntransacted( pInstance, typeInfo, listBackup.data(), listBackup.size(), ctx );
		return false;
	}

	namespace
	{
		static void pushOrphanVal( vector<SchemaOrphanValue>* pOutOrphans, hashed_string name, uint32 nameHash, uint32 wireTypeHash, const uint8* pPayload, uint32 payloadSize )
		{
			if ( pOutOrphans == nullptr )
				return;
			SchemaOrphanValue orphan;
			orphan._name		 = name;
			orphan._nameHash	 = nameHash != 0 ? nameHash : name.getHash();
			orphan._wireTypeHash = wireTypeHash;
			orphan._listBinary.assign( pPayload, pPayload + payloadSize );
			pOutOrphans->push_back( std::move( orphan ) );
		}
	} // namespace
	bool BinarySerializer::deserializeSoft( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
											vector<SchemaOrphanValue>* pOutOrphans, const SerializeContext& ctx )
	{
		size_t offset{ 0 };
		if ( pData == nullptr || offset + sizeof( uint32 ) > dataSize )
			return false;

		uint32 propCount{ 0 };
		Memory::copy( &propCount, pData + offset, sizeof( uint32 ) );
		offset += sizeof( uint32 );

		const vector<PropertyInfo>& listProps = typeInfo.getPropertiesWithBase();
		unordered_set<uint32>		uniqueSeenPropHashes;
		uniqueSeenPropHashes.reserve( listProps.size() );

		for ( uint32 propIndex = 0; propIndex < propCount; ++propIndex )
		{
			if ( offset + sizeof( uint32 ) * 3 > dataSize )
				return false;

			uint32 tagHash{ 0 };
			uint32 wireTypeHash{ 0 };
			uint32 payloadSize{ 0 };
			Memory::copy( &tagHash, pData + offset, sizeof( uint32 ) );
			offset += sizeof( uint32 );
			Memory::copy( &wireTypeHash, pData + offset, sizeof( uint32 ) );
			offset += sizeof( uint32 );
			Memory::copy( &payloadSize, pData + offset, sizeof( uint32 ) );
			offset += sizeof( uint32 );

			if ( offset + payloadSize > dataSize )
				return false;

			const size_t		payloadStart = offset;
			const PropertyInfo* pTargetProp	 = nullptr;
			for ( const PropertyInfo& prop : listProps )
			{
				if ( prop.matchesNameHash( tagHash ) )
				{
					pTargetProp = &prop;
					break;
				}
			}

			if ( pTargetProp == nullptr )
			{
				pushOrphanVal( pOutOrphans, {}, tagHash, wireTypeHash, pData + payloadStart, payloadSize );
				offset = payloadStart + payloadSize;
				continue;
			}

			const PropertyInfo& prop = *pTargetProp;
			uniqueSeenPropHashes.insert( prop.getNameHash() );
			void*  pPropPtr = prop.getValuePtr<void>( pInstance );
			bool   applied{ false };
			size_t local = payloadStart;

			if ( prop._bIsContainer && prop._nestedContainer != nullptr )
				applied = deserializeNestedContainerBinary( pPropPtr, *prop._nestedContainer, pData, payloadStart + payloadSize, local, ctx );
			else if ( prop._bIsContainer && prop._containerWrapper != nullptr )
			{
				prop._containerWrapper->clear( pPropPtr );
				ISequenceContainerWrapper* pSeq		= prop._containerWrapper->asSequence();
				IMapContainerWrapper*	   pMapWrap = prop._containerWrapper->asMap();
				if ( pSeq != nullptr )
				{
					if ( local + sizeof( uint32 ) <= payloadStart + payloadSize )
					{
						uint32 count{ 0 };
						Memory::copy( &count, pData + local, sizeof( uint32 ) );
						local += sizeof( uint32 );
						pSeq->reserve( pPropPtr, count );
						applied = true;
						for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
						{
							pSeq->addElementDefault( pPropPtr );
							void* pElemPtr = pSeq->getElement( pPropPtr, elemIndex );
							if ( deserializeValueBinary( pElemPtr, prop._elementTypeName, pData, payloadStart + payloadSize, local, ctx ) == false )
							{
								applied = false;
								break;
							}
						}
					}
				}
				else if ( pMapWrap != nullptr )
				{
					if ( local + sizeof( uint32 ) <= payloadStart + payloadSize )
					{
						uint32 count{ 0 };
						Memory::copy( &count, pData + local, sizeof( uint32 ) );
						local += sizeof( uint32 );
						vector<uint8> listKBuf( pMapWrap->getKeySize() );
						vector<uint8> listVBuf( pMapWrap->getValueSize() );
						applied = true;
						for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
						{
							pMapWrap->defaultConstructKey( listKBuf.data() );
							pMapWrap->defaultConstructValue( listVBuf.data() );
							const bool kOk = deserializeValueBinary( listKBuf.data(), prop._keyTypeName, pData, payloadStart + payloadSize, local, ctx );
							const bool vOk = deserializeValueBinary( listVBuf.data(), prop._elementTypeName, pData, payloadStart + payloadSize, local, ctx );
							if ( kOk && vOk )
								pMapWrap->insertKeyValue( pPropPtr, listKBuf.data(), listVBuf.data() );
							else
								applied = false;
							pMapWrap->destroyKey( listKBuf.data() );
							pMapWrap->destroyValue( listVBuf.data() );
							if ( applied == false )
								break;
						}
					}
				}
			}
			else
			{
				applied = deserializeValueBinary( pPropPtr, prop._typeName, pData, payloadStart + payloadSize, local, ctx );
				if ( applied == false )
					applied = tryCoerceBinaryPayload( pPropPtr, prop._typeName, pData + payloadStart, payloadSize, ctx );
			}

			if ( applied == false )
				pushOrphanVal( pOutOrphans, prop._name, tagHash, wireTypeHash, pData + payloadStart, payloadSize );

			offset = payloadStart + payloadSize;
		}

		for ( const PropertyInfo& prop : listProps )
		{
			if ( uniqueSeenPropHashes.find( prop.getNameHash() ) != uniqueSeenPropHashes.end() )
				continue;
			applyPropertyDefault( prop.getValuePtr<void>( pInstance ), prop, ctx );
		}

		return true;
	}

	void BinarySerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& listOutBuffer,
											   const SerializeContext& ctx )
	{
		const uint8* pVerBytes = reinterpret_cast<const uint8*>( &version );
		listOutBuffer.insert( listOutBuffer.end(), pVerBytes, pVerBytes + sizeof( uint32 ) );
		serialize( pInstance, typeInfo, listOutBuffer, ctx );
	}

	bool BinarySerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
												 uint32 currentVersion, SchemaMigrateFn migrate, const TypeInfo* pLegacyTypeInfo,
												 const SerializeContext& ctx )
	{
		if ( pData == nullptr || dataSize < sizeof( uint32 ) )
			return false;

		Memory::copy( &outVersion, pData, sizeof( uint32 ) );
		const uint8* pBody	  = pData + sizeof( uint32 );
		const size_t bodySize = dataSize - sizeof( uint32 );

		vector<SchemaOrphanValue> listOrphans;
		vector<uint8>			  listLegacyStorage;
		void*					  pLegacyPtr{ nullptr };

		if ( pLegacyTypeInfo != nullptr && pLegacyTypeInfo->_size > 0 )
		{
			pLegacyPtr = createScratchInstance( *pLegacyTypeInfo, listLegacyStorage );
			if ( pLegacyPtr == nullptr ||
				 deserializeSoft( pLegacyPtr, *pLegacyTypeInfo, pBody, bodySize, &listOrphans, ctx ) == false )
			{
				destroyScratchInstance( pLegacyPtr, *pLegacyTypeInfo );
				return false;
			}
		}

		if ( deserializeSoft( pInstance, typeInfo, pBody, bodySize, &listOrphans, ctx ) == false )
		{
			if ( pLegacyPtr != nullptr )
				destroyScratchInstance( pLegacyPtr, *pLegacyTypeInfo );
			return false;
		}

		const bool needsMigrate = migrate != nullptr && ( outVersion != currentVersion || listOrphans.empty() == false || pLegacyPtr != nullptr );
		bool	   ok{ true };
		if ( needsMigrate )
		{
			SchemaMigrateContext mctx;
			mctx._fromVersion	  = outVersion;
			mctx._toVersion		  = currentVersion;
			mctx._pInstance		  = pInstance;
			mctx._pTypeInfo		  = &typeInfo;
			mctx._pLegacyInstance = pLegacyPtr;
			mctx._pLegacyTypeInfo = pLegacyTypeInfo;
			mctx._pOrphans		  = &listOrphans;
			mctx._pSerializeCtx	  = &ctx;
			ok					  = migrate( mctx );
		}
		else if ( migrate == nullptr && ( outVersion != currentVersion || listOrphans.empty() == false ) )
		{
			SW_LOG_WARNING( "[BinarySerializer] schema version %# -> %# with no migrate callback (%# listOrphans)",
							outVersion, currentVersion, static_cast<uint32>( listOrphans.size() ) );
			ok = false;
		}

		if ( pLegacyPtr != nullptr )
			destroyScratchInstance( pLegacyPtr, *pLegacyTypeInfo );
		return ok;
	}
} // namespace sw
