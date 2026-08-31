#include "pch.h"

#include "Engine/Serialization/Core/SerializerUtil.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
	namespace
	{
		struct SerializerUtilInternal
		{
			/** @brief Alias/옛 이름 → SerializeContext 핸들러용 canonical (_name). */
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
		};
	} // namespace
} // namespace sw

namespace sw
{
	const utf8* SerializerUtil::containerTypeTagName( hashed_string typeName )
	{
		const TypeInfo* pTypeInfo = engine::getTypeRegistry().findType( typeName );
		if ( pTypeInfo != nullptr )
		{
			if ( pTypeInfo->_name.empty() == false )
				return pTypeInfo->_name.c_str();
			if ( pTypeInfo->_fullyQualifiedName.empty() == false )
				return pTypeInfo->_fullyQualifiedName.c_str();
		}
		if ( typeName.empty() == false )
			return typeName.c_str();
		return nullptr;
	}

	void SerializerUtil::serializeValueBinary( const void* pValuePtr, const hashed_string& typeName,
											   vector<uint8>& listBuffer, const SerializeContext& ctx )
	{
		const hashed_string					   resolved = SerializerUtilInternal::resolveHandlerTypeName( typeName, ctx );
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
				const size_t sizePos   = listBuffer.size();
				const uint32 dummySize = 0;
				const uint8* pDummy	   = reinterpret_cast<const uint8*>( &dummySize );
				listBuffer.insert( listBuffer.end(), pDummy, pDummy + sizeof( uint32 ) );

				const size_t structStart = listBuffer.size();
				BinarySerializer::serialize( pValuePtr, *pStructInfo, listBuffer, ctx );
				const uint32 structSize = static_cast<uint32>( listBuffer.size() - structStart );

				Memory::copy( listBuffer.data() + sizePos, &structSize, sizeof( uint32 ) );
				return;
			}
		}

		const SerializeContext::TextWriteFn* pTextWriter = ctx.findTextWriter( resolved );
		if ( pTextWriter != nullptr )
		{
			string		 str	= ( *pTextWriter )( pValuePtr );
			const uint32 size	= static_cast<uint32>( str.size() );
			const uint8* pBytes = reinterpret_cast<const uint8*>( &size );
			listBuffer.insert( listBuffer.end(), pBytes, pBytes + sizeof( uint32 ) );
			listBuffer.insert( listBuffer.end(), str.begin(), str.end() );
			return;
		}

		listBuffer.push_back( 0 );
	}

	bool SerializerUtil::deserializeValueBinary( void* pValuePtr, const hashed_string& typeName,
												 const uint8* pData, size_t dataSize, size_t& offset,
												 const SerializeContext& ctx )
	{
		const hashed_string					  resolved = SerializerUtilInternal::resolveHandlerTypeName( typeName, ctx );
		const SerializeContext::BinaryReadFn* pReader  = ctx.findBinaryReader( resolved );
		if ( pReader != nullptr )
			return ( *pReader )( pValuePtr, pData, dataSize, offset );

		const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
		if ( pEnumInfo != nullptr )
		{
			(void)pEnumInfo;
			if ( offset + sizeof( int64 ) > dataSize )
				return false;
			int64 val{ 0 };
			Memory::copy( &val, pData + offset, sizeof( int64 ) );
			*static_cast<int64*>( pValuePtr ) = val;
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
				uint32 size{ 0 };
				Memory::copy( &size, pData + offset, sizeof( uint32 ) );
				offset += sizeof( uint32 );
				if ( offset + size > dataSize )
					return false;
				const bool ok = BinarySerializer::deserialize( pValuePtr, *pStructInfo, pData + offset, size, ctx );
				offset += size;
				return ok;
			}
		}

		const SerializeContext::TextReadFn* pTextReader = ctx.findTextReader( resolved );
		if ( pTextReader != nullptr )
		{
			if ( offset + sizeof( uint32 ) > dataSize )
				return false;
			uint32 size{ 0 };
			Memory::copy( &size, pData + offset, sizeof( uint32 ) );
			offset += sizeof( uint32 );
			if ( offset + size > dataSize )
				return false;
			string str( reinterpret_cast<const utf8*>( pData + offset ), size );
			offset += size;
			return ( *pTextReader )( pValuePtr, str );
		}

		if ( offset < dataSize )
		{
			++offset;
			return true;
		}
		return false;
	}

	void SerializerUtil::serializeNestedContainerBinary( const void* pContainerPtr, const NestedContainerInfo& nested,
														 vector<uint8>& listBuffer, const SerializeContext& ctx )
	{
		if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
			return;

		ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
		if ( pSeq != nullptr )
		{
			const size_t sz	   = pSeq->getSize( pContainerPtr );
			const uint32 count = static_cast<uint32>( sz );
			const uint8* pB	   = reinterpret_cast<const uint8*>( &count );
			listBuffer.insert( listBuffer.end(), pB, pB + sizeof( uint32 ) );

			for ( size_t elemIndex = 0; elemIndex < sz; ++elemIndex )
			{
				const void* pElem = pSeq->getElementConst( pContainerPtr, elemIndex );
				if ( nested._elementNested != nullptr )
					SerializerUtil::serializeNestedContainerBinary( pElem, *nested._elementNested, listBuffer, ctx );
				else
					SerializerUtil::serializeValueBinary( pElem, nested._elementTypeName, listBuffer, ctx );
			}
			return;
		}

		IMapContainerWrapper* pMapWrap = nested._wrapper->asMap();
		if ( pMapWrap != nullptr )
		{
			const size_t sz	   = pMapWrap->getSize( pContainerPtr );
			const uint32 count = static_cast<uint32>( sz );
			const uint8* pB	   = reinterpret_cast<const uint8*>( &count );
			listBuffer.insert( listBuffer.end(), pB, pB + sizeof( uint32 ) );

			pMapWrap->forEach( pContainerPtr, [&]( const void* pKey, const void* pVal )
			{
				SerializerUtil::serializeValueBinary( pKey, nested._keyTypeName, listBuffer, ctx );
				if ( nested._elementNested != nullptr )
					SerializerUtil::serializeNestedContainerBinary( pVal, *nested._elementNested, listBuffer, ctx );
				else
					SerializerUtil::serializeValueBinary( pVal, nested._elementTypeName, listBuffer, ctx );
			} );
		}
	}

	bool SerializerUtil::deserializeNestedContainerBinary( void* pContainerPtr, const NestedContainerInfo& nested,
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

		if ( ( dataSize - offset ) < count )
			return false;

		ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
		if ( pSeq != nullptr )
		{
			pSeq->reserve( pContainerPtr, MathUtil::min( count, static_cast<uint32>( invalid_index::kUint16 ) ) );
			for ( uint32 elemIndex = 0; elemIndex < count; ++elemIndex )
			{
				pSeq->addElementDefault( pContainerPtr );
				void* pElem = pSeq->getElement( pContainerPtr, elemIndex );
				if ( nested._elementNested != nullptr )
				{
					if ( SerializerUtil::deserializeNestedContainerBinary( pElem, *nested._elementNested, pData, dataSize, offset, ctx ) == false )
						return false;
				}
				else if ( SerializerUtil::deserializeValueBinary( pElem, nested._elementTypeName, pData, dataSize, offset, ctx ) == false )
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
				bool ok = SerializerUtil::deserializeValueBinary( listKBuf.data(), nested._keyTypeName, pData, dataSize, offset, ctx );
				if ( ok )
				{
					if ( nested._elementNested != nullptr )
						ok = SerializerUtil::deserializeNestedContainerBinary( listVBuf.data(), *nested._elementNested, pData, dataSize, offset, ctx );
					else
						ok = SerializerUtil::deserializeValueBinary( listVBuf.data(), nested._elementTypeName, pData, dataSize, offset, ctx );
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

	void SerializerUtil::valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
									  const SerializeContext& ctx )
	{
		const hashed_string					 resolved	 = SerializerUtilInternal::resolveHandlerTypeName( typeName, ctx );
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

	bool SerializerUtil::parseTextValue( void* pValPtr, const hashed_string& typeName, string_view valStr,
										 const SerializeContext& ctx )
	{
		const hashed_string					resolved	= SerializerUtilInternal::resolveHandlerTypeName( typeName, ctx );
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

	bool SerializerUtil::applyPropertyDefault( void* pPropPtr, const PropertyInfo& prop, const SerializeContext& ctx )
	{
		if ( pPropPtr == nullptr || prop._bIsContainer != 0 )
			return false;
		if ( prop._metadata._defaultValue.empty() )
			return false;
		return SerializerUtil::parseTextValue( pPropPtr, prop._typeName, prop._metadata._defaultValue, ctx );
	}

	bool SerializerUtil::keysEqual( string_view a, string_view b, bool bIgnoreCase )
	{
		return StringUtil::equals( a, b, bIgnoreCase );
	}

	const PropertyInfo* SerializerUtil::matchProperty( const vector<PropertyInfo>& listProp, string_view keyRaw,
													   bool bIgnoreCaseKeys, bool& bCaseVariant )
	{
		const PropertyInfo* pMatched = nullptr;
		bCaseVariant				 = false;
		for ( const PropertyInfo& prop : listProp )
		{
			if ( SerializerUtil::keysEqual( keyRaw, prop._name.c_str(), bIgnoreCaseKeys ) )
				return &prop;
			bCaseVariant = bCaseVariant || SerializerUtil::keysEqual( keyRaw, prop._name.c_str(), true );
			for ( const hashed_string& alias : prop._listAlias )
			{
				if ( alias.empty() )
					continue;
				if ( SerializerUtil::keysEqual( keyRaw, alias.c_str(), bIgnoreCaseKeys ) )
					return &prop;
				bCaseVariant = bCaseVariant || SerializerUtil::keysEqual( keyRaw, alias.c_str(), true );
			}
		}
		return pMatched;
	}

	bool SerializerUtil::transcodeJsonToBinary( string_view jsonStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
												const SerializeContext& ctx )
	{
		if ( jsonStr.empty() || typeInfo._size == 0 )
			return false;

		ScopedScratchInstance scratch( typeInfo );
		if ( scratch.isValid() == false )
			return false;

		if ( JsonSerializer::deserialize( scratch.get(), typeInfo, jsonStr, ctx ) == false )
			return false;

		BinarySerializer::serialize( scratch.get(), typeInfo, outBinary, ctx );
		return true;
	}

	string SerializerUtil::transcodeBinaryToJson( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty,
												  const SerializeContext& ctx )
	{
		if ( pData == nullptr || dataSize == 0 || typeInfo._size == 0 )
			return {};

		ScopedScratchInstance scratch( typeInfo );
		if ( scratch.isValid() == false )
			return {};

		if ( BinarySerializer::deserialize( scratch.get(), typeInfo, pData, dataSize, ctx ) == false )
			return {};

		return bPretty ? JsonSerializer::serializePretty( scratch.get(), typeInfo, 4, ctx )
					   : JsonSerializer::serialize( scratch.get(), typeInfo, ctx );
	}

	bool SerializerUtil::transcodeXmlToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
											   const SerializeContext& ctx )
	{
		if ( xmlStr.empty() || typeInfo._size == 0 )
			return false;

		ScopedScratchInstance scratch( typeInfo );
		if ( scratch.isValid() == false )
			return false;

		if ( XmlSerializer::deserialize( scratch.get(), typeInfo, xmlStr, ctx ) == false )
			return false;

		BinarySerializer::serialize( scratch.get(), typeInfo, outBinary, ctx );
		return true;
	}

	string SerializerUtil::transcodeBinaryToXml( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
												 const SerializeContext& ctx )
	{
		if ( pData == nullptr || dataSize == 0 || typeInfo._size == 0 )
			return {};

		ScopedScratchInstance scratch( typeInfo );
		if ( scratch.isValid() == false )
			return {};

		if ( BinarySerializer::deserialize( scratch.get(), typeInfo, pData, dataSize, ctx ) == false )
			return {};

		return XmlSerializer::serialize( scratch.get(), typeInfo, ctx );
	}

} // namespace sw
