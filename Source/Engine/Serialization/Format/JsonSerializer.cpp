#include "pch.h"

#include "Engine/Serialization/Format/JsonSerializer.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
	namespace
	{
		struct JsonSerializerInternal
		{
			static hashed_string resolveHandlerTypeName( const hashed_string& typeName, const SerializeContext& ctx )
			{
				if ( ctx.findBinaryWriter( typeName ) != nullptr || ctx.findTextWriter( typeName ) != nullptr )
					return typeName;

				TypeRegistry&	registry  = engine::getTypeRegistry();
				const TypeInfo* pTypeInfo = registry.findType( typeName );
				if ( pTypeInfo != nullptr )
				{
					if ( pTypeInfo->_name.empty() == false &&
						 ( ctx.findBinaryWriter( pTypeInfo->_name ) != nullptr ||
						   ctx.findTextWriter( pTypeInfo->_name ) != nullptr ) )
						return pTypeInfo->_name;
				}
				return typeName;
			}

			static bool isOwnedPointerElementType( hashed_string elementTypeName )
			{
				const utf8* pName = elementTypeName.c_str();
				if ( pName == nullptr )
					return false;
				for ( const utf8* pCursor = pName; *pCursor != '\0'; ++pCursor )
				{
					if ( *pCursor == '*' )
						return true;
				}
				return false;
			}

			static const TypeInfo* findNestedJsonObjectType( hashed_string typeName, const SerializeContext& ctx )
			{
				if ( ctx.findTextWriter( typeName ) != nullptr )
					return nullptr;
				if ( engine::getTypeRegistry().findEnum( typeName ) != nullptr )
					return nullptr;
				const TypeInfo* pTypeInfo = engine::getTypeRegistry().findType( typeName );
				if ( pTypeInfo == nullptr || pTypeInfo->isPrimitive() )
					return nullptr;
				return pTypeInfo;
			}

			static void writeJsonValue( JsonValue dst, const void* pValPtr, const hashed_string& typeName, const SerializeContext& ctx )
			{
				if ( dst.isValid() == false )
					return;

				const hashed_string resolved  = resolveHandlerTypeName( typeName, ctx );
				const bool			bIsString = ( resolved.isPredefinedType( PredefinedNameType::NameType_string ) ||
												  resolved.isPredefinedType( PredefinedNameType::NameType_hashed_string ) ||
												  resolved.isPredefinedType( PredefinedNameType::NameType_TagID ) );
				const bool			bIsBool	  = ( resolved.isPredefinedType( PredefinedNameType::NameType_bool ) ||
												  resolved.isPredefinedType( PredefinedNameType::NameType_atomic_bool ) );
				const bool			bIsEnum	  = ( engine::getTypeRegistry().findEnum( typeName ) != nullptr );

				if ( bIsString || bIsEnum )
				{
					StringBuilder<constant::kMaxBuffer8192> text;
					SerializerUtil::valueToText( text, pValPtr, typeName, ctx );
					dst.setString( text.view() );
					return;
				}

				if ( bIsBool )
				{
					StringBuilder<constant::kMaxBuffer8192> text;
					SerializerUtil::valueToText( text, pValPtr, typeName, ctx );
					dst.setBool( StringUtil::parseBool( text.view(), false ) );
					return;
				}

				if ( ctx.findTextWriter( resolved ) != nullptr )
				{
					StringBuilder<constant::kMaxBuffer8192> text;
					SerializerUtil::valueToText( text, pValPtr, typeName, ctx );
					JsonDocument parsed;
					if ( parsed.parse( text.view() ) && parsed.root().isObject() == false && parsed.root().isArray() == false )
					{
						dst.assignFrom( parsed.root() );
						return;
					}
					dst.setString( text.view() );
					return;
				}

				const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
				if ( pStructInfo != nullptr )
				{
					if ( pStructInfo->isPrimitive() == false )
					{
						JsonSerializer::writeObject( dst, pValPtr, *pStructInfo, ctx );
						return;
					}
				}

				StringBuilder<constant::kMaxBuffer8192> text;
				SerializerUtil::valueToText( text, pValPtr, typeName, ctx );
				dst.setString( text.view() );
			}

			/**
			 * @brief 컨테이너 원소 하나를 dst 에 씁니다. (중첩 컨테이너 / 구조체 / 소유 포인터 / 값)
			 * @details 소유 포인터만 런타임 타입을 알아야 하므로 { "TypeName": {...} } 래핑을 유지합니다.
			 */
			static void writeContainerElementJson( JsonValue dst, const void* pElemPtr, const NestedContainerInfo& nested,
												   bool bOwnedPtr, const SerializeContext& ctx )
			{
				if ( nested._elementNested != nullptr )
				{
					writeContainerValueJson( dst, pElemPtr, *nested._elementNested, ctx );
					return;
				}

				if ( bOwnedPtr )
				{
					void* const* ppObj = static_cast<void* const*>( pElemPtr );
					void*		 pObj  = ppObj != nullptr ? *ppObj : nullptr;
					if ( pObj == nullptr )
					{
						dst.setObject();
						return;
					}
					const TypeInfo* pRuntimeType = ctx.getRuntimeTypeInfo( pObj );
					if ( pRuntimeType == nullptr )
					{
						dst.setObject();
						return;
					}
					dst.setObject();
					JsonValue body = dst.set( pRuntimeType->_name.c_str(), false );
					JsonSerializer::writeObject( body, pObj, *pRuntimeType, ctx );
					body.set( kSchemaVersionKey, false ).setUint( 0 );
					return;
				}

				const TypeInfo* pElemType = findNestedJsonObjectType( nested._elementTypeName, ctx );
				if ( pElemType != nullptr )
				{
					// 값 구조체는 타입 래핑 없이 본문을 그대로 쓴다(리더가 양쪽 다 받는다).
					JsonSerializer::writeObject( dst, pElemPtr, *pElemType, ctx );
					return;
				}

				writeJsonValue( dst, pElemPtr, nested._elementTypeName, ctx );
			}

			/**
			 * @brief 컨테이너를 자연스러운 JSON 표현으로 씁니다. 시퀀스는 배열, 맵은 오브젝트.
			 */
			static void writeContainerValueJson( JsonValue dst, const void* pContainerPtr, const NestedContainerInfo& nested,
												 const SerializeContext& ctx )
			{
				if ( dst.isValid() == false || pContainerPtr == nullptr || nested._wrapper == nullptr )
					return;

				const bool bOwnedPtr = isOwnedPointerElementType( nested._elementTypeName );

				ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
				if ( pSeq != nullptr )
				{
					dst.setArray();
					const size_t sz = pSeq->getSize( pContainerPtr );
					for ( size_t elementIndex = 0; elementIndex < sz; ++elementIndex )
					{
						const void* pElemPtr = pSeq->getElementConst( pContainerPtr, elementIndex );
						if ( bOwnedPtr )
						{
							void* const* ppObj = static_cast<void* const*>( pElemPtr );
							if ( ppObj == nullptr || *ppObj == nullptr )
								continue; // 널 원소는 건너뛴다(레거시 동작 유지).
						}
						writeContainerElementJson( dst.pushBack(), pElemPtr, nested, bOwnedPtr, ctx );
					}
					return;
				}

				IMapContainerWrapper* pMapWrap = nested._wrapper->asMap();
				if ( pMapWrap != nullptr )
				{
					dst.setObject();
					pMapWrap->forEach( pContainerPtr, [&]( const void* pKPtr, const void* pVPtr )
					{
						StringBuilder<constant::kMaxBuffer8192> keySs;
						SerializerUtil::valueToText( keySs, pKPtr, nested._keyTypeName, ctx );
						writeContainerElementJson( dst.set( keySs.view(), false ), pVPtr, nested, false, ctx );
					} );
				}
			}

			// items(JSON 배열)의 각 원소를 시퀀스 컨테이너에 채운다. 호출자가 미리 wrapper->clear() 를 한다.
			static bool readSequenceItemsJson( void* pContainerPtr, const NestedContainerInfo& nested, const JsonValue& items,
											   bool bOwnedPtr, const SerializeContext& ctx )
			{
				ISequenceContainerWrapper* pSeq = nested._wrapper != nullptr ? nested._wrapper->asSequence() : nullptr;
				if ( pSeq == nullptr || items.isArray() == false )
					return false;

				if ( bOwnedPtr )
				{
					bool bOk{ true };
					for ( size_t elementIndex = 0; elementIndex < items.size(); ++elementIndex )
					{
						const JsonValue elem = items.at( elementIndex );
						if ( elem.isObject() == false )
							continue;
						const vector<string> listKey = elem.memberNames();
						if ( listKey.size() != 1 )
							continue;
						const hashed_string typeName( listKey[0].c_str() );
						void*				pObj = ctx.createOwnedPointer( typeName );
						if ( pObj == nullptr )
							continue;
						const TypeInfo* pType = engine::getTypeRegistry().findType( typeName );
						if ( pType == nullptr )
							continue;
						if ( JsonSerializer::readObject( elem.get( listKey[0], false ), pObj, *pType, nullptr, nullptr, ctx ) == false )
							bOk = false;
					}
					return bOk;
				}

				pSeq->reserve( pContainerPtr, items.size() );
				bool bOk{ true };
				for ( size_t elementIndex = 0; elementIndex < items.size(); ++elementIndex )
				{
					pSeq->addElementDefault( pContainerPtr );
					void*			pElemPtr = pSeq->getElement( pContainerPtr, elementIndex );
					const JsonValue elem	 = items.at( elementIndex );
					if ( nested._elementNested != nullptr )
					{
						if ( readTypedContainerJson( pElemPtr, *nested._elementNested, elem, ctx ) == false )
							bOk = false;
					}
					else
					{
						const TypeInfo* pElemType = findNestedJsonObjectType( nested._elementTypeName, ctx );
						if ( pElemType != nullptr )
						{
							if ( elem.isObject() == false )
							{
								bOk = false;
								continue;
							}
							// 래핑 형식 { "TypeName": {body} } 이면 그 안을 읽고, 아니면 elem 자체를 body 로 본다.
							const vector<string> listMember = elem.memberNames();
							const bool			 bWrapped =
								( listMember.size() == 1 &&
								  engine::getTypeRegistry().findType( hashed_string( listMember[0].c_str() ) ) != nullptr );
							const JsonValue body = bWrapped ? elem.get( listMember[0], false ) : elem;
							if ( JsonSerializer::readObject( body, pElemPtr, *pElemType, nullptr, nullptr, ctx ) == false )
								bOk = false;
						}
						else if ( readJsonValue( pElemPtr, nested._elementTypeName, elem, ctx ) == false )
							bOk = false;
					}
				}
				return bOk;
			}

			static bool readNestedContainerJson( void* pContainerPtr, const NestedContainerInfo& nested, const JsonValue& src,
												 const SerializeContext& ctx )
			{
				if ( pContainerPtr == nullptr || nested._wrapper == nullptr || src.isObject() == false )
					return false;

				const bool bOwnedPtr = isOwnedPointerElementType( nested._elementTypeName );
				if ( bOwnedPtr == false )
					nested._wrapper->clear( pContainerPtr );

				const bool				   bIgnore = ctx.ignoreCaseKeys();
				ISequenceContainerWrapper* pSeq	   = nested._wrapper->asSequence();
				if ( pSeq != nullptr )
				{
					const JsonValue items = src.get( kJsonContainerItemKey, bIgnore );
					if ( items.isValid() == false )
						return true;
					if ( items.isArray() == false )
						return false;
					return readSequenceItemsJson( pContainerPtr, nested, items, bOwnedPtr, ctx );
				}

				IMapContainerWrapper* pMapWrap = nested._wrapper->asMap();
				if ( pMapWrap != nullptr )
				{
					const JsonValue entries = src.get( kJsonContainerEntryKey, bIgnore );
					if ( entries.isValid() == false )
						return true;
					if ( entries.isObject() == false )
						return false;
					return readMapEntriesJson( pContainerPtr, nested, entries, ctx );
				}
				return false;
			}

			// entries(JSON 오브젝트)의 각 멤버를 맵 컨테이너에 채운다. 호출자가 미리 wrapper->clear() 를 한다.
			static bool readMapEntriesJson( void* pContainerPtr, const NestedContainerInfo& nested, const JsonValue& entries,
											const SerializeContext& ctx )
			{
				IMapContainerWrapper* pMapWrap = nested._wrapper != nullptr ? nested._wrapper->asMap() : nullptr;
				if ( pMapWrap == nullptr || entries.isObject() == false )
					return false;

				vector<uint8> listKBuf( pMapWrap->getKeySize() );
				vector<uint8> listVBuf( pMapWrap->getValueSize() );
				for ( const string& key : entries.memberNames() )
				{
					pMapWrap->defaultConstructKey( listKBuf.data() );
					pMapWrap->defaultConstructValue( listVBuf.data() );
					JsonDocument keyDoc;
					keyDoc.root().setString( key );
					bool								kOk{ false };
					const SerializeContext::TextReadFn* pKeyReader = ctx.findTextReader( nested._keyTypeName );
					if ( pKeyReader != nullptr )
						kOk = ( *pKeyReader )( listKBuf.data(), key );
					else
						kOk = readJsonValue( listKBuf.data(), nested._keyTypeName, keyDoc.root(), ctx );

					bool			vOk{ false };
					const JsonValue valJson = entries.get( key, false );
					if ( nested._elementNested != nullptr )
						vOk = readTypedContainerJson( listVBuf.data(), *nested._elementNested, valJson, ctx );
					else
					{
						const SerializeContext::TextReadFn* pElemReader = ctx.findTextReader( nested._elementTypeName );
						if ( pElemReader != nullptr )
							vOk = ( *pElemReader )( listVBuf.data(), valJson.isString() ? valJson.asString() : valJson.dump() );
						else
							vOk = readJsonValue( listVBuf.data(), nested._elementTypeName, valJson, ctx );
					}

					if ( kOk && vOk )
						pMapWrap->insertKeyValue( pContainerPtr, listKBuf.data(), listVBuf.data() );
					pMapWrap->destroyKey( listKBuf.data() );
					pMapWrap->destroyValue( listVBuf.data() );
				}
				return true;
			}

			static bool readTypedContainerJson( void* pContainerPtr, const NestedContainerInfo& nested, const JsonValue& src,
												const SerializeContext& ctx )
			{
				// 시퀀스 컨테이너는 평범한 JSON 배열도 받는다(손으로 쓴 Config JSON 등 자연스러운 표현).
				if ( src.isArray() && pContainerPtr != nullptr && nested._wrapper != nullptr &&
					 nested._wrapper->asSequence() != nullptr )
				{
					const bool bOwnedPtr = isOwnedPointerElementType( nested._elementTypeName );
					if ( bOwnedPtr == false )
						nested._wrapper->clear( pContainerPtr );
					return readSequenceItemsJson( pContainerPtr, nested, src, bOwnedPtr, ctx );
				}

				if ( src.isObject() == false )
					return false;

				const utf8* pTypeTag = SerializerUtil::containerTypeTagName( nested._typeName );
				if ( pTypeTag != nullptr )
				{
					const JsonValue group = src.get( pTypeTag, ctx.ignoreCaseKeys() );
					if ( group.isArray() )
					{
						if ( group.size() == 0 )
							return true;
						return readNestedContainerJson( pContainerPtr, nested, group.at( 0 ), ctx );
					}
				}

				const bool bIgnore = ctx.ignoreCaseKeys();
				if ( src.has( kPropertyNameKey, bIgnore ) || src.has( kJsonContainerItemKey, bIgnore ) ||
					 src.has( kJsonContainerEntryKey, bIgnore ) )
					return readNestedContainerJson( pContainerPtr, nested, src, ctx );

				// 맵 컨테이너는 평범한 JSON 오브젝트({"key":value,...})도 받는다.
				if ( pContainerPtr != nullptr && nested._wrapper != nullptr && nested._wrapper->asMap() != nullptr )
				{
					nested._wrapper->clear( pContainerPtr );
					return readMapEntriesJson( pContainerPtr, nested, src, ctx );
				}
				return false;
			}

			static bool readJsonValue( void* pValPtr, const hashed_string& typeName, const JsonValue& src, const SerializeContext& ctx )
			{
				if ( pValPtr == nullptr || src.isValid() == false )
					return false;

				const hashed_string					resolved	= resolveHandlerTypeName( typeName, ctx );
				const SerializeContext::TextReadFn* pTextReader = ctx.findTextReader( resolved );
				if ( pTextReader != nullptr )
				{
					if ( src.isString() )
						return ( *pTextReader )( pValPtr, src.asString() );
					return ( *pTextReader )( pValPtr, src.dump() );
				}

				const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
				if ( pEnumInfo != nullptr )
				{
					if ( src.isString() )
					{
						const int64 v = pEnumInfo->stringFlagsToValue( src.asString() );
						pEnumInfo->writeValueToMemory( pValPtr, v );
						return true;
					}
					if ( src.isNumber() )
					{
						pEnumInfo->writeValueToMemory( pValPtr, src.asInt( 0 ) );
						return true;
					}
				}

				const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
				if ( pStructInfo != nullptr )
				{
					if ( pStructInfo->isPrimitive() == false )
					{
						if ( src.isObject() )
							return JsonSerializer::readObject( src, pValPtr, *pStructInfo, nullptr, nullptr, ctx );
						return JsonSerializer::deserialize( pValPtr, *pStructInfo, src.dump(), ctx );
					}
				}

				if ( src.isString() )
					return parseTextValueCoerced( pValPtr, typeName, src.asString(), ctx );
				return parseTextValueCoerced( pValPtr, typeName, src.dump(), ctx );
			}
			static void writeProperty( JsonValue parent, const PropertyInfo& prop, const void* pInstance, const SerializeContext& ctx )
			{
				if ( prop._bIsBitField == SW_TRUE )
				{
					const bool bVal = prop.getValue<bool>( pInstance );
					parent.set( prop._name.c_str(), false ).setBool( bVal );
					return;
				}
				const void* pPropPtr = prop.getRawPtr( pInstance );
				if ( prop._bIsContainer && prop.hasContainerWrapper() )
				{
					NestedContainerInfo shape = prop.getContainerShape();
					if ( shape._typeName.empty() )
						shape._typeName = prop._typeName;
					// 프로퍼티 이름 아래에 바로 배열/오브젝트로 쓴다.
					writeContainerValueJson( parent.set( prop._name.c_str(), false ), pPropPtr, shape, ctx );
					return;
				}
				writeJsonValue( parent.set( prop._name.c_str(), false ), pPropPtr, prop._typeName, ctx );
			}

			static bool isContainerTypeKey( const vector<PropertyInfo>& listProp, string_view keyRaw, bool bIgnoreCaseKeys )
			{
				for ( const PropertyInfo& prop : listProp )
				{
					if ( prop._bIsContainer == SW_FALSE || prop.hasContainerWrapper() == false )
						continue;
					NestedContainerInfo shape = prop.getContainerShape();
					if ( shape._typeName.empty() )
						shape._typeName = prop._typeName;
					const utf8* pTypeTag = SerializerUtil::containerTypeTagName( shape._typeName );
					if ( pTypeTag != nullptr && SerializerUtil::keysEqual( keyRaw, pTypeTag, bIgnoreCaseKeys ) )
						return true;
				}
				return false;
			}

			static bool readContainerTypeGroup( const JsonValue& group, const vector<PropertyInfo>& listProp, void* pInstance,
												unordered_set<uint32>& uniqueSeen, bool& bFieldError, vector<SchemaOrphanValue>* pOutListOrphan,
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

					const string		nameStr = node.get( kPropertyNameKey, bIgnore ).asString();
					bool				bCaseVariant{ false };
					const PropertyInfo* pMatched = SerializerUtil::matchProperty( listProp, nameStr, bIgnore, bCaseVariant );
					if ( pMatched == nullptr && bCaseVariant )
						continue;

					if ( pMatched == nullptr || pMatched->_bIsContainer == SW_FALSE || pMatched->hasContainerWrapper() == false )
					{
						if ( pOutListOrphan != nullptr )
						{
							SchemaOrphanValue	orphan;
							const hashed_string nameHs( nameStr.c_str() );
							orphan._name	 = nameHs;
							orphan._nameHash = nameHs.getHash();
							orphan._text	 = node.dump();
							pOutListOrphan->push_back( std::move( orphan ) );
						}
						else
							bFieldError = true;
						bOk = false;
						continue;
					}

					uniqueSeen.insert( pMatched->getNameHash() );
					void*				pPropPtr = pMatched->getRawPtr( pInstance );
					NestedContainerInfo shape	 = pMatched->getContainerShape();
					if ( shape._typeName.empty() )
						shape._typeName = pMatched->_typeName;
					if ( readNestedContainerJson( pPropPtr, shape, node, ctx ) == false )
					{
						if ( pOutListOrphan != nullptr )
						{
							SchemaOrphanValue orphan;
							orphan._name	 = pMatched->_name;
							orphan._nameHash = pMatched->getNameHash();
							orphan._text	 = node.dump();
							pOutListOrphan->push_back( std::move( orphan ) );
						}
						else
							bFieldError = true;
						bOk = false;
					}
				}
				return bOk;
			}

			static bool readProperty( const JsonValue& field, const PropertyInfo& prop, void* pInstance, const SerializeContext& ctx )
			{
				if ( prop._bIsBitField == SW_TRUE )
				{
					bool bVal = false;
					if ( field.isBool() )
						bVal = field.asBool();
					else if ( field.isNumber() )
						bVal = ( field.asInt() != 0 );
					else if ( field.isString() )
					{
						bVal = StringUtil::parseBool( field.asString(), false );
					}
					prop.setValue<bool>( pInstance, bVal );
					return true;
				}
				void* pPropPtr = prop.getRawPtr( pInstance );
				if ( prop._bIsContainer && prop.hasContainerWrapper() )
					return readTypedContainerJson( pPropPtr, prop.getContainerShape(), field, ctx );
				return readJsonValue( pPropPtr, prop._typeName, field, ctx );
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "JsonSerializer" );

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

	bool JsonSerializer::serializeToArchive( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
											 bool bPretty, const SerializeContext& ctx )
	{
		const string jsonStr = bPretty ? serializePretty( pInstance, typeInfo, 4, ctx )
									   : serialize( pInstance, typeInfo, ctx );
		if ( jsonStr.empty() )
			return false;

		outArchive << jsonStr;
		return true;
	}

	bool JsonSerializer::deserializeFromArchive( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
												 const SerializeContext& ctx )
	{
		if ( inArchive.isError() )
			return false;

		string jsonStr;
		inArchive >> jsonStr;
		if ( inArchive.isError() || jsonStr.empty() )
			return false;

		return deserialize( pInstance, typeInfo, jsonStr, ctx );
	}

	bool JsonSerializer::saveFile( string_view absPath, const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces,
								   const SerializeContext& ctx )
	{
		JsonDocument doc;
		writeObject( doc.makeObject(), pInstance, typeInfo, ctx );
		const int32 indent = static_cast<int32>( indentSpaces == 0 ? 4 : indentSpaces );
		return doc.saveFile( absPath, indent );
	}

	bool JsonSerializer::loadFile( string_view path, void* pInstance, const TypeInfo& typeInfo, const SerializeContext& ctx )
	{
		JsonDocument doc;
		if ( doc.loadPath( path ) == false )
			return false;
		return readObject( doc.root(), pInstance, typeInfo, nullptr, nullptr, ctx );
	}

	void JsonSerializer::writeObject( JsonValue dst, const void* pInstance, const TypeInfo& typeInfo,
									  const SerializeContext& ctx )
	{
		if ( dst.isValid() == false || pInstance == nullptr )
			return;
		dst.setObject();
		typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
		{
			if ( prop._metadata._bTransient == SW_TRUE )
				return;
			JsonSerializerInternal::writeProperty( dst, prop, pInstance, ctx );
		} );
	}

	bool JsonSerializer::readObject( JsonValue src, void* pInstance, const TypeInfo& typeInfo,
									 vector<SchemaOrphanValue>* pOutListOrphan, uint32* pOutVersion,
									 const SerializeContext& ctx )
	{
		if ( pInstance == nullptr || src.isObject() == false )
			return false;

		const bool					bIgnoreCaseKeys = ctx.ignoreCaseKeys();
		const vector<PropertyInfo>& listProp		= typeInfo.getPropertiesWithBase();
		unordered_set<uint32>		uniqueSeen;
		bool						bFieldError{ false };

		if ( pOutVersion != nullptr )
			*pOutVersion = 0;

		for ( const string& keyRaw : src.memberNames() )
		{
			const JsonValue field = src.get( keyRaw, false );
			if ( SerializerUtil::keysEqual( keyRaw, kSchemaVersionKey, bIgnoreCaseKeys ) )
			{
				if ( pOutVersion != nullptr )
					*pOutVersion = static_cast<uint32>( field.asUint( 0 ) );
				continue;
			}

			if ( JsonSerializerInternal::isContainerTypeKey( listProp, keyRaw, bIgnoreCaseKeys ) )
			{
				if ( JsonSerializerInternal::readContainerTypeGroup( field, listProp, pInstance, uniqueSeen, bFieldError, pOutListOrphan, ctx ) == false &&
					 pOutListOrphan == nullptr )
					bFieldError = true;
				continue;
			}

			bool				bCaseVariant{ false };
			const PropertyInfo* pMatched = SerializerUtil::matchProperty( listProp, keyRaw, bIgnoreCaseKeys, bCaseVariant );
			if ( pMatched == nullptr && bCaseVariant )
				continue;

			if ( pMatched == nullptr || pMatched->_metadata._bTransient == SW_TRUE )
			{
				if ( pOutListOrphan != nullptr )
				{
					SchemaOrphanValue	orphan;
					const hashed_string keyHs( keyRaw.c_str() );
					orphan._name	 = keyHs;
					orphan._nameHash = keyHs.getHash();
					orphan._text	 = field.dump();
					pOutListOrphan->push_back( std::move( orphan ) );
				}
				else
					bFieldError = true;
				continue;
			}

			uniqueSeen.insert( pMatched->getNameHash() );
			if ( JsonSerializerInternal::readProperty( field, *pMatched, pInstance, ctx ) == false )
			{
				if ( pOutListOrphan != nullptr )
				{
					SchemaOrphanValue orphan;
					orphan._name	 = pMatched->_name;
					orphan._nameHash = pMatched->getNameHash();
					orphan._text	 = field.dump();
					pOutListOrphan->push_back( std::move( orphan ) );
				}
				else
					bFieldError = true;
			}
		}

		for ( const PropertyInfo& prop : listProp )
		{
			if ( uniqueSeen.find( prop.getNameHash() ) != uniqueSeen.end() )
				continue;
			SerializerUtil::applyPropertyDefault( prop.getRawPtr( pInstance ), prop, ctx );
		}

		if ( pOutListOrphan != nullptr )
			return true;
		return bFieldError == false;
	}

	bool JsonSerializer::deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
										  vector<SchemaOrphanValue>* pOutListOrphan, uint32* pOutVersion,
										  const SerializeContext& ctx )
	{
		JsonDocument doc;
		if ( doc.parse( jsonStr ) == false )
			return false;
		return readObject( doc.root(), pInstance, typeInfo, pOutListOrphan, pOutVersion, ctx );
	}

	string JsonSerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
											   const SerializeContext& ctx )
	{
		JsonDocument doc;
		JsonValue	 root = doc.makeObject();
		root.set( kSchemaVersionKey, false ).setUint( version );
		typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
		{
			if ( prop._metadata._bTransient == SW_TRUE )
				return;
			JsonSerializerInternal::writeProperty( root, prop, pInstance, ctx );
		} );
		return doc.dump();
	}

	bool JsonSerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
											   string_view jsonStr, uint32 currentVersion, SchemaMigrateFn migrate,
											   const TypeInfo* pLegacyTypeInfo, const SerializeContext& ctx )
	{
		vector<SchemaOrphanValue> listOrphan;
		ScopedScratchInstance	  scratchLegacy( pLegacyTypeInfo );
		void*					  pLegacyPtr = scratchLegacy.get();
		outVersion							 = 0;

		if ( pLegacyTypeInfo != nullptr && pLegacyTypeInfo->_size > 0 )
		{
			uint32 legacyVer{ 0 };
			if ( pLegacyPtr == nullptr ||
				 deserializeSoft( pLegacyPtr, *pLegacyTypeInfo, jsonStr, &listOrphan, &legacyVer, ctx ) == false )
				return false;
			outVersion = legacyVer;
		}

		uint32 softVer{ 0 };
		if ( deserializeSoft( pInstance, typeInfo, jsonStr, &listOrphan, &softVer, ctx ) == false )
			return false;
		if ( pLegacyPtr == nullptr )
			outVersion = softVer;
		else if ( softVer != 0 )
			outVersion = softVer;

		return runSchemaMigrateStep( outVersion, currentVersion, pInstance, typeInfo, pLegacyPtr, pLegacyTypeInfo,
									 listOrphan, migrate, outVersion != currentVersion, ctx );
	}

} // namespace sw
