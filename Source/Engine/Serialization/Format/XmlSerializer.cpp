#include "pch.h"

#include "Engine/Serialization/Format/XmlSerializer.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	namespace
	{
		struct XmlSerializerInternal
		{
			static const utf8* xmlTypeInfoName( hashed_string typeName )
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

			static const TypeInfo* findNestedXmlObjectType( hashed_string typeName, const SerializeContext& ctx )
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

			static void writeNestedContainerXml( const void* pContainerPtr, const NestedContainerInfo& nested,
												 const utf8* pPropName, IXmlBackend& backend, const SerializeContext& ctx )
			{
				if ( pContainerPtr == nullptr || nested._wrapper == nullptr || pPropName == nullptr )
					return;
				ISequenceContainerWrapper* pSeq		= nested._wrapper->asSequence();
				IMapContainerWrapper*	   pMapWrap = nested._wrapper->asMap();
				const utf8*				   pTypeTag = xmlTypeInfoName( nested._typeName );
				if ( pTypeTag == nullptr )
					return;

				backend.beginMap( pTypeTag );
				backend.writeAttribute( kXmlPropertyNameAttr, pPropName );
				if ( pSeq != nullptr )
				{
					const bool bOwnedPtr = isOwnedPointerElementType( nested._elementTypeName );
					size_t	   sz		 = pSeq->getSize( pContainerPtr );
					for ( size_t elemIndex = 0; elemIndex < sz; ++elemIndex )
					{
						const void* pElemPtr = pSeq->getElementConst( pContainerPtr, elemIndex );
						if ( nested._elementNested != nullptr )
						{
							writeNestedContainerXml( pElemPtr, *nested._elementNested, "item", backend, ctx );
						}
						else if ( bOwnedPtr )
						{
							void* const* ppObj = static_cast<void* const*>( pElemPtr );
							void*		 pObj  = ppObj != nullptr ? *ppObj : nullptr;
							if ( pObj == nullptr )
								continue;
							const TypeInfo* pRuntimeType = ctx.getRuntimeTypeInfo( pObj );
							if ( pRuntimeType == nullptr )
								continue;
							backend.beginMap( pRuntimeType->_name.c_str() );
							XmlSerializer::serializeVersionedInto( backend, 0, pObj, *pRuntimeType, ctx );
							backend.endMap();
						}
						else
						{
							const TypeInfo* pElemType = findNestedXmlObjectType( nested._elementTypeName, ctx );
							if ( pElemType != nullptr )
							{
								backend.beginMap( pElemType->_name.c_str() );
								writeXmlProperties( pElemPtr, *pElemType, backend, ctx );
								backend.endMap();
							}
							else
							{
								StringBuilder<constant::kMaxBuffer8192> ss;
								SerializerUtil::valueToText( ss, pElemPtr, nested._elementTypeName, ctx );
								backend.writeArrayItem( ss.c_str() );
							}
						}
					}
				}
				else if ( pMapWrap != nullptr )
				{
					pMapWrap->forEach( pContainerPtr, [&]( const void* pKPtr, const void* pVPtr )
					{
						backend.beginMapEntry();

						StringBuilder<constant::kMaxBuffer8192> kSs;
						SerializerUtil::valueToText( kSs, pKPtr, nested._keyTypeName, ctx );
						backend.writeMapKey( kSs.c_str() );

						if ( nested._elementNested != nullptr )
							writeNestedContainerXml( pVPtr, *nested._elementNested, "value", backend, ctx );
						else
						{
							StringBuilder<constant::kMaxBuffer8192> vSs;
							SerializerUtil::valueToText( vSs, pVPtr, nested._elementTypeName, ctx );
							backend.writeMapValue( vSs.c_str() );
						}
						backend.endMapEntry();
					} );
				}
				backend.endMap();
			}

			static void noteCoerceFailVal( vector<SchemaOrphanValue>* pOutOrphans, bool& bFieldError, const PropertyInfo& prop, string_view strValue )
			{
				bFieldError = true;
				if ( pOutOrphans != nullptr )
				{
					SchemaOrphanValue orphan;
					orphan._name		 = prop._name;
					orphan._nameHash	 = prop.getNameHash();
					orphan._wireTypeHash = 0;
					orphan._text		 = string( strValue );
					pOutOrphans->push_back( std::move( orphan ) );
				}
			}

			struct NestedArrayReadCallback
			{
				void*					   _pContainerPtr{ nullptr };
				ISequenceContainerWrapper* _pSeq{ nullptr };
				const NestedContainerInfo* _pNested{ nullptr };
				const SerializeContext*	   _pCtx{ nullptr };
				vector<SchemaOrphanValue>* _pOutOrphans{ nullptr };
				const PropertyInfo*		   _pPropForOrphan{ nullptr };
				size_t*					   _pElemIndex{ nullptr };
				bool*					   _pAny{ nullptr };
				bool*					   _pFieldError{ nullptr };

				void invoke( string_view itemStr )
				{
					*_pAny = true;
					_pSeq->addElementDefault( _pContainerPtr );
					void* pElemPtr = _pSeq->getElement( _pContainerPtr, ( *_pElemIndex )++ );
					if ( _pNested->_elementNested != nullptr )
						return;
					if ( parseTextValueCoerced( pElemPtr, _pNested->_elementTypeName, itemStr, *_pCtx ) == false )
						noteCoerceFailVal( _pOutOrphans, *_pFieldError, *_pPropForOrphan, itemStr );
				}
			};

			struct NestedMapReadCallback
			{
				void*					   _pContainerPtr{ nullptr };
				IMapContainerWrapper*	   _pMapWrap{ nullptr };
				const NestedContainerInfo* _pNested{ nullptr };
				const SerializeContext*	   _pCtx{ nullptr };
				vector<SchemaOrphanValue>* _pOutOrphans{ nullptr };
				const PropertyInfo*		   _pPropForOrphan{ nullptr };
				vector<uint8>*			   _pListKBuf{ nullptr };
				vector<uint8>*			   _pListVBuf{ nullptr };
				bool*					   _pAny{ nullptr };
				bool*					   _pFieldError{ nullptr };

				void invoke( string_view kStr, string_view vStr )
				{
					*_pAny = true;
					_pMapWrap->defaultConstructKey( _pListKBuf->data() );
					_pMapWrap->defaultConstructValue( _pListVBuf->data() );
					const bool kOk = parseTextValueCoerced( _pListKBuf->data(), _pNested->_keyTypeName, kStr, *_pCtx );
					bool	   vOk = false;
					if ( _pNested->_elementNested == nullptr )
						vOk = parseTextValueCoerced( _pListVBuf->data(), _pNested->_elementTypeName, vStr, *_pCtx );
					if ( kOk && vOk )
						_pMapWrap->insertKeyValue( _pContainerPtr, _pListKBuf->data(), _pListVBuf->data() );
					else
						noteCoerceFailVal( _pOutOrphans, *_pFieldError, *_pPropForOrphan, vStr );
					_pMapWrap->destroyKey( _pListKBuf->data() );
					_pMapWrap->destroyValue( _pListVBuf->data() );
				}
			};

			struct NestedOwnedPointerReadCallback
			{
				const SerializeContext* _pCtx{ nullptr };
				bool*					_pAny{ nullptr };
				bool*					_pFieldError{ nullptr };

				void invoke( string_view typeName, string_view nodeXml )
				{
					*_pAny = true;
					if ( _pCtx == nullptr || typeName.empty() )
					{
						*_pFieldError = true;
						return;
					}

					const string typeNameNt( typeName );
					void*		 pObj = _pCtx->createOwnedPointer( hashed_string( typeNameNt.c_str() ) );
					if ( pObj == nullptr )
					{
						*_pFieldError = true;
						return;
					}

					const TypeInfo* pType = engine::getTypeRegistry().findType( hashed_string( typeNameNt.c_str() ) );
					if ( pType == nullptr )
					{
						*_pFieldError = true;
						return;
					}

					uint32 ver{ 0 };
					if ( XmlSerializer::deserializeVersioned( ver, pObj, *pType, string( nodeXml ), 0, nullptr, nullptr,
															  *_pCtx ) == false )
						*_pFieldError = true;
				}
			};

			static bool readNestedContainerXml( void* pContainerPtr, const NestedContainerInfo& nested, IXmlBackend& backend, const SerializeContext& ctx, bool& bOutFieldError, vector<SchemaOrphanValue>* pOutOrphans, const PropertyInfo& propForOrphan )
			{
				if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
					return false;

				const bool bOwnedPtr = isOwnedPointerElementType( nested._elementTypeName );
				if ( bOwnedPtr == false )
					nested._wrapper->clear( pContainerPtr );

				ISequenceContainerWrapper* pSeq		= nested._wrapper->asSequence();
				IMapContainerWrapper*	   pMapWrap = nested._wrapper->asMap();
				if ( pSeq != nullptr && bOwnedPtr )
				{
					bool						   any{ false };
					NestedOwnedPointerReadCallback ptrCb{};
					ptrCb._pCtx		   = &ctx;
					ptrCb._pAny		   = &any;
					ptrCb._pFieldError = &bOutFieldError;
					backend.iterateMap( nullptr, SW_DELEGATE_METHOD( XmlMapItemDelegate, &NestedOwnedPointerReadCallback::invoke, &ptrCb ) );
					return any;
				}
				if ( pSeq != nullptr )
				{
					size_t					elemIndex{ 0 };
					bool					any{ false };
					NestedArrayReadCallback arrayCb{};
					arrayCb._pContainerPtr	= pContainerPtr;
					arrayCb._pSeq			= pSeq;
					arrayCb._pNested		= &nested;
					arrayCb._pCtx			= &ctx;
					arrayCb._pOutOrphans	= pOutOrphans;
					arrayCb._pPropForOrphan = &propForOrphan;
					arrayCb._pElemIndex		= &elemIndex;
					arrayCb._pAny			= &any;
					arrayCb._pFieldError	= &bOutFieldError;
					backend.iterateArray( nullptr, SW_DELEGATE_METHOD( XmlArrayItemDelegate, &NestedArrayReadCallback::invoke, &arrayCb ) );
					return any;
				}
				else if ( pMapWrap != nullptr )
				{
					vector<uint8>		  listKBuf( pMapWrap->getKeySize() );
					vector<uint8>		  listVBuf( pMapWrap->getValueSize() );
					bool				  any{ false };
					NestedMapReadCallback mapCb{};
					mapCb._pContainerPtr  = pContainerPtr;
					mapCb._pMapWrap		  = pMapWrap;
					mapCb._pNested		  = &nested;
					mapCb._pCtx			  = &ctx;
					mapCb._pOutOrphans	  = pOutOrphans;
					mapCb._pPropForOrphan = &propForOrphan;
					mapCb._pListKBuf	  = &listKBuf;
					mapCb._pListVBuf	  = &listVBuf;
					mapCb._pAny			  = &any;
					mapCb._pFieldError	  = &bOutFieldError;
					backend.iterateMap( nullptr, SW_DELEGATE_METHOD( XmlMapItemDelegate, &NestedMapReadCallback::invoke, &mapCb ) );
					return any;
				}
				return false;
			}

			static void writeXmlProperties( const void* pInstance, const TypeInfo& typeInfo, IXmlBackend& backend,
											const SerializeContext& ctx )
			{
				typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
				{
					if ( prop._metadata._bTransient == SW_TRUE )
						return;
					const void* pPropPtr = prop.getRawPtr( pInstance );

					if ( prop._bIsContainer && prop.hasContainerWrapper() )
					{
						NestedContainerInfo shape = prop.getContainerShape();
						if ( shape._typeName.empty() )
							shape._typeName = prop._typeName;
						writeNestedContainerXml( pPropPtr, shape, prop._name.c_str(), backend, ctx );
					}
					else
					{
						const TypeInfo* pNestedType = findNestedXmlObjectType( prop._typeName, ctx );
						if ( pNestedType != nullptr )
						{
							backend.beginMap( prop._name.c_str() );
							writeXmlProperties( pPropPtr, *pNestedType, backend, ctx );
							backend.endMap();
						}
						else
						{
							StringBuilder<constant::kMaxBuffer8192> ss;
							SerializerUtil::valueToText( ss, pPropPtr, prop._typeName, ctx );
							backend.writeAttribute( prop._name.c_str(), ss.c_str() );
						}
					}
				} );
			}

			static bool isNameKnown( const unordered_set<string>& uniqueKnownNames, const utf8* pChildName )
			{
				if ( pChildName == nullptr )
					return false;
				if ( uniqueKnownNames.find( pChildName ) != uniqueKnownNames.end() )
					return true;
				// 대소문자만 다른 태그는 orphan 이 아님 (JsonSerializer bCaseVariant 와 동일).
				for ( const string& known : uniqueKnownNames )
				{
					if ( StringUtil::equalsIgnoreCase( known.c_str(), pChildName ) )
						return true;
				}
				return false;
			}

			static bool readXmlIntoInstance( void* pInstance, const TypeInfo& typeInfo, IXmlBackend& backend, const SerializeContext& ctx,
											 vector<SchemaOrphanValue>* pOutOrphans )
			{
				unordered_set<uint32> uniqueSeen;
				bool				  bFieldError{ false };

				typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
				{
					if ( prop._metadata._bTransient == SW_TRUE )
						return;
					void* pPropPtr = prop.getRawPtr( pInstance );

					if ( prop._bIsContainer && prop.hasContainerWrapper() )
					{
						NestedContainerInfo shape = prop.getContainerShape();
						if ( shape._typeName.empty() )
							shape._typeName = prop._typeName;
						const utf8* pTypeTag = xmlTypeInfoName( shape._typeName );
						bool		entered{ false };
						if ( pTypeTag != nullptr )
						{
							if ( backend.pushNamedTypeChild( pTypeTag, prop._name.c_str() ) )
								entered = true;
							else
							{
								for ( const hashed_string& alias : prop._listAlias )
								{
									if ( alias.empty() == false && backend.pushNamedTypeChild( pTypeTag, alias.c_str() ) )
									{
										entered = true;
										break;
									}
								}
							}
						}
						if ( entered )
						{
							uniqueSeen.insert( prop.getNameHash() );
							if ( readNestedContainerXml( pPropPtr, shape, backend, ctx, bFieldError, pOutOrphans, prop ) == false )
								bFieldError = true;
							backend.popChild();
						}
						else
							SerializerUtil::applyPropertyDefault( pPropPtr, prop, ctx );
					}
					else
					{
						const TypeInfo* pNestedType = findNestedXmlObjectType( prop._typeName, ctx );
						if ( pNestedType != nullptr )
						{
							bool entered = backend.pushChild( prop._name.c_str() );
							if ( entered == false )
							{
								for ( const hashed_string& alias : prop._listAlias )
								{
									if ( alias.empty() == false && backend.pushChild( alias.c_str() ) )
									{
										entered = true;
										break;
									}
								}
							}
							if ( entered )
							{
								uniqueSeen.insert( prop.getNameHash() );
								if ( readXmlIntoInstance( pPropPtr, *pNestedType, backend, ctx, pOutOrphans ) == false )
									bFieldError = true;
								backend.popChild();
							}
							else
								SerializerUtil::applyPropertyDefault( pPropPtr, prop, ctx );
							return;
						}

						string strValue;
						bool   readOk = backend.readAttribute( prop._name.c_str(), strValue );
						if ( readOk == false )
						{
							for ( const hashed_string& alias : prop._listAlias )
							{
								if ( alias.empty() == false && backend.readAttribute( alias.c_str(), strValue ) )
								{
									readOk = true;
									break;
								}
							}
						}

						if ( readOk )
						{
							uniqueSeen.insert( prop.getNameHash() );
							if ( parseTextValueCoerced( pPropPtr, prop._typeName, strValue, ctx ) == false )
								noteCoerceFailVal( pOutOrphans, bFieldError, prop, strValue );
						}
						else
							SerializerUtil::applyPropertyDefault( pPropPtr, prop, ctx );
					}
				} );

				(void)uniqueSeen;
				if ( pOutOrphans != nullptr )
					return true;
				return bFieldError == false;
			}

			static void appendUnknownXmlChildOrphans( XmlNode root, const TypeInfo& typeInfo, bool bIgnore,
													  vector<SchemaOrphanValue>* pOutOrphans )
			{
				if ( pOutOrphans == nullptr || root.isValid() == false )
					return;

				unordered_set<string> uniqueKnownNames;
				typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
				{
					uniqueKnownNames.insert( prop._name.c_str() );
					for ( const hashed_string& alias : prop._listAlias )
					{
						if ( alias.empty() == false )
							uniqueKnownNames.insert( alias.c_str() );
					}
				} );

				// 대소문자만 다른 태그는 setIgnoreCaseKeys(false) 로 의도적으로 바인딩을 거른 것이므로
				// 모르는 필드(orphan)로 올리지 않는다. bIgnore 와 무관하게 무시 대소문자로 판정한다.
				for ( XmlNode child = root.child( nullptr, bIgnore ); child.isValid(); child = child.next( nullptr, bIgnore ) )
				{
					const utf8* pChildName = child.name();
					if ( pChildName == nullptr )
						continue;
					if ( StringUtil::strcmp( pChildName, kSchemaVersionKey ) == 0 )
						continue;
					if ( isNameKnown( uniqueKnownNames, pChildName ) )
						continue;
					const utf8* pNameAttr = child.attr( kXmlPropertyNameAttr, bIgnore );
					if ( pNameAttr != nullptr && isNameKnown( uniqueKnownNames, pNameAttr ) )
						continue;

					const hashed_string nameHs( pChildName );
					SchemaOrphanValue	orphan;
					orphan._name	 = nameHs;
					orphan._nameHash = nameHs.getHash();
					orphan._text	 = child.text() != nullptr ? child.text() : "";
					pOutOrphans->push_back( std::move( orphan ) );
				}

				for ( XmlAttribute attr = root.firstAttr(); attr.isValid(); attr = attr.next() )
				{
					const utf8* pAttrName = attr.name();
					if ( pAttrName == nullptr )
						continue;
					if ( StringUtil::strcmp( pAttrName, kSchemaVersionKey ) == 0 )
						continue;
					if ( isNameKnown( uniqueKnownNames, pAttrName ) )
						continue;

					const hashed_string nameHs( pAttrName );
					SchemaOrphanValue	orphan;
					orphan._name	 = nameHs;
					orphan._nameHash = nameHs.getHash();
					orphan._text	 = attr.value() != nullptr ? attr.value() : "";
					pOutOrphans->push_back( std::move( orphan ) );
				}
			}

			static bool tryAppendUnknownXmlChildOrphans( string_view xmlStr, const TypeInfo& typeInfo,
														 const SerializeContext& ctx, vector<SchemaOrphanValue>* pOutOrphans )
			{
				if ( xmlStr.empty() || pOutOrphans == nullptr )
					return false;

				XmlDocument doc;
				if ( doc.parse( xmlStr ) == false )
					return false;

				const bool bIgnore = ctx.ignoreCaseKeys();
				XmlNode	   root	   = doc.root( typeInfo._name.c_str(), bIgnore );
				if ( root.isValid() == false )
					root = doc.root( nullptr, bIgnore );
				if ( root.isValid() == false )
					return false;

				appendUnknownXmlChildOrphans( root, typeInfo, bIgnore, pOutOrphans );
				return true;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "XmlSerializer" );

	struct XmlDocumentBackend::Impl
	{
		XmlDocument		_doc;
		XmlNode			_currentParent;
		vector<XmlNode> _listNodeStack;

		static string sanitizeTag( const utf8* pName )
		{
			string s( pName != nullptr ? pName : "" );
			for ( size_t pos = 0; ( pos = s.find( "::", pos ) ) != string::npos; pos += 2 )
			{
				s.replace( pos, 2, "__" );
			}
			return s;
		}
	};

	XmlDocumentBackend::XmlDocumentBackend()
		: _impl{ make_unique<Impl>() } {}

	XmlDocumentBackend::~XmlDocumentBackend() = default;

	void XmlDocumentBackend::initXmlSerialization( const utf8* pRootTagName )
	{
		_impl->_doc.clear();
		string	tag			  = Impl::sanitizeTag( pRootTagName );
		XmlNode root		  = _impl->_doc.appendRoot( tag.c_str() );
		_impl->_currentParent = root;
		_impl->_listNodeStack.push_back( root );
	}

	void XmlDocumentBackend::writeValue( const utf8* pTagName, const utf8* pValueString )
	{
		if ( _impl->_currentParent.isValid() == false )
			return;

		string	sTag = Impl::sanitizeTag( pTagName );
		XmlNode node = _impl->_currentParent.appendChild( sTag.c_str() );
		if ( pValueString != nullptr && pValueString[0] != '\0' )
			node.setValue( pValueString );
	}

	void XmlDocumentBackend::writeAttribute( const utf8* pAttrName, const utf8* pValueString )
	{
		if ( _impl->_currentParent.isValid() == false )
			return;

		string sName = Impl::sanitizeTag( pAttrName );
		_impl->_currentParent.appendAttr( sName.c_str(), pValueString != nullptr ? pValueString : "" );
	}

	void XmlDocumentBackend::beginArray( const utf8* pTagName )
	{
		beginMap( pTagName );
	}

	void XmlDocumentBackend::writeArrayItem( const utf8* pValueString )
	{
		writeValue( "item", pValueString );
	}

	void XmlDocumentBackend::endArray()
	{
		endMap();
	}

	void XmlDocumentBackend::beginMap( const utf8* pTagName )
	{
		if ( _impl->_currentParent.isValid() == false )
			return;

		string	sTag = Impl::sanitizeTag( pTagName );
		XmlNode node = _impl->_currentParent.appendChild( sTag.c_str() );
		_impl->_listNodeStack.push_back( node );
		_impl->_currentParent = node;
	}

	void XmlDocumentBackend::beginMapEntry()
	{
		beginMap( "entry" );
	}

	void XmlDocumentBackend::writeMapKey( const utf8* pKeyString )
	{
		writeValue( "key", pKeyString );
	}

	void XmlDocumentBackend::writeMapValue( const utf8* pValueString )
	{
		writeValue( "value", pValueString );
	}

	void XmlDocumentBackend::endMapEntry()
	{
		endMap();
	}

	void XmlDocumentBackend::endMap()
	{
		if ( _impl->_listNodeStack.size() > 1 )
		{
			_impl->_listNodeStack.pop_back();
			_impl->_currentParent = _impl->_listNodeStack.back();
		}
	}

	string XmlDocumentBackend::endSerialize()
	{
		return _impl->_doc.saveToString();
	}

	bool XmlDocumentBackend::initXmlDeserialization( const utf8* pXmlStr, const utf8* pRootTagName )
	{
		_impl->_doc.clear();
		if ( pXmlStr == nullptr || pXmlStr[0] == '\0' )
			return false;

		if ( _impl->_doc.parse( pXmlStr ) == false )
			return false;

		string	sTag = Impl::sanitizeTag( pRootTagName );
		XmlNode root = _impl->_doc.root( sTag.c_str(), ignoreCaseKeys() );
		if ( root.isValid() == false )
			root = _impl->_doc.root( nullptr, ignoreCaseKeys() );

		if ( root.isValid() == false )
			return false;

		_impl->_currentParent = root;
		_impl->_listNodeStack.clear();
		_impl->_listNodeStack.push_back( root );
		return true;
	}

	bool XmlDocumentBackend::readValue( const utf8* pTagName, string& outValue )
	{
		if ( _impl->_currentParent.isValid() == false )
			return false;

		string	sTag = Impl::sanitizeTag( pTagName );
		XmlNode node = _impl->_currentParent.child( sTag.c_str(), ignoreCaseKeys() );
		if ( node.isValid() == false )
			return false;

		outValue = node.text() != nullptr ? node.text() : "";
		return true;
	}

	bool XmlDocumentBackend::readAttribute( const utf8* pAttrName, string& outValue )
	{
		if ( _impl->_currentParent.isValid() == false )
			return false;

		string		sName = Impl::sanitizeTag( pAttrName );
		const utf8* pVal  = _impl->_currentParent.attr( sName.c_str(), ignoreCaseKeys() );
		if ( pVal == nullptr )
			return false;

		outValue = pVal;
		return true;
	}

	bool XmlDocumentBackend::iterateArray( const utf8* pTagName, const XmlArrayItemDelegate& callback )
	{
		if ( _impl->_currentParent.isValid() == false )
			return false;

		const bool bIgnore = ignoreCaseKeys();
		XmlNode	   arrNode = _impl->_currentParent;
		if ( pTagName != nullptr && pTagName[0] != '\0' )
		{
			string sTag = Impl::sanitizeTag( pTagName );
			arrNode		= _impl->_currentParent.child( sTag.c_str(), bIgnore );
			if ( arrNode.isValid() == false )
				return false;
		}

		for ( XmlNode item = arrNode.child( "item", bIgnore ); item; item = item.next( "item", bIgnore ) )
		{
			callback( item.text() != nullptr ? item.text() : "" );
		}

		return true;
	}

	bool XmlDocumentBackend::iterateMap( const utf8* pTagName, const XmlMapItemDelegate& callback )
	{
		if ( _impl->_currentParent.isValid() == false )
			return false;

		const bool bIgnore = ignoreCaseKeys();
		XmlNode	   mapNode = _impl->_currentParent;
		if ( pTagName != nullptr && pTagName[0] != '\0' )
		{
			string sTag = Impl::sanitizeTag( pTagName );
			mapNode		= _impl->_currentParent.child( sTag.c_str(), bIgnore );
			if ( mapNode.isValid() == false )
				return false;
		}

		for ( XmlNode child = mapNode.child( nullptr, bIgnore ); child; child = child.next( nullptr, bIgnore ) )
		{
			const utf8* pChildName = child.name();
			if ( pChildName == nullptr )
				continue;

			if ( StringUtil::equalsIgnoreCase( pChildName, "entry" ) )
			{
				XmlNode kNode = child.child( "key", bIgnore );
				XmlNode vNode = child.child( "value", bIgnore );
				if ( kNode.isValid() && vNode.isValid() )
					callback( kNode.text() != nullptr ? kNode.text() : "", vNode.text() != nullptr ? vNode.text() : "" );
				continue;
			}

			const string nodeXml = child.toString();
			callback( pChildName, nodeXml );
		}
		return true;
	}

	bool XmlDocumentBackend::pushChild( const utf8* pTagName )
	{
		if ( _impl->_currentParent.isValid() == false )
			return false;

		string	sTag  = Impl::sanitizeTag( pTagName );
		XmlNode child = _impl->_currentParent.child( sTag.c_str(), ignoreCaseKeys() );
		if ( child.isValid() == false )
			return false;

		_impl->_listNodeStack.push_back( child );
		_impl->_currentParent = child;
		return true;
	}

	bool XmlDocumentBackend::pushNamedTypeChild( const utf8* pTypeTag, const utf8* pPropName )
	{
		if ( _impl->_currentParent.isValid() == false || pTypeTag == nullptr || pPropName == nullptr )
			return false;

		const bool bIgnore = ignoreCaseKeys();
		string	   sTag	   = Impl::sanitizeTag( pTypeTag );
		for ( XmlNode child = _impl->_currentParent.child( sTag.c_str(), bIgnore ); child.isValid();
			  child			= child.next( sTag.c_str(), bIgnore ) )
		{
			const utf8* pNameAttr = child.attr( kXmlPropertyNameAttr, bIgnore );
			if ( pNameAttr == nullptr )
				continue;
			const bool bMatch = bIgnore ? StringUtil::equalsIgnoreCase( pNameAttr, pPropName )
										: StringUtil::strcmp( pNameAttr, pPropName ) == 0;
			if ( bMatch == false )
				continue;

			_impl->_listNodeStack.push_back( child );
			_impl->_currentParent = child;
			return true;
		}
		return false;
	}

	void XmlDocumentBackend::popChild()
	{
		if ( _impl->_listNodeStack.size() <= 1 )
			return;
		_impl->_listNodeStack.pop_back();
		_impl->_currentParent = _impl->_listNodeStack.back();
	}

	string XmlSerializer::serialize( const void* pInstance, const TypeInfo& typeInfo,
									 IXmlBackend& backend, const SerializeContext& ctx )
	{
		backend.initXmlSerialization( typeInfo._name.c_str() );
		XmlSerializerInternal::writeXmlProperties( pInstance, typeInfo, backend, ctx );
		return backend.endSerialize();
	}

	bool XmlSerializer::deserialize( void* pInstance, const TypeInfo& typeInfo,
									 IXmlBackend& backend, string_view xmlStr,
									 const SerializeContext& ctx )
	{
		if ( xmlStr.empty() )
			return false;

		backend.setIgnoreCaseKeys( ctx.ignoreCaseKeys() );

		if ( backend.initXmlDeserialization( xmlStr.data(), typeInfo._name.c_str() ) == false )
			return false;

		if ( XmlSerializerInternal::readXmlIntoInstance( pInstance, typeInfo, backend, ctx, nullptr ) == false )
			return false;

		vector<SchemaOrphanValue> listOrphans;
		if ( XmlSerializerInternal::tryAppendUnknownXmlChildOrphans( xmlStr, typeInfo, ctx, &listOrphans ) && listOrphans.empty() == false )
			return false;
		return true;
	}

	string XmlSerializer::serialize( const void* pInstance, const TypeInfo& typeInfo, const SerializeContext& ctx )
	{
		XmlDocumentBackend backend;
		return serialize( pInstance, typeInfo, backend, ctx );
	}

	bool XmlSerializer::deserialize( void* pInstance, const TypeInfo& typeInfo, string_view xmlStr, const SerializeContext& ctx )
	{
		if ( xmlStr.empty() )
			return false;
		vector<SchemaOrphanValue> listOrphans;
		if ( deserializeSoft( pInstance, typeInfo, xmlStr, &listOrphans, nullptr, ctx ) == false )
			return false;
		return listOrphans.empty();
	}

	bool XmlSerializer::saveFile( string_view absPath, const void* pInstance, const TypeInfo& typeInfo,
								  const SerializeContext& ctx )
	{
		if ( absPath.empty() )
			return false;
		return FileUtil::writeTextFile( absPath, serialize( pInstance, typeInfo, ctx ) );
	}

	bool XmlSerializer::loadFile( string_view path, void* pInstance, const TypeInfo& typeInfo, const SerializeContext& ctx )
	{
		XmlDocument doc;
		if ( doc.loadPath( path ) == false )
			return false;
		return deserialize( pInstance, typeInfo, doc.saveToString(), ctx );
	}

	bool XmlSerializer::deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
										 vector<SchemaOrphanValue>* pOutOrphans, uint32* pOutVersion,
										 const SerializeContext& ctx )
	{
		if ( xmlStr.empty() )
			return false;

		XmlDocument doc;
		if ( doc.parse( xmlStr ) == false )
			return false;

		const bool bIgnore = ctx.ignoreCaseKeys();
		XmlNode	   root	   = doc.root( typeInfo._name.c_str(), bIgnore );
		if ( root.isValid() == false )
			root = doc.root( nullptr, bIgnore );
		if ( root.isValid() == false )
			return false;

		if ( pOutVersion != nullptr )
		{
			*pOutVersion	 = 0;
			const utf8* pVer = root.attr( kSchemaVersionKey, bIgnore );
			if ( pVer != nullptr )
				*pOutVersion = static_cast<uint32>( StringUtil::strtoull( pVer, nullptr, 10 ) );
		}

		XmlDocumentBackend backend;
		backend.setIgnoreCaseKeys( bIgnore );
		if ( backend.initXmlDeserialization( xmlStr.data(), typeInfo._name.c_str() ) == false )
			return false;
		if ( XmlSerializerInternal::readXmlIntoInstance( pInstance, typeInfo, backend, ctx, pOutOrphans ) == false )
			return false;

		if ( pOutOrphans != nullptr )
			XmlSerializerInternal::appendUnknownXmlChildOrphans( root, typeInfo, bIgnore, pOutOrphans );

		return true;
	}

	string XmlSerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
											  const SerializeContext& ctx )
	{
		XmlDocumentBackend backend;
		backend.initXmlSerialization( typeInfo._name.c_str() );
		serializeVersionedInto( backend, version, pInstance, typeInfo, ctx );
		return backend.endSerialize();
	}

	void XmlSerializer::serializeVersionedInto( IXmlBackend& backend, uint32 version, const void* pInstance,
												const TypeInfo& typeInfo, const SerializeContext& ctx )
	{
		const string verStr = to_string( version );
		backend.writeAttribute( kSchemaVersionKey, verStr.c_str() );
		XmlSerializerInternal::writeXmlProperties( pInstance, typeInfo, backend, ctx );
	}

	bool XmlSerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
											  string_view xmlStr, uint32 currentVersion, SchemaMigrateFn migrate,
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
				 deserializeSoft( pLegacyPtr, *pLegacyTypeInfo, xmlStr, &listOrphans, &legacyVer, ctx ) == false )
			{
				destroyScratchInstance( pLegacyPtr, *pLegacyTypeInfo );
				return false;
			}
			outVersion = legacyVer;
		}

		uint32 softVer{ 0 };
		if ( deserializeSoft( pInstance, typeInfo, xmlStr, &listOrphans, &softVer, ctx ) == false )
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
			SW_LOG_WARNING( "schema version %# -> %# with no migrate callback (%# listOrphans)",
							outVersion, currentVersion, static_cast<uint32>( listOrphans.size() ) );
			ok = false;
		}
		else if ( migrate == nullptr && listOrphans.empty() == false && ctx.allowUnknownProperties() == false )
		{
			SW_LOG_WARNING( "schema version %# -> %# with no migrate callback (%# listOrphans)",
							outVersion, currentVersion, static_cast<uint32>( listOrphans.size() ) );
			ok = false;
		}
		if ( pLegacyPtr != nullptr )
			destroyScratchInstance( pLegacyPtr, *pLegacyTypeInfo );
		return ok;
	}

} // namespace sw
