#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerInternal.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{

	namespace
	{
		/** @brief Alias/옛 이름 → SerializeContext 핸들러용 canonical (_name). */
		hashed_string resolveHandlerTypeName( const hashed_string& typeName, const SerializeContext& ctx )
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

	} // namespace


	void serializeValueBinary( const void* pValuePtr, const hashed_string& typeName,
							   vector<uint8>& listBuffer, const SerializeContext& ctx )
	{
		const hashed_string					   resolved = resolveHandlerTypeName( typeName, ctx );
		const SerializeContext::BinaryWriteFn* pWriter	= ctx.findBinaryWriter( resolved );
		if ( pWriter != nullptr )
		{
			( *pWriter )( pValuePtr, listBuffer );
			return;
		}

		const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
		if ( pEnumInfo != nullptr )
		{
			(void)pEnumInfo;
			int64		 val = *static_cast<const int64*>( pValuePtr );
			const uint8* pB	 = reinterpret_cast<const uint8*>( &val );
			listBuffer.insert( listBuffer.end(), pB, pB + sizeof( int64 ) );
			return;
		}

		const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
		if ( pStructInfo != nullptr )
		{
			if ( pStructInfo->isPrimitive() == false )
			{
				// Size-prefixed blob so nested structs inside containers can advance offset.
				vector<uint8> listNested;
				BinarySerializer::serialize( pValuePtr, *pStructInfo, listNested, ctx );
				const uint32 payloadSize = static_cast<uint32>( listNested.size() );
				const uint8* pSizeBytes	 = reinterpret_cast<const uint8*>( &payloadSize );
				listBuffer.insert( listBuffer.end(), pSizeBytes, pSizeBytes + sizeof( uint32 ) );
				listBuffer.insert( listBuffer.end(), listNested.begin(), listNested.end() );
			}
		}
	}

	bool deserializeValueBinary( void* pValuePtr, const hashed_string& typeName,
								 const uint8* pData, size_t dataSize, size_t& offset,
								 const SerializeContext& ctx )
	{
		const hashed_string					  resolved = resolveHandlerTypeName( typeName, ctx );
		const SerializeContext::BinaryReadFn* pReader  = ctx.findBinaryReader( resolved );
		if ( pReader != nullptr )
			return ( *pReader )( pValuePtr, pData, dataSize, offset );

		const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
		if ( pEnumInfo != nullptr )
		{
			(void)pEnumInfo;
			if ( offset + sizeof( int64 ) > dataSize )
				return false;
			Memory::copy( pValuePtr, pData + offset, sizeof( int64 ) );
			offset += sizeof( int64 );
			return true;
		}

		const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
		if ( pStructInfo != nullptr )
		{
			if ( pStructInfo->isPrimitive() == false )
			{
				if ( offset + sizeof( uint32 ) > dataSize )
					return false;
				uint32 payloadSize{ 0 };
				Memory::copy( &payloadSize, pData + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );
				if ( offset + payloadSize > dataSize )
					return false;
				const bool ok = BinarySerializer::deserialize( pValuePtr, *pStructInfo, pData + offset, payloadSize, ctx );
				offset += payloadSize;
				return ok;
			}
		}

		return false;
	}

	void serializeNestedContainerBinary( const void* pContainerPtr, const NestedContainerInfo& nested,
										 vector<uint8>& listBuffer, const SerializeContext& ctx )
	{
		if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
			return;

		ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
		if ( pSeq != nullptr )
		{
			const uint32 count		 = static_cast<uint32>( pSeq->getSize( pContainerPtr ) );
			const uint8* pCountBytes = reinterpret_cast<const uint8*>( &count );
			listBuffer.insert( listBuffer.end(), pCountBytes, pCountBytes + sizeof( uint32 ) );
			for ( uint32 elemIndex = 0; elemIndex < count; ++elemIndex )
			{
				const void* pElem = pSeq->getElementConst( pContainerPtr, elemIndex );
				if ( nested._elementNested != nullptr )
					serializeNestedContainerBinary( pElem, *nested._elementNested, listBuffer, ctx );
				else
					serializeValueBinary( pElem, nested._elementTypeName, listBuffer, ctx );
			}
			return;
		}

		IMapContainerWrapper* pMapWrap = nested._wrapper->asMap();
		if ( pMapWrap != nullptr )
		{
			const uint32 count		 = static_cast<uint32>( pMapWrap->getSize( pContainerPtr ) );
			const uint8* pCountBytes = reinterpret_cast<const uint8*>( &count );
			listBuffer.insert( listBuffer.end(), pCountBytes, pCountBytes + sizeof( uint32 ) );
			pMapWrap->forEach( pContainerPtr, [&]( const void* pKPtr, const void* pVPtr )
			{
				serializeValueBinary( pKPtr, nested._keyTypeName, listBuffer, ctx );
				if ( nested._elementNested != nullptr )
					serializeNestedContainerBinary( pVPtr, *nested._elementNested, listBuffer, ctx );
				else
					serializeValueBinary( pVPtr, nested._elementTypeName, listBuffer, ctx );
			} );
		}
	}

	bool deserializeNestedContainerBinary( void* pContainerPtr, const NestedContainerInfo& nested,
										   const uint8* pData, size_t dataSize, size_t& offset,
										   const SerializeContext& ctx )
	{
		if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
			return false;
		nested._wrapper->clear( pContainerPtr );

		if ( offset + sizeof( uint32 ) > dataSize )
			return false;
		uint32 count{ 0 };
		Memory::copy( &count, pData + offset, sizeof( uint32 ) );
		offset += sizeof( uint32 );

		ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
		if ( pSeq != nullptr )
		{
			pSeq->reserve( pContainerPtr, count );
			for ( uint32 elemIndex = 0; elemIndex < count; ++elemIndex )
			{
				pSeq->addElementDefault( pContainerPtr );
				void* pElem = pSeq->getElement( pContainerPtr, elemIndex );
				if ( nested._elementNested != nullptr )
				{
					if ( deserializeNestedContainerBinary( pElem, *nested._elementNested, pData, dataSize, offset, ctx ) == false )
						return false;
				}
				else if ( deserializeValueBinary( pElem, nested._elementTypeName, pData, dataSize, offset, ctx ) == false )
					return false;
			}
			return true;
		}

		IMapContainerWrapper* pMapWrap = nested._wrapper->asMap();
		if ( pMapWrap != nullptr )
		{
			vector<uint8> listKBuf( pMapWrap->getKeySize() );
			vector<uint8> listVBuf( pMapWrap->getValueSize() );
			for ( uint32 entryIndex = 0; entryIndex < count; ++entryIndex )
			{
				pMapWrap->defaultConstructKey( listKBuf.data() );
				pMapWrap->defaultConstructValue( listVBuf.data() );
				bool ok = deserializeValueBinary( listKBuf.data(), nested._keyTypeName, pData, dataSize, offset, ctx );
				if ( ok )
				{
					if ( nested._elementNested != nullptr )
						ok = deserializeNestedContainerBinary( listVBuf.data(), *nested._elementNested, pData, dataSize, offset, ctx );
					else
						ok = deserializeValueBinary( listVBuf.data(), nested._elementTypeName, pData, dataSize, offset, ctx );
				}
				if ( ok )
					pMapWrap->insertKeyValue( pContainerPtr, listKBuf.data(), listVBuf.data() );
				pMapWrap->destroyKey( listKBuf.data() );
				pMapWrap->destroyValue( listVBuf.data() );
				if ( ok == false )
					return false;
			}
			return true;
		}
		return false;
	}

	bool parseTextValue( void* pValPtr, const hashed_string& typeName, string_view valStr,
						 const SerializeContext& ctx )
	{
		const hashed_string					resolved	= resolveHandlerTypeName( typeName, ctx );
		const SerializeContext::TextReadFn* pTextReader = ctx.findTextReader( resolved );
		if ( pTextReader != nullptr )
			return ( *pTextReader )( pValPtr, valStr );

		const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
		if ( pEnumInfo != nullptr )
		{
			string_view sv = valStr;
			if ( sv.size() >= 2 && sv.front() == '"' && sv.back() == '"' )
				sv = sv.substr( 1, sv.size() - 2 );
			const int64 v					= pEnumInfo->stringFlagsToValue( sv );
			*static_cast<int64*>( pValPtr ) = v;
			return true;
		}

		const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
		if ( pStructInfo != nullptr )
		{
			if ( pStructInfo->isPrimitive() == false )
			{
				string_view sv = StringUtil::trim( valStr );
				if ( sv.size() >= 2 && sv.front() == '"' && sv.back() == '"' )
				{
					const string unescaped = JsonDocument::unescapeString( sv.substr( 1, sv.size() - 2 ) );
					return JsonSerializer::deserialize( pValPtr, *pStructInfo, unescaped, ctx );
				}
				return JsonSerializer::deserialize( pValPtr, *pStructInfo, sv, ctx );
			}
		}

		return false;
	}

	/** @brief PROPERTY(Default="...") when the asset omitted this scalar field. */
	bool applyPropertyDefault( void* pPropPtr, const PropertyInfo& prop, const SerializeContext& ctx )
	{
		if ( pPropPtr == nullptr || prop._bIsContainer != 0 )
			return false;
		if ( prop._metadata._defaultValue.empty() )
			return false;
		return parseTextValue( pPropPtr, prop._typeName, prop._metadata._defaultValue, ctx );
	}

	void valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
					  const SerializeContext& ctx )
	{
		const hashed_string					 resolved	 = resolveHandlerTypeName( typeName, ctx );
		const SerializeContext::TextWriteFn* pTextWriter = ctx.findTextWriter( resolved );
		if ( pTextWriter != nullptr )
		{
			ss.append( ( *pTextWriter )( pValPtr ).c_str() );
			return;
		}

		const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
		if ( pEnumInfo != nullptr )
		{
			const int64 val = *static_cast<const int64*>( pValPtr );
			if ( pEnumInfo->_bIsBitFlag )
				ss.append( pEnumInfo->toStringFlags( val ).c_str() );
			else
				ss.append( pEnumInfo->toString( val ).c_str() );
			return;
		}

		const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
		if ( pStructInfo != nullptr )
		{
			if ( pStructInfo->isPrimitive() == false )
			{
				ss.append( JsonSerializer::serialize( pValPtr, *pStructInfo, ctx ) );
				return;
			}
		}

		ss.append( "null" );
	}

	void valueToJson( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
					  const SerializeContext& ctx )
	{
		JsonDocument doc;
		writeJsonValue( doc.root(), pValPtr, typeName, ctx );
		ss.append( doc.dump().c_str() );
	}

	void writeJsonValue( JsonValue dst, const void* pValPtr, const hashed_string& typeName, const SerializeContext& ctx )
	{
		if ( dst.isValid() == false )
			return;

		const hashed_string resolved	= resolveHandlerTypeName( typeName, ctx );
		const bool			bIsString	= ( resolved == hashed_string( "string" ) ||
								resolved == hashed_string( "sw::string" ) ||
								resolved == hashed_string( "hashed_string" ) ||
								resolved == hashed_string( "sw::hashed_string" ) ||
								engine::getTypeRegistry().isType( resolved, "string" ) ||
								engine::getTypeRegistry().isType( resolved, "hashed_string" ) );
		const bool			bIsBool		= ( resolved == hashed_string( "bool" ) || resolved == hashed_string( "sw::bool" ) );
		const bool			bIsEnum		= ( engine::getTypeRegistry().findEnum( typeName ) != nullptr );

		if ( bIsString || bIsEnum )
		{
			StringBuilder<constant::kMaxBuffer8192> text;
			valueToText( text, pValPtr, typeName, ctx );
			dst.setString( text.view() );
			return;
		}

		if ( bIsBool )
		{
			StringBuilder<constant::kMaxBuffer8192> text;
			valueToText( text, pValPtr, typeName, ctx );
			dst.setBool( text.view() == "true" || text.view() == "1" );
			return;
		}

		if ( ctx.findTextWriter( resolved ) != nullptr )
		{
			StringBuilder<constant::kMaxBuffer8192> text;
			valueToText( text, pValPtr, typeName, ctx );
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
		valueToText( text, pValPtr, typeName, ctx );
		dst.setString( text.view() );
	}

	void appendNestedContainerJson( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pContainerPtr,
									const NestedContainerInfo& nested, const SerializeContext& ctx )
	{
		JsonDocument doc;
		writeNestedContainerJson( doc.root(), pContainerPtr, nested, ctx );
		ss.append( doc.dump().c_str() );
	}

	void writeNestedContainerJson( JsonValue dst, const void* pContainerPtr, const NestedContainerInfo& nested,
								   const SerializeContext& ctx )
	{
		if ( pContainerPtr == nullptr || nested._wrapper == nullptr || dst.isValid() == false )
			return;

		ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
		if ( pSeq != nullptr )
		{
			dst.setArray();
			const size_t sz = pSeq->getSize( pContainerPtr );
			for ( size_t elementIndex = 0; elementIndex < sz; ++elementIndex )
			{
				const void* pElemPtr = pSeq->getElementConst( pContainerPtr, elementIndex );
				if ( nested._elementNested != nullptr )
					writeNestedContainerJson( dst.pushBack(), pElemPtr, *nested._elementNested, ctx );
				else
					writeJsonValue( dst.pushBack(), pElemPtr, nested._elementTypeName, ctx );
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
				valueToText( keySs, pKPtr, nested._keyTypeName, ctx );
				if ( nested._elementNested != nullptr )
					writeNestedContainerJson( dst.set( keySs.view(), false ), pVPtr, *nested._elementNested, ctx );
				else
					writeJsonValue( dst.set( keySs.view(), false ), pVPtr, nested._elementTypeName, ctx );
			} );
		}
	}

	bool parseNestedContainerFromJson( void* pContainerPtr, const NestedContainerInfo& nested,
									   string_view json, const SerializeContext& ctx )
	{
		JsonDocument doc;
		if ( doc.parse( json ) == false )
			return false;
		return readNestedContainerJson( pContainerPtr, nested, doc.root(), ctx );
	}

	bool readNestedContainerJson( void* pContainerPtr, const NestedContainerInfo& nested, const JsonValue& src,
								  const SerializeContext& ctx )
	{
		if ( pContainerPtr == nullptr || nested._wrapper == nullptr || src.isValid() == false )
			return false;

		nested._wrapper->clear( pContainerPtr );

		ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
		if ( pSeq != nullptr )
		{
			if ( src.isArray() == false )
				return false;
			pSeq->reserve( pContainerPtr, src.size() );
			bool bOk{ true };
			for ( size_t elementIndex = 0; elementIndex < src.size(); ++elementIndex )
			{
				pSeq->addElementDefault( pContainerPtr );
				void* pElemPtr = pSeq->getElement( pContainerPtr, elementIndex );
				if ( nested._elementNested != nullptr )
				{
					if ( readNestedContainerJson( pElemPtr, *nested._elementNested, src.at( elementIndex ), ctx ) == false )
						bOk = false;
				}
				else if ( readJsonValue( pElemPtr, nested._elementTypeName, src.at( elementIndex ), ctx ) == false )
					bOk = false;
			}
			return bOk;
		}

		IMapContainerWrapper* pMapWrap = nested._wrapper->asMap();
		if ( pMapWrap != nullptr )
		{
			if ( src.isObject() == false )
				return false;
			vector<uint8> listKBuf( pMapWrap->getKeySize() );
			vector<uint8> listVBuf( pMapWrap->getValueSize() );
			for ( const string& key : src.memberNames() )
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

				bool vOk{ false };
				if ( nested._elementNested != nullptr )
					vOk = readNestedContainerJson( listVBuf.data(), *nested._elementNested, src.get( key, false ), ctx );
				else
				{
					const JsonValue						valJson		= src.get( key, false );
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
		return false;
	}

	bool readJsonValue( void* pValPtr, const hashed_string& typeName, const JsonValue& src, const SerializeContext& ctx )
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
				const int64 v					= pEnumInfo->stringFlagsToValue( src.asString() );
				*static_cast<int64*>( pValPtr ) = v;
				return true;
			}
			if ( src.isNumber() )
			{
				*static_cast<int64*>( pValPtr ) = src.asInt( 0 );
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

	bool keysEqual( string_view a, string_view b, bool bIgnoreCase )
	{
		if ( a.size() != b.size() )
			return false;
		if ( bIgnoreCase == false )
			return a == b;
		return StringUtil::strnicmp( a.data(), b.data(), static_cast<uint32>( a.size() ) ) == 0;
	}

	const PropertyInfo* matchProperty( const vector<PropertyInfo>& listProps, string_view keyRaw,
									   bool bIgnoreCaseKeys, bool& bCaseVariant )
	{
		const PropertyInfo* pMatched = nullptr;
		bCaseVariant				 = false;
		for ( const PropertyInfo& prop : listProps )
		{
			if ( keysEqual( keyRaw, prop._name.c_str(), bIgnoreCaseKeys ) )
				return &prop;
			bCaseVariant = bCaseVariant || keysEqual( keyRaw, prop._name.c_str(), true );
			for ( const hashed_string& alias : prop._listAliases )
			{
				if ( alias.empty() )
					continue;
				if ( keysEqual( keyRaw, alias.c_str(), bIgnoreCaseKeys ) )
					return &prop;
				bCaseVariant = bCaseVariant || keysEqual( keyRaw, alias.c_str(), true );
			}
		}
		return pMatched;
	}

} // namespace sw
