#include "pch.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerInternal.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
	namespace
	{
		void writeProperty( JsonValue field, const PropertyInfo& prop, const void* pInstance, const SerializeContext& ctx )
		{
			const void* pPropPtr = prop.getValuePtr<void>( pInstance );
			if ( prop._bIsContainer && prop._nestedContainer != nullptr )
			{
				writeNestedContainerJson( field, pPropPtr, *prop._nestedContainer, ctx );
				return;
			}
			if ( prop._bIsContainer && prop._containerWrapper != nullptr )
			{
				ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
				if ( pSeq != nullptr )
				{
					field.setArray();
					const size_t sz = pSeq->getSize( pPropPtr );
					for ( size_t elemIndex = 0; elemIndex < sz; ++elemIndex )
					{
						const void* pElemPtr = pSeq->getElementConst( pPropPtr, elemIndex );
						writeJsonValue( field.pushBack(), pElemPtr, prop._elementTypeName, ctx );
					}
					return;
				}
				IMapContainerWrapper* pMapWrap = prop._containerWrapper->asMap();
				if ( pMapWrap != nullptr )
				{
					field.setObject();
					pMapWrap->forEach( pPropPtr, [&]( const void* pKPtr, const void* pVPtr )
					{
						StringBuilder<constant::kMaxBuffer8192> keySs;
						valueToText( keySs, pKPtr, prop._keyTypeName, ctx );
						writeJsonValue( field.set( keySs.view(), false ), pVPtr, prop._elementTypeName, ctx );
					} );
					return;
				}
			}
			writeJsonValue( field, pPropPtr, prop._typeName, ctx );
		}

		bool readProperty( const JsonValue& field, const PropertyInfo& prop, void* pInstance, const SerializeContext& ctx )
		{
			void* pPropPtr = prop.getValuePtr<void>( pInstance );
			if ( prop._bIsContainer && prop._nestedContainer != nullptr )
				return readNestedContainerJson( pPropPtr, *prop._nestedContainer, field, ctx );
			if ( prop._bIsContainer && prop._containerWrapper != nullptr )
			{
				prop._containerWrapper->clear( pPropPtr );
				ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
				if ( pSeq != nullptr )
				{
					if ( field.isArray() == false )
						return false;
					pSeq->reserve( pPropPtr, field.size() );
					bool bElemOk{ true };
					for ( size_t elemIndex = 0; elemIndex < field.size(); ++elemIndex )
					{
						pSeq->addElementDefault( pPropPtr );
						void* pElemPtr = pSeq->getElement( pPropPtr, elemIndex );
						if ( readJsonValue( pElemPtr, prop._elementTypeName, field.at( elemIndex ), ctx ) == false )
							bElemOk = false;
					}
					return bElemOk;
				}
				IMapContainerWrapper* pMapWrap = prop._containerWrapper->asMap();
				if ( pMapWrap != nullptr )
				{
					if ( field.isObject() == false )
						return false;
					vector<uint8> listKBuf( pMapWrap->getKeySize() );
					vector<uint8> listVBuf( pMapWrap->getValueSize() );
					bool		  bAllOk{ true };
					for ( const string& key : field.memberNames() )
					{
						pMapWrap->defaultConstructKey( listKBuf.data() );
						pMapWrap->defaultConstructValue( listVBuf.data() );
						JsonDocument keyDoc;
						keyDoc.root().setString( key );
						const bool kOk = readJsonValue( listKBuf.data(), prop._keyTypeName, keyDoc.root(), ctx );
						const bool vOk = readJsonValue( listVBuf.data(), prop._elementTypeName, field.get( key, false ), ctx );
						if ( kOk && vOk )
							pMapWrap->insertKeyValue( pPropPtr, listKBuf.data(), listVBuf.data() );
						else
							bAllOk = false;
						pMapWrap->destroyKey( listKBuf.data() );
						pMapWrap->destroyValue( listVBuf.data() );
					}
					return bAllOk;
				}
				return false;
			}
			return readJsonValue( pPropPtr, prop._typeName, field, ctx );
		}

	} // namespace


	string JsonSerializer::escapeString( string_view value )
	{
		return JsonDocument::escapeString( value );
	}

	string JsonSerializer::unescapeString( string_view value )
	{
		return JsonDocument::unescapeString( value );
	}

	string JsonSerializer::extractStringField( string_view json, string_view fieldName,
											   bool bIgnoreCaseKeys )
	{
		return JsonDocument::extractStringField( json, fieldName, bIgnoreCaseKeys );
	}

	string JsonSerializer::serialize( const void* pInstance, const TypeInfo& typeInfo, const SerializeContext& ctx )
	{
		JsonDocument doc;
		writeObject( doc.makeObject(), pInstance, typeInfo, ctx );
		return doc.dump();
	}

	string JsonSerializer::serializePretty( const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces,
											const SerializeContext& ctx )
	{
		JsonDocument doc;
		writeObject( doc.makeObject(), pInstance, typeInfo, ctx );
		return doc.dump( static_cast<int32>( indentSpaces == 0 ? 4 : indentSpaces ) );
	}

	bool JsonSerializer::deserialize( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
									  const SerializeContext& ctx )
	{
		return deserializeSoft( pInstance, typeInfo, jsonStr, nullptr, nullptr, ctx );
	}

	void JsonSerializer::writeObject( JsonValue dst, const void* pInstance, const TypeInfo& typeInfo,
									  const SerializeContext& ctx )
	{
		if ( dst.isValid() == false || pInstance == nullptr )
			return;
		dst.setObject();
		for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
		{
			writeProperty( dst.set( prop._name.c_str(), false ), prop, pInstance, ctx );
		}
	}

	bool JsonSerializer::readObject( JsonValue src, void* pInstance, const TypeInfo& typeInfo,
									 vector<SchemaOrphanValue>* pOutOrphans, uint32* pOutVersion,
									 const SerializeContext& ctx )
	{
		if ( pInstance == nullptr || src.isObject() == false )
			return false;

		const bool					bIgnoreCaseKeys = ctx.ignoreCaseKeys();
		const vector<PropertyInfo>& listProps		= typeInfo.getPropertiesWithBase();
		unordered_set<uint32>		uniqueSeen;
		bool						bFieldError{ false };

		if ( pOutVersion != nullptr )
			*pOutVersion = 0;

		for ( const string& keyRaw : src.memberNames() )
		{
			const JsonValue field = src.get( keyRaw, false );
			if ( keysEqual( keyRaw, kSchemaVersionKey, bIgnoreCaseKeys ) )
			{
				if ( pOutVersion != nullptr )
					*pOutVersion = static_cast<uint32>( field.asUint( 0 ) );
				continue;
			}

			bool				bCaseVariant{ false };
			const PropertyInfo* pMatched = matchProperty( listProps, keyRaw, bIgnoreCaseKeys, bCaseVariant );
			if ( pMatched == nullptr && ( bCaseVariant || keyRaw == "_bActive" || keyRaw == "bActive" || keyRaw == "_componentName" ) )
				continue;

			if ( pMatched == nullptr )
			{
				if ( pOutOrphans != nullptr )
				{
					SchemaOrphanValue	orphan;
					const hashed_string keyHs( keyRaw.c_str() );
					orphan._name	 = keyHs;
					orphan._nameHash = keyHs.getHash();
					orphan._text	 = field.dump();
					pOutOrphans->push_back( std::move( orphan ) );
				}
				else
					bFieldError = true;
				continue;
			}

			uniqueSeen.insert( pMatched->getNameHash() );
			if ( readProperty( field, *pMatched, pInstance, ctx ) == false )
			{
				if ( pOutOrphans != nullptr )
				{
					SchemaOrphanValue orphan;
					orphan._name	 = pMatched->_name;
					orphan._nameHash = pMatched->getNameHash();
					orphan._text	 = field.dump();
					pOutOrphans->push_back( std::move( orphan ) );
				}
				else
					bFieldError = true;
			}
		}

		for ( const PropertyInfo& prop : listProps )
		{
			if ( uniqueSeen.find( prop.getNameHash() ) != uniqueSeen.end() )
				continue;
			applyPropertyDefault( prop.getValuePtr<void>( pInstance ), prop, ctx );
		}

		if ( pOutOrphans != nullptr )
			return true;
		return bFieldError == false;
	}

	bool JsonSerializer::deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
										  vector<SchemaOrphanValue>* pOutOrphans, uint32* pOutVersion,
										  const SerializeContext& ctx )
	{
		JsonDocument doc;
		if ( doc.parse( jsonStr ) == false )
			return false;
		return readObject( doc.root(), pInstance, typeInfo, pOutOrphans, pOutVersion, ctx );
	}

	string JsonSerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
											   const SerializeContext& ctx )
	{
		JsonDocument doc;
		JsonValue	 root = doc.makeObject();
		root.set( kSchemaVersionKey, false ).setUint( version );
		for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
		{
			writeProperty( root.set( prop._name.c_str(), false ), prop, pInstance, ctx );
		}
		return doc.dump();
	}

	bool JsonSerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
											   string_view jsonStr, uint32 currentVersion, SchemaMigrateFn migrate,
											   const TypeInfo* pLegacyTypeInfo, const SerializeContext& ctx )
	{
		vector<SchemaOrphanValue> listOrphans;
		vector<uint8>			  listLegacyStorage;
		void*					  pLegacyPtr{ nullptr };
		outVersion = 0;

		if ( pLegacyTypeInfo != nullptr && pLegacyTypeInfo->_size > 0 )
		{
			pLegacyPtr = createScratchInstance( *pLegacyTypeInfo, listLegacyStorage );
			uint32 legacyVer{ 0 };
			if ( pLegacyPtr == nullptr ||
				 deserializeSoft( pLegacyPtr, *pLegacyTypeInfo, jsonStr, &listOrphans, &legacyVer, ctx ) == false )
			{
				destroyScratchInstance( pLegacyPtr, *pLegacyTypeInfo );
				return false;
			}
			outVersion = legacyVer;
		}

		uint32 softVer{ 0 };
		if ( deserializeSoft( pInstance, typeInfo, jsonStr, &listOrphans, &softVer, ctx ) == false )
		{
			if ( pLegacyPtr != nullptr )
				destroyScratchInstance( pLegacyPtr, *pLegacyTypeInfo );
			return false;
		}
		if ( pLegacyPtr == nullptr )
			outVersion = softVer;
		else if ( softVer != 0 )
			outVersion = softVer;

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
		else if ( migrate == nullptr && outVersion != currentVersion )
		{
			SW_LOG_WARNING( "[JsonSerializer] (%#) schema version %# -> %# with no migrate callback (%# _orphans: %#)",
							typeInfo._name.c_str(), outVersion, currentVersion, static_cast<uint32>( listOrphans.size() ),
							listOrphans.empty() ? "" : listOrphans[0]._name.c_str() );
			ok = false;
		}
		if ( pLegacyPtr != nullptr )
			destroyScratchInstance( pLegacyPtr, *pLegacyTypeInfo );
		return ok;
	}

} // namespace sw
