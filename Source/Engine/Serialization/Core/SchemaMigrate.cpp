#include "pch.h"

#include "Engine/Serialization/Core/SchemaMigrate.h"

#include "Core/Concurrency/Atomic.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SerializerInternal.h"

namespace sw
{

	namespace
	{
		bool constructWithDefaultCtor( void* pPtr, const TypeInfo& typeInfo )
		{
			const FunctionInfo* pCtor = typeInfo.findMethod( hashed_string( "$ctor" ) );
			if ( pCtor == nullptr || pCtor->_invoker.isBound() == false )
				return false;
			pCtor->_invoker( pPtr, TaskArgs{} );
			return true;
		}

		bool destroyIfDefaultConstructed( void* pPtr, const TypeInfo& typeInfo )
		{
			const FunctionInfo* pCtor = typeInfo.findMethod( hashed_string( "$ctor" ) );
			if ( pCtor == nullptr || pCtor->_invoker.isBound() == false || typeInfo._destroyInstance == nullptr )
				return false;
			typeInfo._destroyInstance( pPtr );
			return true;
		}

	} // namespace

	void* createScratchInstance( const TypeInfo& typeInfo, vector<uint8>& listStorage )
	{
		if ( typeInfo._size == 0 )
			return nullptr;
		listStorage.assign( typeInfo._size, 0 );
		void* pBase = listStorage.data();

		if ( constructWithDefaultCtor( pBase, typeInfo ) )
			return pBase;

		for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
		{
			void* pPropPtr = prop.getValuePtr<void>( pBase );
			if ( pPropPtr == nullptr )
				continue;
			NestedContainerInfo shape = prop.getContainerShape();
			if ( shape._wrapper != nullptr )
			{
				shape._wrapper->constructEmpty( pPropPtr );
			}
			else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_string ) )
				sw_placement_new( pPropPtr ) string();
			else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_hashed_string ) )
				sw_placement_new( pPropPtr ) hashed_string();
			else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_AtomicBool ) )
				sw_placement_new( pPropPtr ) AtomicBool();
			else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_TagID ) )
				sw_placement_new( pPropPtr ) TagID();
			else
			{
				const TypeInfo* pNested = engine::getTypeRegistry().findType( prop._typeName );
				if ( pNested != nullptr )
					constructWithDefaultCtor( pPropPtr, *pNested );
			}
		}
		return pBase;
	}

	void destroyScratchInstance( void* pInstance, const TypeInfo& typeInfo )
	{
		if ( pInstance == nullptr )
			return;
		if ( destroyIfDefaultConstructed( pInstance, typeInfo ) )
			return;
		for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
		{
			void* pPropPtr = prop.getValuePtr<void>( pInstance );
			if ( pPropPtr == nullptr )
				continue;
			NestedContainerInfo shape = prop.getContainerShape();
			if ( shape._wrapper != nullptr )
			{
				shape._wrapper->destroyContainer( pPropPtr );
			}
			else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_string ) )
				std::destroy_at( static_cast<string*>( pPropPtr ) );
			else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_hashed_string ) )
				std::destroy_at( static_cast<hashed_string*>( pPropPtr ) );
			else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_AtomicBool ) )
				std::destroy_at( static_cast<AtomicBool*>( pPropPtr ) );
			else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_TagID ) )
				std::destroy_at( static_cast<TagID*>( pPropPtr ) );
			else
			{
				const TypeInfo* pNested = engine::getTypeRegistry().findType( prop._typeName );
				if ( pNested != nullptr )
					destroyIfDefaultConstructed( pPropPtr, *pNested );
			}
		}
	}

	namespace
	{
		string_view stripJsonQuotes( string_view s )
		{
			if ( s.size() >= 2 && s.front() == '"' && s.back() == '"' )
			{
				s.remove_prefix( 1 );
				s.remove_suffix( 1 );
			}
			return s;
		}

		bool isStringType( hashed_string typeName )
		{
			return typeName.isPredefinedType( PredefinedNameType::NameType_string ) ||
				   typeName.isPredefinedType( PredefinedNameType::NameType_hashed_string );
		}

		hashed_string resolveWireTypeHash( uint32 wireTypeHash )
		{
			return engine::getTypeRegistry().canonicalTypeNameByHash( wireTypeHash );
		}

		bool isNumericTypeName( hashed_string typeName )
		{
			const TypeInfo* pInfo = engine::getTypeRegistry().findType( typeName );
			if ( pInfo == nullptr || pInfo->isPrimitive() == false )
				return false;
			return typeName.isPredefinedType( PredefinedNameType::NameType_string ) == false &&
				   typeName.isPredefinedType( PredefinedNameType::NameType_hashed_string ) == false &&
				   typeName.isPredefinedType( PredefinedNameType::NameType_float2 ) == false &&
				   typeName.isPredefinedType( PredefinedNameType::NameType_float3 ) == false &&
				   typeName.isPredefinedType( PredefinedNameType::NameType_float4 ) == false &&
				   typeName.isPredefinedType( PredefinedNameType::NameType_float4x4 ) == false &&
				   typeName.isPredefinedType( PredefinedNameType::NameType_quaternion ) == false;
		}

		template <typename T>
		bool readPod( const uint8* pPayload, size_t payloadSize, T& out )
		{
			if ( payloadSize != sizeof( T ) )
				return false;
			Memory::copy( &out, pPayload, sizeof( T ) );
			return true;
		}

		bool formatPodToString( const uint8* pPayload, size_t payloadSize, string& out )
		{
			if ( payloadSize == sizeof( int32 ) )
			{
				int32 v{ 0 };
				readPod( pPayload, payloadSize, v );
				out = sw::to_string( v );
				return true;
			}
			if ( payloadSize == sizeof( int64 ) )
			{
				int64 v{ 0 };
				readPod( pPayload, payloadSize, v );
				out = sw::to_string( v );
				return true;
			}
			if ( payloadSize == sizeof( float32 ) )
			{
				float32 v{ 0 };
				readPod( pPayload, payloadSize, v );
				out = sw::to_string( v );
				return true;
			}
			return false;
		}

		vector<string> splitPath( const utf8* pDottedPath )
		{
			vector<string> listParts;
			if ( pDottedPath == nullptr || pDottedPath[0] == '\0' )
				return listParts;
			string_splitter splitter( pDottedPath, { "." } );
			for ( string_view token : splitter.getSplitList() )
			{
				string_view t = StringUtil::trim( token );
				if ( t.empty() == false )
					listParts.push_back( string{ t } );
			}
			return listParts;
		}

	} // namespace

	const SchemaOrphanValue* SchemaMigrateContext::findOrphan( hashed_string name ) const
	{
		if ( _pOrphans == nullptr )
			return nullptr;
		for ( const SchemaOrphanValue& orphanValue : *_pOrphans )
		{
			if ( orphanValue._name == name )
				return &orphanValue;
		}
		return nullptr;
	}

	const SchemaOrphanValue* SchemaMigrateContext::findOrphanHash( uint32 nameHash ) const
	{
		if ( _pOrphans == nullptr || nameHash == 0 )
			return nullptr;
		for ( const SchemaOrphanValue& orphanValue : *_pOrphans )
		{
			if ( orphanValue._nameHash == nameHash )
				return &orphanValue;
		}
		return nullptr;
	}

	bool SchemaMigrateContext::applyOrphanTo( hashed_string propName, hashed_string wireTypeHint ) const
	{
		const SchemaOrphanValue* pOrphan = findOrphan( propName );
		if ( pOrphan == nullptr )
			pOrphan = findOrphanHash( propName.getHash() );
		if ( pOrphan == nullptr || _pInstance == nullptr || _pTypeInfo == nullptr )
			return false;

		void*				pPtr{ nullptr };
		const PropertyInfo* pProp = nullptr;
		if ( resolvePropertyPath( _pInstance, *_pTypeInfo, propName.c_str(), pPtr, pProp ) == false )
			return false;

		const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();

		if ( pOrphan->_text.empty() == false )
			return parseTextValueCoerced( pPtr, pProp->_typeName, pOrphan->_text, ctx );

		if ( pOrphan->_listBinary.empty() == false )
		{
			hashed_string hint = wireTypeHint;
			if ( hint.empty() )
				hint = resolveWireTypeHash( pOrphan->_wireTypeHash );
			if ( hint.empty() == false )
			{
				size_t off{ 0 };
				if ( deserializeValueBinary( pPtr, hint, pOrphan->_listBinary.data(), pOrphan->_listBinary.size(),
											 off, ctx ) )
				{
					if ( hint == pProp->_typeName )
						return true;
					// wire 타입으로 임시 버퍼에 읽은 뒤 텍스트 coerce — 간단 경로: coerce payload
				}
			}
			return tryCoerceBinaryPayload( pPtr, pProp->_typeName, pOrphan->_listBinary.data(), pOrphan->_listBinary.size(), ctx );
		}
		return false;
	}

	bool SchemaMigrateContext::applyOrphanToPath( const utf8* pDottedPath, hashed_string wireTypeHint ) const
	{
		if ( pDottedPath == nullptr )
			return false;
		const vector<string> listParts = splitPath( pDottedPath );
		if ( listParts.empty() )
			return false;

		// orphan 이름은 보통 leaf 또는 full old key
		const hashed_string		 leaf( listParts.back().c_str() );
		const SchemaOrphanValue* pOrphan = findOrphan( leaf );
		if ( pOrphan == nullptr )
			pOrphan = findOrphan( hashed_string( pDottedPath ) );
		if ( pOrphan == nullptr )
			return false;

		void*				pPtr{ nullptr };
		const PropertyInfo* pProp = nullptr;
		if ( resolvePropertyPath( _pInstance, *_pTypeInfo, pDottedPath, pPtr, pProp ) == false )
			return false;

		const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();
		if ( pOrphan->_text.empty() == false )
			return parseTextValueCoerced( pPtr, pProp->_typeName, pOrphan->_text, ctx );
		if ( pOrphan->_listBinary.empty() == false )
		{
			const hashed_string hint = wireTypeHint.empty() == false ? wireTypeHint : pProp->_typeName;
			size_t				off{ 0 };
			if ( deserializeValueBinary( pPtr, hint, pOrphan->_listBinary.data(), pOrphan->_listBinary.size(), off,
										 ctx ) )
				return true;
			return tryCoerceBinaryPayload( pPtr, pProp->_typeName, pOrphan->_listBinary.data(), pOrphan->_listBinary.size(), ctx );
		}
		return false;
	}

	bool SchemaMigrateContext::moveProperty( hashed_string fromProp, hashed_string toProp ) const
	{
		return movePropertyPath( fromProp.c_str(), toProp.c_str() );
	}

	bool SchemaMigrateContext::movePropertyPath( const utf8* pFromPath, const utf8* pToPath ) const
	{
		if ( _pInstance == nullptr || _pTypeInfo == nullptr || pFromPath == nullptr || pToPath == nullptr )
			return false;

		const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();

		void*				pSrcPtr{ nullptr };
		const PropertyInfo* pSrcProp = nullptr;
		void*				pSrcRoot = _pLegacyInstance != nullptr ? _pLegacyInstance : _pInstance;
		const TypeInfo*		pSrcType = _pLegacyTypeInfo != nullptr ? _pLegacyTypeInfo : _pTypeInfo;
		if ( resolvePropertyPath( pSrcRoot, *pSrcType, pFromPath, pSrcPtr, pSrcProp ) == false )
			return false;

		void*				pDstPtr{ nullptr };
		const PropertyInfo* pDstProp = nullptr;
		if ( resolvePropertyPath( _pInstance, *_pTypeInfo, pToPath, pDstPtr, pDstProp ) == false )
			return false;

		StringBuilder<constant::kMaxBuffer8192> ss;
		valueToText( ss, pSrcPtr, pSrcProp->_typeName, ctx );
		return parseTextValueCoerced( pDstPtr, pDstProp->_typeName, ss.view(), ctx );
	}

	bool SchemaMigrateContext::setPropertyFromText( hashed_string propName, string_view text ) const
	{
		if ( _pInstance == nullptr || _pTypeInfo == nullptr )
			return false;
		void*				pPtr{ nullptr };
		const PropertyInfo* pProp = nullptr;
		if ( resolvePropertyPath( _pInstance, *_pTypeInfo, propName.c_str(), pPtr, pProp ) == false )
			return false;
		const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();
		return parseTextValueCoerced( pPtr, pProp->_typeName, text, ctx );
	}

	bool tryCoerceBinaryPayload( void* pPropPtr, hashed_string targetTypeName, const uint8* pPayload, size_t payloadSize,
								 const SerializeContext& ctx )
	{
		if ( pPropPtr == nullptr || pPayload == nullptr )
			return false;

		size_t offset{ 0 };
		if ( deserializeValueBinary( pPropPtr, targetTypeName, pPayload, payloadSize, offset, ctx ) &&
			 offset == payloadSize )
			return true;

		// POD → string
		if ( isStringType( targetTypeName ) )
		{
			string asText;
			if ( formatPodToString( pPayload, payloadSize, asText ) )
				return parseTextValueCoerced( pPropPtr, targetTypeName, asText, ctx );

			// length-prefixed string blob already handled above; if len matches, try raw bytes as text
			if ( payloadSize >= sizeof( uint32 ) )
			{
				uint32 len{ 0 };
				Memory::copy( &len, pPayload, sizeof( uint32 ) );
				if ( sizeof( uint32 ) + len == payloadSize )
				{
					const string_view sv{ reinterpret_cast<const utf8*>( pPayload + sizeof( uint32 ) ), len };
					return parseTextValueCoerced( pPropPtr, targetTypeName, sv, ctx );
				}
			}
		}

		// string blob → numeric
		if ( isNumericTypeName( targetTypeName ) && payloadSize >= sizeof( uint32 ) )
		{
			uint32 len{ 0 };
			Memory::copy( &len, pPayload, sizeof( uint32 ) );
			if ( sizeof( uint32 ) + len == payloadSize )
			{
				const string_view sv{ reinterpret_cast<const utf8*>( pPayload + sizeof( uint32 ) ), len };
				return parseTextValueCoerced( pPropPtr, targetTypeName, sv, ctx );
			}
		}

		// same-size POD reinterpret (int32↔float32 등) — 마지막 수단
		offset										  = 0;
		const SerializeContext::BinaryReadFn* pReader = ctx.findBinaryReader( targetTypeName );
		if ( pReader != nullptr )
		{
			if ( ( *pReader )( pPropPtr, pPayload, payloadSize, offset ) && offset == payloadSize )
				return true;
		}

		return false;
	}

	bool parseTextValueCoerced( void* pValPtr, hashed_string typeName, string_view valStr,
								const SerializeContext& ctx )
	{
		if ( pValPtr == nullptr )
			return false;

		const string_view stripped = stripJsonQuotes( valStr );
		if ( parseTextValue( pValPtr, typeName, valStr, ctx ) )
			return true;
		if ( stripped.data() != valStr.data() || stripped.size() != valStr.size() )
		{
			if ( parseTextValue( pValPtr, typeName, stripped, ctx ) )
				return true;
		}

		// numeric wire → string
		if ( isStringType( typeName ) )
		{
			if ( engine::getTypeRegistry().isType( typeName, "hashed_string" ) )
			{
				*static_cast<hashed_string*>( pValPtr ) =
					hashed_string( stripped.data(), static_cast<uint32>( stripped.size() ) );
			}
			else
				*static_cast<string*>( pValPtr ) = string( stripped );
			return true;
		}

		return false;
	}

	bool resolvePropertyPath( void* pRoot, const TypeInfo& typeInfo, const utf8* pDottedPath, void*& pOutPtr,
							  const PropertyInfo*& pOutProp )
	{
		pOutPtr	 = nullptr;
		pOutProp = nullptr;
		if ( pRoot == nullptr || pDottedPath == nullptr )
			return false;

		const vector<string> listParts = splitPath( pDottedPath );
		if ( listParts.empty() )
			return false;

		void*				pCur	  = pRoot;
		const TypeInfo*		pCurType  = &typeInfo;
		const PropertyInfo* pLastProp = nullptr;

		for ( size_t partIndex = 0; partIndex < listParts.size(); ++partIndex )
		{
			const hashed_string name( listParts[partIndex].c_str() );
			const PropertyInfo* pProp = pCurType->findProperty( name );
			if ( pProp == nullptr )
			{
				for ( const PropertyInfo& candidate : pCurType->getPropertiesWithBase() )
				{
					if ( candidate.matchesName( name ) )
					{
						pProp = &candidate;
						break;
					}
				}
			}
			if ( pProp == nullptr )
				return false;

			void* pPropPtr = pProp->getValuePtr<void>( pCur );
			if ( partIndex + 1 == listParts.size() )
			{
				pOutPtr	 = pPropPtr;
				pOutProp = pProp;
				return true;
			}

			const TypeInfo* pNested = engine::getTypeRegistry().findType( pProp->_typeName );
			if ( pNested == nullptr )
				return false;
			pCur	  = pPropPtr;
			pCurType  = pNested;
			pLastProp = pProp;
			(void)pLastProp;
		}
		return false;
	}

} // namespace sw
