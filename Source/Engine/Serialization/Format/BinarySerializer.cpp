#include "pch.h"

#include "Engine/Serialization/Format/BinarySerializer.h"

#include "Core/Compression/CompressionStream.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/BinaryStream.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerInternal.h"

namespace sw
{
	SW_LOG_CALLER( "BinarySerializer" );

	namespace
	{
		bool deserializeUntransacted( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize, const SerializeContext& ctx )
		{
			BinaryStreamReader reader( pData, dataSize );
			uint32			   propCount{ 0 };
			if ( reader.read( propCount ) == false )
				return false;

			const vector<PropertyInfo>& listProps = typeInfo.getPropertiesWithBase();
			const size_t				numProps  = listProps.size();
			uint64						seenBitmask{ 0 };
			unordered_set<uint32>		uniqueSeenPropHashes;
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

				const PropertyInfo* pTargetProp	 = nullptr;
				size_t				matchedIndex = 0;
				for ( size_t propSearchIdx = 0; propSearchIdx < numProps; ++propSearchIdx )
				{
					if ( listProps[propSearchIdx].matchesNameHash( tagHash ) )
					{
						pTargetProp	 = &listProps[propSearchIdx];
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
				if ( numProps <= 64 )
					seenBitmask |= ( 1ULL << matchedIndex );
				else
					uniqueSeenPropHashes.insert( prop.getNameHash() );

				void* pPropPtr = prop.getValuePtr<void>( pInstance );

				if ( wireTypeHash != 0 && wireTypeHash != prop._typeName.getHash() )
				{
					if ( tryCoerceBinaryPayload( pPropPtr, prop._typeName, pData + payloadStart, payloadSize, ctx ) == false )
						return false;
					reader.skip( payloadSize );
					continue;
				}

				if ( prop._bIsContainer && prop.hasContainerWrapper() )
				{
					size_t local = payloadStart;
					if ( deserializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), pData, payloadStart + payloadSize, local, ctx ) == false )
						return false;
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

				reader.skip( payloadSize );
			}

			// Omitted tags: apply PROPERTY(Default) when present (Binary is tag-driven, not TypeInfo-driven).
			for ( size_t propIdx = 0; propIdx < numProps; ++propIdx )
			{
				if ( numProps <= 64 )
				{
					if ( ( seenBitmask & ( 1ULL << propIdx ) ) != 0 )
						continue;
				}
				else
				{
					if ( uniqueSeenPropHashes.find( listProps[propIdx].getNameHash() ) != uniqueSeenPropHashes.end() )
						continue;
				}
				applyPropertyDefault( listProps[propIdx].getValuePtr<void>( pInstance ), listProps[propIdx], ctx );
			}

			return true;
		}

	} // namespace

	void BinarySerializer::serialize( const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& listOutBuffer,
									  const SerializeContext& ctx )
	{
		BinaryStreamWriter			writer( listOutBuffer );
		const vector<PropertyInfo>& listProps = typeInfo.getPropertiesWithBase();
		uint32						propCount = static_cast<uint32>( listProps.size() );
		listOutBuffer.reserve( listOutBuffer.size() + sizeof( uint32 ) + propCount * 32 );
		writer.write( propCount );

		for ( const PropertyInfo& prop : listProps )
		{
			const void* pPropPtr = prop.getValuePtr<void>( pInstance );

			uint32 hashVal = prop.getNameHash();
			writer.write( hashVal );

			uint32 typeHash = prop._typeName.getHash();
			writer.write( typeHash );

			size_t sizeHeaderPos = writer.getOffset();
			uint32 dummySize{ 0 };
			writer.write( dummySize );

			size_t payloadStart = writer.getOffset();

			if ( prop._bIsContainer && prop.hasContainerWrapper() )
				serializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), listOutBuffer, ctx );
			else
				serializeValueBinary( pPropPtr, prop._typeName, listOutBuffer, ctx );

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
		BinaryStreamReader reader( pData, dataSize );
		uint32			   propCount{ 0 };
		if ( reader.read( propCount ) == false )
			return false;

		const vector<PropertyInfo>& listProps = typeInfo.getPropertiesWithBase();
		const size_t				numProps  = listProps.size();
		uint64						seenBitmask{ 0 };
		unordered_set<uint32>		uniqueSeenPropHashes;
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

			const PropertyInfo* pTargetProp	 = nullptr;
			size_t				matchedIndex = 0;
			for ( size_t propSearchIdx = 0; propSearchIdx < numProps; ++propSearchIdx )
			{
				if ( listProps[propSearchIdx].matchesNameHash( tagHash ) )
				{
					pTargetProp	 = &listProps[propSearchIdx];
					matchedIndex = propSearchIdx;
					break;
				}
			}

			if ( pTargetProp == nullptr )
			{
				pushOrphanVal( pOutOrphans, {}, tagHash, wireTypeHash, pData + payloadStart, payloadSize );
				reader.skip( payloadSize );
				continue;
			}

			const PropertyInfo& prop = *pTargetProp;
			if ( numProps <= 64 )
				seenBitmask |= ( 1ULL << matchedIndex );
			else
				uniqueSeenPropHashes.insert( prop.getNameHash() );

