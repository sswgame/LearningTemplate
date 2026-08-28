#include "pch.h"

#include "Engine/Serialization/Format/JsonSerializer.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerInternal.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
	namespace
	{
		void writeProperty( JsonValue parent, const PropertyInfo& prop, const void* pInstance, const SerializeContext& ctx )
		{
			const void* pPropPtr = prop.getValuePtr<void>( pInstance );
			if ( prop._bIsContainer && prop.hasContainerWrapper() )
			{
				NestedContainerInfo shape = prop.getContainerShape();
				if ( shape._typeName.empty() )
					shape._typeName = prop._typeName;
				writeTypedContainerJson( parent, prop._name.c_str(), pPropPtr, shape, ctx );
				return;
			}
			writeJsonValue( parent.set( prop._name.c_str(), false ), pPropPtr, prop._typeName, ctx );
		}

		bool isContainerTypeKey( const vector<PropertyInfo>& listProps, string_view keyRaw, bool bIgnoreCaseKeys )
		{
			for ( const PropertyInfo& prop : listProps )
			{
				if ( prop._bIsContainer == false || prop.hasContainerWrapper() == false )
					continue;
				NestedContainerInfo shape = prop.getContainerShape();
				if ( shape._typeName.empty() )
					shape._typeName = prop._typeName;
				const utf8* pTypeTag = containerTypeTagName( shape._typeName );
				if ( pTypeTag != nullptr && keysEqual( keyRaw, pTypeTag, bIgnoreCaseKeys ) )
					return true;
			}
			return false;
		}

		bool readContainerTypeGroup( const JsonValue& group, const vector<PropertyInfo>& listProps, void* pInstance,
									 unordered_set<uint32>& uniqueSeen, bool& bFieldError, vector<SchemaOrphanValue>* pOutOrphans,
									 const SerializeContext& ctx )
		{
			if ( group.isArray() == false )
				return false;

			const bool bIgnore = ctx.ignoreCaseKeys();
			bool	   bOk{ true };
			for ( size_t nodeIndex = 0; nodeIndex < group.size(); ++nodeIndex )
			{
				const JsonValue node = group.at( nodeIndex );
				if ( node.isObject() == false )
				{
					bOk = false;
					continue;
				}

				const string		nameStr	   = node.get( kPropertyNameKey, bIgnore ).asString();
				bool				bCaseVariant{ false };
				const PropertyInfo* pMatched = matchProperty( listProps, nameStr, bIgnore, bCaseVariant );
				if ( pMatched == nullptr || pMatched->_bIsContainer == false || pMatched->hasContainerWrapper() == false )
				{
					if ( pOutOrphans != nullptr )
					{
						SchemaOrphanValue	orphan;
						const hashed_string nameHs( nameStr.c_str() );
						orphan._name	 = nameHs;
						orphan._nameHash = nameHs.getHash();
						orphan._text	 = node.dump();
						pOutOrphans->push_back( std::move( orphan ) );
					}
					else
						bFieldError = true;
					bOk = false;
					continue;
				}

				uniqueSeen.insert( pMatched->getNameHash() );
				void*				   pPropPtr = pMatched->getValuePtr<void>( pInstance );
				NestedContainerInfo	   shape	= pMatched->getContainerShape();
				if ( shape._typeName.empty() )
					shape._typeName = pMatched->_typeName;
				if ( readNestedContainerJson( pPropPtr, shape, node, ctx ) == false )
				{
					if ( pOutOrphans != nullptr )
					{
						SchemaOrphanValue orphan;
						orphan._name	 = pMatched->_name;
						orphan._nameHash = pMatched->getNameHash();
						orphan._text	 = node.dump();
						pOutOrphans->push_back( std::move( orphan ) );
					}
					else
						bFieldError = true;
					bOk = false;
				}
			}
			return bOk;
		}

		bool readProperty( const JsonValue& field, const PropertyInfo& prop, void* pInstance, const SerializeContext& ctx )
		{
			void* pPropPtr = prop.getValuePtr<void>( pInstance );
			if ( prop._bIsContainer && prop.hasContainerWrapper() )
				return readTypedContainerJson( pPropPtr, prop.getContainerShape(), field, ctx );
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
			writeProperty( dst, prop, pInstance, ctx );
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

			if ( isContainerTypeKey( listProps, keyRaw, bIgnoreCaseKeys ) )
			{
				if ( readContainerTypeGroup( field, listProps, pInstance, uniqueSeen, bFieldError, pOutOrphans, ctx ) == false &&
					 pOutOrphans == nullptr )
					bFieldError = true;
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
			writeProperty( root, prop, pInstance, ctx );
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
