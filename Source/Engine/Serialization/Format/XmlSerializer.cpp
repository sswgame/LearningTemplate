#include "pch.h"

#include "Engine/Serialization/Format/XmlSerializer.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct XmlSerializerInternal
        {
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

            static void noteCoerceFailVal( vector<SchemaOrphanValue>* pOutListOrphan, bool& bFieldError, const PropertyInfo& prop, string_view strValue )
            {
                bFieldError = true;
                if ( pOutListOrphan != nullptr )
                {
                    SchemaOrphanValue orphan;
                    orphan._name         = prop._name;
                    orphan._nameHash     = prop.getNameHash();
                    orphan._wireTypeHash = 0;
                    orphan._text         = string( strValue );
                    pOutListOrphan->push_back( std::move( orphan ) );
                }
            }

            /**
             * @brief 컨테이너를 현재 노드 "안에" 자연스러운 형태로 씁니다.
             * @details 시퀀스 원소는 구조체면 타입 이름 태그, 그 외에는 <item>.
             *          맵 항목은 <entry key="K">. 값은 그 노드 안에 같은 규칙으로 재귀합니다.
             *          리더는 태그 이름에 의존하지 않으므로(다형 포인터 제외) 임의 중첩이 됩니다.
             */
            static void writeContainerXml( const void* pContainerPtr, const NestedContainerInfo& nested,
                                           IXmlBackend& backend, const SerializeContext& ctx )
            {
                if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
                    return;

                const bool                 bOwnedPtr = isOwnedPointerElementType( nested._elementTypeName );
                ISequenceContainerWrapper* pSeq      = nested._wrapper->asSequence();
                IMapContainerWrapper*      pMapWrap  = nested._wrapper->asMap();

                if ( pSeq != nullptr )
                {
                    const size_t sz = pSeq->getSize( pContainerPtr );
                    for ( size_t elemIndex = 0; elemIndex < sz; ++elemIndex )
                    {
                        const void* pElemPtr = pSeq->getElementConst( pContainerPtr, elemIndex );
                        if ( nested._elementNested != nullptr )
                        {
                            backend.beginMap( kXmlItemTag );
                            writeContainerXml( pElemPtr, *nested._elementNested, backend, ctx );
                            backend.endMap();
                        }
                        else if ( bOwnedPtr )
                        {
                            void* const* ppObj = static_cast<void* const*>( pElemPtr );
                            void*        pObj  = ppObj != nullptr ? *ppObj : nullptr;
                            if ( pObj == nullptr )
                                continue;
                            // 다형 원소만 태그 이름이 곧 런타임 타입 정보다.
                            const TypeInfo* pRuntimeType = ctx.getRuntimeTypeInfo( pObj );
                            if ( pRuntimeType == nullptr )
                                continue;
                            backend.beginMap( pRuntimeType->_name.c_str() );
                            writeXmlProperties( pObj, *pRuntimeType, backend, ctx );
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
                                backend.writeValue( kXmlItemTag, ss.c_str() );
                            }
                        }
                    }
                    return;
                }

                if ( pMapWrap != nullptr )
                {
                    pMapWrap->forEach( pContainerPtr, [&]( const void* pKPtr, const void* pVPtr )
                    {
                        StringBuilder<constant::kMaxBuffer8192> kSs;
                        SerializerUtil::valueToText( kSs, pKPtr, nested._keyTypeName, ctx );

                        backend.beginMap( kXmlEntryTag );
                        backend.writeAttribute( kXmlKeyAttr, kSs.c_str() );
                        if ( nested._elementNested != nullptr )
                        {
                            writeContainerXml( pVPtr, *nested._elementNested, backend, ctx );
                        }
                        else
                        {
                            const TypeInfo* pElemType = findNestedXmlObjectType( nested._elementTypeName, ctx );
                            if ( pElemType != nullptr )
                            {
                                backend.beginMap( pElemType->_name.c_str() );
                                writeXmlProperties( pVPtr, *pElemType, backend, ctx );
                                backend.endMap();
                            }
                            else
                            {
                                StringBuilder<constant::kMaxBuffer8192> vSs;
                                SerializerUtil::valueToText( vSs, pVPtr, nested._elementTypeName, ctx );
                                backend.writeText( vSs.c_str() );
                            }
                        }
                        backend.endMap();
                    } );
                }
            }

            /**
             * @brief 현재 노드 "안에" 있는 컨테이너를 읽습니다. writeContainerXml 의 역연산입니다.
             * @details 자식 태그 이름에 의존하지 않고 순서대로 훑습니다(다형 포인터만 이름=타입).
             *          원소가 또 컨테이너면 그 자식 노드에서 재귀하므로 임의 중첩이 됩니다.
             */
            static bool readContainerXml( void* pContainerPtr, const NestedContainerInfo& nested, IXmlBackend& backend,
                                          const SerializeContext& ctx, bool& bOutFieldError,
                                          vector<SchemaOrphanValue>* pOutListOrphan, const PropertyInfo& propForOrphan )
            {
                if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
                    return false;

                const bool bOwnedPtr = isOwnedPointerElementType( nested._elementTypeName );
                if ( bOwnedPtr == false )
                    nested._wrapper->clear( pContainerPtr );

                ISequenceContainerWrapper* pSeq     = nested._wrapper->asSequence();
                IMapContainerWrapper*      pMapWrap = nested._wrapper->asMap();

                if ( pSeq != nullptr )
                {
                    size_t elemIndex{ 0 };
                    bool   bAny{ false };
                    backend.iterateChildren( SW_DELEGATE_LAMBDA( XmlChildVisitDelegate, [&]( string_view tagName )
                    {
                        bAny = true;
                        if ( bOwnedPtr )
                        {
                            // 다형 원소: 태그 이름이 런타임 타입이다.
                            const hashed_string typeName( string( tagName ).c_str() );
                            void*               pObj  = ctx.createOwnedPointer( typeName );
                            const TypeInfo*     pType = engine::getTypeRegistry().findType( typeName );
                            if ( pObj == nullptr || pType == nullptr )
                                return;
                            readXmlIntoInstance( pObj, *pType, backend, ctx, pOutListOrphan );
                            return;
                        }

                        pSeq->addElementDefault( pContainerPtr );
                        void* pElemPtr = pSeq->getElement( pContainerPtr, elemIndex++ );
                        if ( nested._elementNested != nullptr )
                        {
                            readContainerXml( pElemPtr, *nested._elementNested, backend, ctx, bOutFieldError, pOutListOrphan, propForOrphan );
                            return;
                        }

                        const TypeInfo* pElemType = findNestedXmlObjectType( nested._elementTypeName, ctx );
                        if ( pElemType != nullptr )
                        {
                            readXmlIntoInstance( pElemPtr, *pElemType, backend, ctx, pOutListOrphan );
                            return;
                        }

                        string itemText;
                        backend.readText( itemText );
                        if ( parseTextValueCoerced( pElemPtr, nested._elementTypeName, itemText, ctx ) == false )
                            noteCoerceFailVal( pOutListOrphan, bOutFieldError, propForOrphan, itemText );
                    } ) );
                    return bAny;
                }

                if ( pMapWrap != nullptr )
                {
                    vector<uint8> listKBuf( pMapWrap->getKeySize() );
                    vector<uint8> listVBuf( pMapWrap->getValueSize() );
                    bool          bAny{ false };
                    backend.iterateChildren( SW_DELEGATE_LAMBDA( XmlChildVisitDelegate, [&]( string_view tagName )
                    {
                        (void)tagName;
                        bAny = true;

                        string keyText;
                        backend.readAttribute( kXmlKeyAttr, keyText );

                        pMapWrap->defaultConstructKey( listKBuf.data() );
                        pMapWrap->defaultConstructValue( listVBuf.data() );

                        const bool kOk = parseTextValueCoerced( listKBuf.data(), nested._keyTypeName, keyText, ctx );
                        bool       vOk{ true };
                        if ( nested._elementNested != nullptr )
                        {
                            readContainerXml( listVBuf.data(), *nested._elementNested, backend, ctx, bOutFieldError, pOutListOrphan, propForOrphan );
                        }
                        else
                        {
                            const TypeInfo* pElemType = findNestedXmlObjectType( nested._elementTypeName, ctx );
                            if ( pElemType != nullptr )
                            {
                                // 구조체 값은 <entry> 안의 <TypeName> 자식에 들어 있다.
                                if ( backend.pushFirstChild() )
                                {
                                    readXmlIntoInstance( listVBuf.data(), *pElemType, backend, ctx, pOutListOrphan );
                                    backend.popChild();
                                }
                            }
                            else
                            {
                                string valText;
                                backend.readText( valText );
                                vOk = parseTextValueCoerced( listVBuf.data(), nested._elementTypeName, valText, ctx );
                                if ( vOk == false )
                                    noteCoerceFailVal( pOutListOrphan, bOutFieldError, propForOrphan, valText );
                            }
                        }

                        if ( kOk && vOk )
                            pMapWrap->insertKeyValue( pContainerPtr, listKBuf.data(), listVBuf.data() );
                        pMapWrap->destroyKey( listKBuf.data() );
                        pMapWrap->destroyValue( listVBuf.data() );
                    } ) );
                    return bAny;
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

                    if ( prop._bIsBitField == SW_TRUE )
                    {
                        const bool bVal = prop.getValue<bool>( pInstance );
                        backend.writeAttribute( prop._name.c_str(), bVal ? "true" : "false" );
                        return;
                    }

                    const void* pPropPtr = prop.getRawPtr( pInstance );

                    if ( prop._bIsContainer && prop.hasContainerWrapper() )
                    {
                        NestedContainerInfo shape = prop.getContainerShape();
                        if ( shape._typeName.empty() )
                            shape._typeName = prop._typeName;
                        // 프로퍼티 이름이 곧 컨테이너 요소다. 그 안에 원소들이 들어간다.
                        backend.beginMap( prop._name.c_str() );
                        writeContainerXml( pPropPtr, shape, backend, ctx );
                        backend.endMap();
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

                            // 기본은 "전부 쓴다" 다 — 그래야 파일에 없음과 명시적으로 비어 있음이
                            // 구분된다. 생략해도 좋다고 **스키마가 선언한** 필드만 비었을 때 뺀다.
                            if ( prop._metadata._bSkipIfEmpty == SW_TRUE && ss.size() == 0 )
                                return;

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
                    if ( StringUtil::equals( known.c_str(), pChildName, true ) )
                        return true;
                }
                return false;
            }

            static bool readXmlIntoInstance( void* pInstance, const TypeInfo& typeInfo, IXmlBackend& backend, const SerializeContext& ctx,
                                             vector<SchemaOrphanValue>* pOutListOrphan )
            {
                unordered_set<uint32> uniqueSeen;
                bool                  bFieldError{ false };

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
                        // 컨테이너는 프로퍼티 이름 요소 안에 들어 있다.
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
                            if ( readContainerXml( pPropPtr, shape, backend, ctx, bFieldError, pOutListOrphan, prop ) == false )
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
                                if ( readXmlIntoInstance( pPropPtr, *pNestedType, backend, ctx, pOutListOrphan ) == false )
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
                            if ( prop._bIsBitField == SW_TRUE )
                            {
                                const bool bVal = StringUtil::parseBool( strValue, false );
                                prop.setValue<bool>( pInstance, bVal );
                            }
                            else if ( parseTextValueCoerced( pPropPtr, prop._typeName, strValue, ctx ) == false )
                                noteCoerceFailVal( pOutListOrphan, bFieldError, prop, strValue );
                        }
                        else
                            SerializerUtil::applyPropertyDefault( pPropPtr, prop, ctx );
                    }
                } );

                (void)uniqueSeen;
                if ( pOutListOrphan != nullptr )
                    return true;
                return bFieldError == false;
            }

            static void appendUnknownXmlChildOrphans( XmlNode root, const TypeInfo& typeInfo, bool bIgnore,
                                                      vector<SchemaOrphanValue>* pOutListOrphan )
            {
                if ( pOutListOrphan == nullptr || root.isValid() == false )
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
                    if ( StringUtil::equals( pChildName, kSchemaVersionKey ) )
                        continue;
                    if ( isNameKnown( uniqueKnownNames, pChildName ) )
                        continue;
                    const utf8* pNameAttr = child.attr( kXmlPropertyNameAttr, bIgnore );
                    if ( pNameAttr != nullptr && isNameKnown( uniqueKnownNames, pNameAttr ) )
                        continue;

                    const hashed_string nameHs( pChildName );
                    SchemaOrphanValue   orphan;
                    orphan._name     = nameHs;
                    orphan._nameHash = nameHs.getHash();
                    orphan._text     = child.text() != nullptr ? child.text() : "";
                    pOutListOrphan->push_back( std::move( orphan ) );
                }

                for ( XmlAttribute attr = root.firstAttr(); attr.isValid(); attr = attr.next() )
                {
                    const utf8* pAttrName = attr.name();
                    if ( pAttrName == nullptr )
                        continue;
                    if ( StringUtil::equals( pAttrName, kSchemaVersionKey ) )
                        continue;
                    if ( isNameKnown( uniqueKnownNames, pAttrName ) )
                        continue;

                    const hashed_string nameHs( pAttrName );
                    SchemaOrphanValue   orphan;
                    orphan._name     = nameHs;
                    orphan._nameHash = nameHs.getHash();
                    orphan._text     = attr.value() != nullptr ? attr.value() : "";
                    pOutListOrphan->push_back( std::move( orphan ) );
                }
            }

            static bool tryAppendUnknownXmlChildOrphans( string_view xmlStr, const TypeInfo& typeInfo,
                                                         const SerializeContext& ctx, vector<SchemaOrphanValue>* pOutListOrphan )
            {
                if ( xmlStr.empty() || pOutListOrphan == nullptr )
                    return false;

                XmlDocument doc;
                if ( doc.parse( xmlStr ) == false )
                    return false;

                const bool bIgnore = ctx.ignoreCaseKeys();
                XmlNode    root    = doc.root( typeInfo._name.c_str(), bIgnore );
                if ( root.isValid() == false )
                    root = doc.root( nullptr, bIgnore );
                if ( root.isValid() == false )
                    return false;

                appendUnknownXmlChildOrphans( root, typeInfo, bIgnore, pOutListOrphan );
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
        XmlDocument     _doc;
        XmlNode         _currentParent;
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
        string  tag           = Impl::sanitizeTag( pRootTagName );
        XmlNode root          = _impl->_doc.appendRoot( tag.c_str() );
        _impl->_currentParent = root;
        _impl->_listNodeStack.push_back( root );
    }

    void XmlDocumentBackend::writeValue( const utf8* pTagName, const utf8* pValueString )
    {
        if ( _impl->_currentParent.isValid() == false )
            return;

        string  sTag = Impl::sanitizeTag( pTagName );
        XmlNode node = _impl->_currentParent.appendChild( sTag.c_str() );
        if ( StringUtil::isNullOrEmpty( pValueString ) == false )
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

        string  sTag = Impl::sanitizeTag( pTagName );
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
        if ( StringUtil::isNullOrEmpty( pXmlStr ) )
            return false;

        if ( _impl->_doc.parse( pXmlStr ) == false )
            return false;

        string  sTag = Impl::sanitizeTag( pRootTagName );
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

        string  sTag = Impl::sanitizeTag( pTagName );
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

        string      sName = Impl::sanitizeTag( pAttrName );
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
        XmlNode    arrNode = _impl->_currentParent;
        if ( StringUtil::isNullOrEmpty( pTagName ) == false )
        {
            string sTag = Impl::sanitizeTag( pTagName );
            arrNode     = _impl->_currentParent.child( sTag.c_str(), bIgnore );
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
        XmlNode    mapNode = _impl->_currentParent;
        if ( StringUtil::isNullOrEmpty( pTagName ) == false )
        {
            string sTag = Impl::sanitizeTag( pTagName );
            mapNode     = _impl->_currentParent.child( sTag.c_str(), bIgnore );
            if ( mapNode.isValid() == false )
                return false;
        }

        for ( XmlNode child = mapNode.child( nullptr, bIgnore ); child; child = child.next( nullptr, bIgnore ) )
        {
            const utf8* pChildName = child.name();
            if ( pChildName == nullptr )
                continue;

            if ( StringUtil::equals( pChildName, "entry", true ) )
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

        string  sTag  = Impl::sanitizeTag( pTagName );
        XmlNode child = _impl->_currentParent.child( sTag.c_str(), ignoreCaseKeys() );
        if ( child.isValid() == false )
            return false;

        _impl->_listNodeStack.push_back( child );
        _impl->_currentParent = child;
        return true;
    }

    bool XmlDocumentBackend::pushFirstChild()
    {
        if ( _impl->_currentParent.isValid() == false )
            return false;
        XmlNode child = _impl->_currentParent.child( nullptr, ignoreCaseKeys() );
        if ( child.isValid() == false )
            return false;
        _impl->_listNodeStack.push_back( child );
        _impl->_currentParent = child;
        return true;
    }

    void XmlDocumentBackend::writeText( const utf8* pText )
    {
        if ( _impl->_currentParent.isValid() && pText != nullptr )
            _impl->_currentParent.setValue( pText );
    }

    bool XmlDocumentBackend::readText( string& outText )
    {
        if ( _impl->_currentParent.isValid() == false )
            return false;
        const utf8* pText = _impl->_currentParent.text();
        outText           = ( pText != nullptr ) ? pText : "";
        return true;
    }

    bool XmlDocumentBackend::iterateChildren( const XmlChildVisitDelegate& callback )
    {
        if ( _impl->_currentParent.isValid() == false || callback.isBound() == false )
            return false;

        const XmlNode parent = _impl->_currentParent;
        bool          bAny{ false };
        for ( XmlNode child = parent.child( nullptr, ignoreCaseKeys() ); child.isValid(); child = child.next( nullptr, ignoreCaseKeys() ) )
        {
            // 콜백이 도는 동안 그 자식이 현재 노드가 되어야 재귀 순회가 가능하다.
            _impl->_listNodeStack.push_back( child );
            _impl->_currentParent = child;

            const utf8* pName = child.name();
            callback( string_view( pName != nullptr ? pName : "" ) );
            bAny = true;

            _impl->_listNodeStack.pop_back();
            _impl->_currentParent = parent;
        }
        return bAny;
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

        vector<SchemaOrphanValue> listOrphan;
        if ( XmlSerializerInternal::tryAppendUnknownXmlChildOrphans( xmlStr, typeInfo, ctx, &listOrphan ) && listOrphan.empty() == false )
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
        vector<SchemaOrphanValue> listOrphan;
        if ( deserializeSoft( pInstance, typeInfo, xmlStr, &listOrphan, nullptr, ctx ) == false )
            return false;
        return listOrphan.empty();
    }

    bool XmlSerializer::serializeToArchive( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                            const SerializeContext& ctx )
    {
        const string xmlStr = serialize( pInstance, typeInfo, ctx );
        if ( xmlStr.empty() )
            return false;

        outArchive << xmlStr;
        return true;
    }

    bool XmlSerializer::deserializeFromArchive( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                                const SerializeContext& ctx )
    {
        if ( inArchive.isError() )
            return false;

        string xmlStr;
        inArchive >> xmlStr;
        if ( inArchive.isError() || xmlStr.empty() )
            return false;

        return deserialize( pInstance, typeInfo, xmlStr, ctx );
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
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false && FileUtil::readTextFile( path, text ) == false )
            return false;
        return deserialize( pInstance, typeInfo, text, ctx );
    }

    bool XmlSerializer::deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
                                         vector<SchemaOrphanValue>* pOutListOrphan, uint32* pOutVersion,
                                         const SerializeContext& ctx )
    {
        if ( xmlStr.empty() )
            return false;

        XmlDocument doc;
        if ( doc.parse( xmlStr ) == false )
            return false;

        const bool bIgnore = ctx.ignoreCaseKeys();
        XmlNode    root    = doc.root( typeInfo._name.c_str(), bIgnore );
        if ( root.isValid() == false )
            root = doc.root( nullptr, bIgnore );
        if ( root.isValid() == false )
            return false;

        if ( pOutVersion != nullptr )
        {
            *pOutVersion     = 0;
            const utf8* pVer = root.attr( kSchemaVersionKey, bIgnore );
            if ( pVer != nullptr )
            {
                uint64 ver{ 0 };
                StringUtil::parseUInt64( pVer, ver, 10 );
                *pOutVersion = static_cast<uint32>( ver );
            }
        }

        XmlDocumentBackend backend;
        backend.setIgnoreCaseKeys( bIgnore );
        if ( backend.initXmlDeserialization( xmlStr.data(), typeInfo._name.c_str() ) == false )
            return false;
        if ( XmlSerializerInternal::readXmlIntoInstance( pInstance, typeInfo, backend, ctx, pOutListOrphan ) == false )
            return false;

        if ( pOutListOrphan != nullptr )
            XmlSerializerInternal::appendUnknownXmlChildOrphans( root, typeInfo, bIgnore, pOutListOrphan );

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
        vector<SchemaOrphanValue> listOrphan;
        ScopedScratchInstance     scratchLegacy( pLegacyTypeInfo );
        void*                     pLegacyPtr = scratchLegacy.get();
        outVersion                           = 0;

        if ( pLegacyTypeInfo != nullptr && pLegacyTypeInfo->_size > 0 )
        {
            uint32 legacyVer{ 0 };
            if ( pLegacyPtr == nullptr ||
                 deserializeSoft( pLegacyPtr, *pLegacyTypeInfo, xmlStr, &listOrphan, &legacyVer, ctx ) == false )
                return false;
            outVersion = legacyVer;
        }

        uint32 softVer{ 0 };
        if ( deserializeSoft( pInstance, typeInfo, xmlStr, &listOrphan, &softVer, ctx ) == false )
            return false;
        if ( pLegacyPtr == nullptr )
            outVersion = softVer;
        else if ( softVer != 0 )
            outVersion = softVer;

        return runSchemaMigrateStep( outVersion, currentVersion, pInstance, typeInfo, pLegacyPtr, pLegacyTypeInfo,
                                     listOrphan, migrate, outVersion != currentVersion, ctx );
    }

} // namespace sw