			void*  pPropPtr = prop.getValuePtr<void>( pInstance );
			bool   applied{ false };
			size_t local = payloadStart;

			if ( prop._bIsContainer && prop.hasContainerWrapper() )
				applied = deserializeNestedContainerBinary( pPropPtr, prop.getContainerShape(), pData, payloadStart + payloadSize, local, ctx );
			else
			{
				applied = deserializeValueBinary( pPropPtr, prop._typeName, pData, payloadStart + payloadSize, local, ctx );
				if ( applied == false )
					applied = tryCoerceBinaryPayload( pPropPtr, prop._typeName, pData + payloadStart, payloadSize, ctx );
			}

			if ( applied == false )
				pushOrphanVal( pOutOrphans, prop._name, tagHash, wireTypeHash, pData + payloadStart, payloadSize );

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
				if ( uniqueSeenPropHashes.find( listProps[propIdx].getNameHash() ) != uniqueSeenPropHashes.end() )
					continue;
			}
			applyPropertyDefault( listProps[propIdx].getValuePtr<void>( pInstance ), listProps[propIdx], ctx );
		}

		return true;
	}

	void BinarySerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& listOutBuffer,
											   const SerializeContext& ctx )
	{
		BinaryStreamWriter writer( listOutBuffer );
		writer.write( version );
		serialize( pInstance, typeInfo, listOutBuffer, ctx );
	}

	bool BinarySerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
												 uint32 currentVersion, SchemaMigrateFn migrate, const TypeInfo* pLegacyTypeInfo,
												 const SerializeContext& ctx )
	{
		BinaryStreamReader reader( pData, dataSize );
		if ( reader.read( outVersion ) == false )
			return false;

		const uint8* pBody	  = pData + reader.getOffset();
		const size_t bodySize = dataSize - reader.getOffset();

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
			SW_LOG_WARNING( "schema version %# -> %# with no migrate callback (%# listOrphans)",
							outVersion, currentVersion, static_cast<uint32>( listOrphans.size() ) );
			ok = false;
		}

		if ( pLegacyPtr != nullptr )
			destroyScratchInstance( pLegacyPtr, *pLegacyTypeInfo );
		return ok;
	}

	bool BinarySerializer::serializeCompressed( const void*				pInstance,
												const TypeInfo&			typeInfo,
												vector<uint8>&			listOutBuffer,
												CompressionCodecType	codecType,
												const SerializeContext& ctx )
	{
		listOutBuffer.clear();
		if ( pInstance == nullptr )
			return false;

		vector<uint8> listRawBinary;
		BinarySerializer::serialize( pInstance, typeInfo, listRawBinary, ctx );
		if ( listRawBinary.empty() )
			return false;

		return CompressionStream::compressBuffer( listRawBinary.data(), listRawBinary.size(), listOutBuffer, codecType );
	}

	bool BinarySerializer::deserializeCompressed( void*					  pInstance,
												  const TypeInfo&		  typeInfo,
												  const uint8*			  pData,
												  size_t				  dataSize,
												  const SerializeContext& ctx )
	{
		if ( pInstance == nullptr || pData == nullptr || dataSize == 0 )
			return false;

		vector<uint8> listRawBinary;
		const bool	  bDecompressOk = CompressionStream::decompressBuffer( pData, dataSize, listRawBinary );
		if ( bDecompressOk == false || listRawBinary.empty() )
			return false;

		return BinarySerializer::deserialize( pInstance, typeInfo, listRawBinary.data(), listRawBinary.size(), ctx );
	}

	bool BinarySerializer::serializeVersionedCompressed( uint32					 version,
														 const void*			 pInstance,
														 const TypeInfo&		 typeInfo,
														 vector<uint8>&			 listOutBuffer,
														 CompressionCodecType	 codecType,
														 const SerializeContext& ctx )
	{
		listOutBuffer.clear();
		if ( pInstance == nullptr )
			return false;

		vector<uint8> listRawBinary;
		BinarySerializer::serializeVersioned( version, pInstance, typeInfo, listRawBinary, ctx );
		if ( listRawBinary.empty() )
			return false;

		return CompressionStream::compressBuffer( listRawBinary.data(), listRawBinary.size(), listOutBuffer, codecType );
	}

	bool BinarySerializer::deserializeVersionedCompressed( uint32&				   outVersion,
														   void*				   pInstance,
														   const TypeInfo&		   typeInfo,
														   const uint8*			   pData,
														   size_t				   dataSize,
														   uint32				   currentVersion,
														   SchemaMigrateFn		   migrate,
														   const TypeInfo*		   pLegacyTypeInfo,
														   const SerializeContext& ctx )
	{
		if ( pInstance == nullptr || pData == nullptr || dataSize == 0 )
			return false;

		vector<uint8> listRawBinary;
		const bool	  bDecompressOk = CompressionStream::decompressBuffer( pData, dataSize, listRawBinary );
		if ( bDecompressOk == false || listRawBinary.empty() )
			return false;

		return BinarySerializer::deserializeVersioned( outVersion, pInstance, typeInfo, listRawBinary.data(), listRawBinary.size(),
													   currentVersion, migrate, pLegacyTypeInfo, ctx );
	}
} // namespace sw
