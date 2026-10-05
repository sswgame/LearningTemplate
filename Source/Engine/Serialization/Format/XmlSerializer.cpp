#include "pch.h"

#include "Engine/Serialization/Format/XmlSerializer.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Core/ContainerVisitor.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct XmlSerializerInternal
        {
            /**
             * @brief 프로퍼티 이름, 안 되면 별칭 순으로 `tryName( pName )` 을 불러 처음 성공하면 true 입니다.
             * @details 자식 요소 들어가기(컨테이너 · 중첩 구조체)와 속성 읽기가 이 "이름 → 별칭" 루프 하나를 씁니다.
             */
            template <typename TryNameFunc>
            [[nodiscard]] static bool tryNameOrAlias( const PropertyInfo& prop, TryNameFunc&& tryName )
            {
                if ( tryName( prop._name.c_str() ) )
                    return true;
                for ( const hashed_string& alias : prop._listAlias )
                {
                    if ( alias.empty() == false && tryName( alias.c_str() ) )
                        return true;
                }
                return false;
            }

            /** @brief 읽지 못한 글을 그 칸의 orphan 으로 남깁니다(로드가 끝난 뒤 버린 값으로 알린다). orphan 목록이 없으면 아무것도 하지 않습니다. */
            static void recordDroppedText( vector<SchemaOrphanValue>* pOutListOrphan, const PropertyInfo& prop, string_view text )
            {
                if ( pOutListOrphan == nullptr )
                    return;
                SchemaOrphanValue orphan;
                orphan._name         = prop._name;
                orphan._nameHash     = prop.getNameHash();
                orphan._wireTypeHash = 0;
                orphan._text         = string( text );
                pOutListOrphan->push_back( std::move( orphan ) );
            }

            /** @brief 컨테이너를 지금 노드 "안에" 적습니다 — 시퀀스 원소는 구조체면 타입 이름 태그, 그 외에는 `<item>`, 맵 항목은 `<entry key="K">` 입니다. */
            class ContainerWriter final : public IContainerWriter
            {
            public:
                ContainerWriter( IXmlBackend& backend, const SerializeContext& ctx )
                    : _backend{ backend }
                    , _ctx{ ctx }
                {
                }

                void beginSequence( size_t ) override {}
                void endSequence() override {}
                void beginMap( size_t ) override {}
                void endMap() override {}

                void beginMapEntry( const void* pKey, const hashed_string& keyTypeName ) override
                {
                    StringBuilder<constant::kMaxBuffer8192> keyText;
                    SerializerUtil::valueToText( keyText, pKey, keyTypeName, _ctx );
                    _backend.beginMap( kXmlEntryTag );
                    _backend.writeAttribute( kXmlKeyAttr, keyText.c_str() );
                }

                void endMapEntry() override { _backend.endMap(); }

                // 시퀀스 원소인 컨테이너는 `<item>` 안에, 맵 값인 컨테이너는 `<entry>` 안에 바로 적는다.
                void beginNestedContainer( const ContainerSlot slot ) override
                {
                    if ( slot == ContainerSlot::SequenceElement )
                        _backend.beginMap( kXmlItemTag );
                }

                void endNestedContainer( const ContainerSlot slot ) override
                {
                    if ( slot == ContainerSlot::SequenceElement )
                        _backend.endMap();
                }

                void writeOwnedPointer( const void* pObject ) override
                {
                    if ( pObject == nullptr )
                        return;
                    // 맡아 둔 원소(모르는 타입)는 읽은 원문 그대로 다시 쓴다.
                    SerializeContext::OpaqueElementView opaque{};
                    if ( _ctx.queryOpaqueElement( pObject, opaque ) && opaque._format == SerializeContext::OpaqueFormat::Xml )
                    {
                        _backend.writeRawElement( opaque._text );
                        return;
                    }
                    // 다형 원소만 태그 이름이 곧 런타임 타입 정보다.
                    const TypeInfo* pRuntimeType = _ctx.getRuntimeTypeInfo( pObject );
                    if ( pRuntimeType == nullptr )
                        return;
                    _backend.beginMap( pRuntimeType->_name.c_str() );
                    writeXmlProperties( pObject, *pRuntimeType, _backend, _ctx );
                    _backend.endMap();
                }

                void writeValueObject( const void* pValue, const hashed_string&, const TypeInfo& typeInfo, ContainerSlot ) override
                {
                    _backend.beginMap( typeInfo._name.c_str() );
                    writeXmlProperties( pValue, typeInfo, _backend, _ctx );
                    _backend.endMap();
                }

                void writeScalar( const void* pValue, const hashed_string& typeName, const ContainerSlot slot ) override
                {
                    StringBuilder<constant::kMaxBuffer8192> text;
                    SerializerUtil::valueToText( text, pValue, typeName, _ctx );
                    if ( slot == ContainerSlot::SequenceElement )
                        _backend.writeValue( kXmlItemTag, text.c_str() );
                    else
                        _backend.writeText( text.c_str() );
                }

            private:
                IXmlBackend&            _backend;
                const SerializeContext& _ctx;
            };

            /** @brief 지금 노드 "안의" 컨테이너를 읽습니다 — 자식 태그 이름에 기대지 않고 차례로 훑습니다(다형 원소만 이름 = 타입). */
            class ContainerReader final : public IContainerReader
            {
            public:
                ContainerReader( IXmlBackend& backend, const SerializeContext& ctx, vector<SchemaOrphanValue>* pOutListOrphan, const PropertyInfo& propForOrphan )
                    : _backend{ backend }
                    , _ctx{ ctx }
                    , _pOutListOrphan{ pOutListOrphan }
                    , _propForOrphan{ propForOrphan }
                    , _currentTagName{}
                {
                }

                // 원소 수는 미리 세지 않는다 — 자식을 차례로 훑는다. 빈 컨테이너도 Read 다.
                ContainerReadResult beginSequence( size_t& outCountHint ) override
                {
                    outCountHint = 0;
                    return ContainerReadResult::Read;
                }

                ContainerReadResult beginMap( size_t& outCountHint ) override
                {
                    outCountHint = 0;
                    return ContainerReadResult::Read;
                }

                ContainerReadResult forEachElement( const ContainerElementVisitDelegate& visit ) override
                {
                    ContainerReadResult total{ ContainerReadResult::Read };
                    size_t              elementIndex{ 0 };
                    // 자식이 없으면 false 다 — 빈 컨테이너라 볼 것이 없다.
                    (void)_backend.iterateChildren( SW_DELEGATE_LAMBDA( XmlChildVisitDelegate, [&]( string_view tagName )
                    {
                        if ( total == ContainerReadResult::StreamBroken )
                            return;
                        _currentTagName = tagName;
                        total           = ContainerVisitor::mergeResult( total, visit( elementIndex++ ) );
                    } ) );
                    return total;
                }

                ContainerReadResult readMapKey( void* pKey, const hashed_string& keyTypeName ) override
                {
                    string keyText;
                    (void)_backend.readAttribute( kXmlKeyAttr, keyText ); // 없으면 빈 키 — 아래 파싱이 거른다
                    return readText( pKey, keyTypeName, keyText );
                }

                ContainerReadResult readOwnedPointer() override
                {
                    // 태그 이름이 런타임 타입이다. 파일의 이름을 전역 이름 표에 넣지 않는다(`findInterned`).
                    const hashed_string typeName = hashed_string::findInterned( _currentTagName );
                    const TypeInfo*     pType    = typeName.empty() ? nullptr : engine::getTypeRegistry().findType( typeName );
                    void*               pObject  = ( pType != nullptr ) ? _ctx.createOwnedPointer( typeName ) : nullptr;
                    if ( pObject == nullptr )
                    {
                        // 모르는(만들 수 없는) 타입이다 — 원문을 맡긴다(다음 저장이 그대로 다시 쓴다). 맡을 곳이 없으면 건너뛴다.
                        string rawXml;
                        if ( _backend.readCurrentNodeXml( rawXml ) )
                        {
                            SerializeContext::OpaqueElementView opaque{};
                            opaque._typeName = _currentTagName;
                            opaque._format   = SerializeContext::OpaqueFormat::Xml;
                            opaque._text     = rawXml;
                            (void)_ctx.keepOpaqueElement( opaque ); // 맡지 못하면 건너뛴다
                        }
                        return ContainerReadResult::Read;
                    }
                    // 원소 안의 못 읽은 칸 — orphan 목록이 있으면 거기 남고 Read, 엄격 읽기면 FieldFailed 다.
                    return toResult( readNestedXmlIntoInstance( pObject, *pType, _backend, _ctx, _pOutListOrphan ) );
                }

                ContainerReadResult readValueObject( void* pValue, const hashed_string&, const TypeInfo& typeInfo, const ContainerSlot slot ) override
                {
                    if ( slot == ContainerSlot::SequenceElement )
                        return toResult( readNestedXmlIntoInstance( pValue, typeInfo, _backend, _ctx, _pOutListOrphan ) );
                    // 맵 값의 구조체는 `<entry>` 안의 `<TypeName>` 자식이다. 자식이 없으면 기본값 그대로 넣는다.
                    if ( _backend.pushFirstChild() == false )
                        return ContainerReadResult::Read;
                    const bool bRead = readNestedXmlIntoInstance( pValue, typeInfo, _backend, _ctx, _pOutListOrphan );
                    _backend.popChild();
                    return toResult( bRead );
                }

                // 시퀀스 원소는 `<item>` 의 글, 맵 값은 `<entry>` 의 글이다 — 둘 다 지금 노드의 글이다.
                ContainerReadResult readScalar( void* pValue, const hashed_string& typeName, ContainerSlot ) override
                {
                    string text;
                    (void)_backend.readText( text ); // 없으면 빈 글 — 아래 파싱이 실패로 알린다
                    return readText( pValue, typeName, text );
                }

                ContainerReadResult skipElement( const size_t elementIndex ) override
                {
                    SW_LOG_WARNING( "'%#' has more elements than it can hold - element %# dropped", _propForOrphan._name.c_str(), elementIndex );
                    string droppedText;
                    (void)_backend.readText( droppedText ); // 구조체 원소면 비어 있다 — 실패로 알리는 것이 목적이다
                    recordDroppedText( _pOutListOrphan, _propForOrphan, droppedText );
                    return ContainerReadResult::FieldFailed;
                }

            private:
                static ContainerReadResult toResult( const bool bRead ) { return bRead ? ContainerReadResult::Read : ContainerReadResult::FieldFailed; }

                /** @brief 글 하나를 값으로 읽습니다. 못 읽으면 그 글을 orphan 으로 남기고 FieldFailed 입니다(조용히 버리지 않는다). */
                ContainerReadResult readText( void* pValue, const hashed_string& typeName, const string& text ) const
                {
                    if ( parseTextValueCoerced( pValue, typeName, text, _ctx ) )
                        return ContainerReadResult::Read;
                    recordDroppedText( _pOutListOrphan, _propForOrphan, text );
                    return ContainerReadResult::FieldFailed;
                }

                IXmlBackend&               _backend;
                const SerializeContext&    _ctx;
                vector<SchemaOrphanValue>* _pOutListOrphan;
                const PropertyInfo&        _propForOrphan;
                string_view                _currentTagName;
            };

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
                        // 프로퍼티 이름이 곧 컨테이너 요소다. 그 안에 원소들이 들어간다.
                        backend.beginMap( prop._name.c_str() );
                        ContainerWriter writer( backend, ctx );
                        ContainerVisitor::write( pPropPtr, prop.getContainerShape(), writer, ctx );
                        backend.endMap();
                    }
                    else
                    {
                        const TypeInfo* pNestedType = SerializerUtil::findNestedObjectType( prop._typeName, ctx );
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

                            // 기본은 "모두 쓴다" 이다. 그래야 파일에 없음과 명시적으로 비어 있음이
                            // 구분된다. 생략해도 좋다고 **스키마가 선언한** 필드만 비었을 때 뺀다.
                            if ( prop._metadata._bSkipIfEmpty == SW_TRUE && ss.size() == 0 )
                                return;

                            backend.writeAttribute( prop._name.c_str(), ss.c_str() );
                        }
                    }
                }, true /* 상속 PROPERTY 포함 */ );
            }

            /**
             * @brief @p pName 이 타입(상속 포함)의 프로퍼티 이름 · 별칭이면 true 입니다. 대소문자를 가리지 않습니다(해시가 그렇다).
             * @details 대소문자만 다른 태그 · 속성은 orphan 이 아니다(JsonSerializer 의 bCaseVariant 와 같다).
             */
            static bool isNameKnown( const TypeInfo& typeInfo, const utf8* pName )
            {
                if ( pName == nullptr )
                    return false;
                const uint32 nameHash = static_cast<uint32>( hashed_string::computeHash( string_view{ pName } ) );
                for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
                {
                    if ( prop.matchesNameHash( nameHash ) )
                        return true;
                }
                return false;
            }

            /**
             * @brief 안쪽 원소(컴포넌트 · 구조체 칸 · 컨테이너의 구조체 원소)를 읽고, orphan 목록이 있으면 그 원소의 모르는 속성 · 자식도 orphan 으로 남깁니다.
             * @details 루트 원소는 `deserializeSoft` 가 따로 훑는다. 안쪽 원소의 모르는 이름은 타입 이름을 붙여 적는다(`SpriteComponent._clipPth`) —
             *          로드가 끝난 뒤 `runSchemaMigrateStep` 이 루트 타입 이름과 함께 경고한다.
             */
            [[nodiscard]] static bool readNestedXmlIntoInstance( void* pInstance, const TypeInfo& typeInfo, IXmlBackend& backend, const SerializeContext& ctx,
                                                                 vector<SchemaOrphanValue>* pOutListOrphan )
            {
                const bool bRead = readXmlIntoInstance( pInstance, typeInfo, backend, ctx, pOutListOrphan );
                if ( pOutListOrphan != nullptr )
                    appendUnknownXmlChildOrphans( backend.getCurrentNode(), typeInfo, backend.ignoresCaseKeys(), pOutListOrphan, typeInfo._name.c_str() );
                return bRead;
            }

            [[nodiscard]] static bool readXmlIntoInstance( void* pInstance, const TypeInfo& typeInfo, IXmlBackend& backend, const SerializeContext& ctx,
                                                           vector<SchemaOrphanValue>* pOutListOrphan )
            {
                bool bFieldError{ false };

                typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
                {
                    if ( prop._metadata._bTransient == SW_TRUE )
                        return;
                    void* pPropPtr = prop.getRawPtr( pInstance );

                    if ( prop._bIsContainer && prop.hasContainerWrapper() )
                    {
                        // 컨테이너는 프로퍼티 이름 요소 안에 들어 있다.
                        const bool entered = tryNameOrAlias( prop, [&]( const utf8* pName )
                        {
                            return backend.pushChild( pName );
                        } );
                        if ( entered )
                        {
                            ContainerReader reader( backend, ctx, pOutListOrphan, prop );
                            if ( ContainerVisitor::read( pPropPtr, prop.getContainerShape(), reader, ctx ) != ContainerReadResult::Read )
                                bFieldError = true;
                            backend.popChild();
                        }
                        else
                            SerializerUtil::applyPropertyDefault( prop, pInstance, ctx );
                    }
                    else
                    {
                        const TypeInfo* pNestedType = SerializerUtil::findNestedObjectType( prop._typeName, ctx );
                        if ( pNestedType != nullptr )
                        {
                            const bool entered = tryNameOrAlias( prop, [&]( const utf8* pName )
                            {
                                return backend.pushChild( pName );
                            } );
                            if ( entered )
                            {
                                if ( readNestedXmlIntoInstance( pPropPtr, *pNestedType, backend, ctx, pOutListOrphan ) == false )
                                    bFieldError = true;
                                backend.popChild();
                            }
                            else
                                SerializerUtil::applyPropertyDefault( prop, pInstance, ctx );
                            return;
                        }

                        string     strValue;
                        const bool readOk = tryNameOrAlias( prop, [&]( const utf8* pName )
                        {
                            return backend.readAttribute( pName, strValue );
                        } );

                        if ( readOk )
                        {
                            // 비트필드는 그 비트만 쓴다. 불리언이 아닌 글("ture")은 false 로 삼키지 않고 실패로 남긴다.
                            if ( prop._bIsBitField == SW_TRUE )
                            {
                                if ( SerializerUtil::applyPropertyText( prop, pInstance, strValue, ctx ) == false )
                                {
                                    bFieldError = true;
                                    recordDroppedText( pOutListOrphan, prop, strValue );
                                }
                            }
                            else if ( parseTextValueCoerced( pPropPtr, prop._typeName, strValue, ctx ) == false )
                            {
                                bFieldError = true;
                                recordDroppedText( pOutListOrphan, prop, strValue );
                            }
                        }
                        else
                            SerializerUtil::applyPropertyDefault( prop, pInstance, ctx );
                    }
                }, true /* 상속 PROPERTY 포함 */ );

                if ( pOutListOrphan != nullptr )
                    return true;
                return bFieldError == false;
            }

            /** @brief 파일에 적힌 모르는 이름 하나를 orphan 으로 남깁니다. @p pOwnerTypeName 이 있으면 경고에 찍을 이름 앞에 붙입니다. */
            static void appendUnknownNameOrphan( const utf8* pWrittenName, const utf8* pText, const utf8* pOwnerTypeName,
                                                 vector<SchemaOrphanValue>& outListOrphan )
            {
                // 파일의 모르는 이름을 전역 이름 표에 넣지 않는다 — 아는 이름이면 그것을, 아니면 해시만 든다(`SchemaMigrateContext::findOrphan`).
                SchemaOrphanValue orphan;
                orphan._name     = hashed_string::findInterned( string_view{ pWrittenName } );
                orphan._nameHash = hashed_string::computeHash( string_view{ pWrittenName } );
                orphan._text     = pText != nullptr ? pText : "";
                // 버렸다고 알릴 때 찍을 이름 — 위의 `_name` 은 intern 된 이름일 때만 찬다
                if ( StringUtil::isNullOrEmpty( pOwnerTypeName ) == false )
                {
                    orphan._writtenName = pOwnerTypeName;
                    orphan._writtenName += '.';
                }
                orphan._writtenName += pWrittenName;
                outListOrphan.push_back( std::move( orphan ) );
            }

            /**
             * @brief 원소 @p node 의 속성 · 자식 가운데 타입이 모르는 이름을 orphan 으로 남깁니다.
             * @param pOwnerTypeName 안쪽 원소면 그 타입 이름(경고에 붙는다), 루트면 nullptr 입니다.
             */
            static void appendUnknownXmlChildOrphans( XmlNode node, const TypeInfo& typeInfo, bool bIgnore,
                                                      vector<SchemaOrphanValue>* pOutListOrphan, const utf8* pOwnerTypeName = nullptr )
            {
                if ( pOutListOrphan == nullptr || node.isValid() == false )
                    return;

                // 대소문자만 다른 태그는 setIgnoreCaseKeys(false) 로 의도적으로 바인딩을 거른 것이므로
                // 모르는 필드(orphan)로 올리지 않는다. bIgnore 와 무관하게 무시 대소문자로 판정한다.
                for ( XmlNode child = node.findChild( nullptr, bIgnore ); child.isValid(); child = child.findNextSibling( nullptr, bIgnore ) )
                {
                    const utf8* pChildName = child.getName();
                    if ( pChildName == nullptr )
                        continue;
                    if ( StringUtil::equals( pChildName, kSchemaVersionKey ) )
                        continue;
                    if ( isNameKnown( typeInfo, pChildName ) )
                        continue;
                    const utf8* pNameAttr = child.findAttribute( kXmlPropertyNameAttr, bIgnore );
                    if ( pNameAttr != nullptr && isNameKnown( typeInfo, pNameAttr ) )
                        continue;
                    appendUnknownNameOrphan( pChildName, child.getText(), pOwnerTypeName, *pOutListOrphan );
                }

                for ( XmlAttribute attr = node.getFirstAttribute(); attr.isValid(); attr = attr.getNext() )
                {
                    const utf8* pAttrName = attr.getName();
                    if ( pAttrName == nullptr )
                        continue;
                    if ( StringUtil::equals( pAttrName, kSchemaVersionKey ) )
                        continue;
                    if ( isNameKnown( typeInfo, pAttrName ) )
                        continue;
                    appendUnknownNameOrphan( pAttrName, attr.getValue(), pOwnerTypeName, *pOutListOrphan );
                }
            }

            [[nodiscard]] static bool tryAppendUnknownXmlChildOrphans( string_view xmlStr, const TypeInfo& typeInfo,
                                                                       const SerializeContext& ctx, vector<SchemaOrphanValue>* pOutListOrphan )
            {
                if ( xmlStr.empty() || pOutListOrphan == nullptr )
                    return false;

                XmlDocument doc;
                if ( doc.parse( xmlStr ) == false )
                    return false;

                const bool bIgnore = ctx.ignoresCaseKeys();
                XmlNode    root    = doc.getRoot( typeInfo._name.c_str(), bIgnore );
                if ( root.isValid() == false )
                    root = doc.getRoot( nullptr, bIgnore );
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

    void XmlDocumentBackend::initializeXmlSerialization( const utf8* pRootTagName )
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
        _impl->_currentParent.appendAttribute( sName.c_str(), pValueString != nullptr ? pValueString : "" );
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

    XmlNode IXmlBackend::getCurrentNode() const
    {
        return XmlNode{};
    }

    XmlNode XmlDocumentBackend::getCurrentNode() const
    {
        return _impl->_currentParent;
    }

    XmlNode XmlDocumentBackend::getDeserializationRoot() const
    {
        // 스택의 바닥이 `initializeXmlDeserialization` 이 찾은 루트다. `pushChild` 로 내려가
        // 있어도 루트는 그대로 바닥에 있다.
        if ( _impl->_listNodeStack.empty() )
            return XmlNode{};
        return _impl->_listNodeStack.front();
    }

    bool XmlDocumentBackend::initializeXmlDeserialization( string_view xmlStr, const utf8* pRootTagName )
    {
        _impl->_doc.clear();
        if ( xmlStr.empty() )
            return false;

        if ( _impl->_doc.parse( xmlStr ) == false )
            return false;

        string  sTag = Impl::sanitizeTag( pRootTagName );
        XmlNode root = _impl->_doc.getRoot( sTag.c_str(), ignoresCaseKeys() );
        if ( root.isValid() == false )
            root = _impl->_doc.getRoot( nullptr, ignoresCaseKeys() );

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
        XmlNode node = _impl->_currentParent.findChild( sTag.c_str(), ignoresCaseKeys() );
        if ( node.isValid() == false )
            return false;

        outValue = node.getText() != nullptr ? node.getText() : "";
        return true;
    }

    bool XmlDocumentBackend::readAttribute( const utf8* pAttrName, string& outValue )
    {
        if ( _impl->_currentParent.isValid() == false )
            return false;

        string      sName = Impl::sanitizeTag( pAttrName );
        const utf8* pVal  = _impl->_currentParent.findAttribute( sName.c_str(), ignoresCaseKeys() );
        if ( pVal == nullptr )
            return false;

        outValue = pVal;
        return true;
    }

    bool XmlDocumentBackend::pushChild( const utf8* pTagName )
    {
        if ( _impl->_currentParent.isValid() == false )
            return false;

        string  sTag  = Impl::sanitizeTag( pTagName );
        XmlNode child = _impl->_currentParent.findChild( sTag.c_str(), ignoresCaseKeys() );
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
        XmlNode child = _impl->_currentParent.findChild( nullptr, ignoresCaseKeys() );
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
        const utf8* pText = _impl->_currentParent.getText();
        outText           = ( pText != nullptr ) ? pText : "";
        return true;
    }

    bool XmlDocumentBackend::readCurrentNodeXml( string& outXml )
    {
        if ( _impl->_currentParent.isValid() == false )
            return false;
        outXml = _impl->_currentParent.toString();
        return outXml.empty() == false;
    }

    void XmlDocumentBackend::writeRawElement( string_view xml )
    {
        if ( _impl->_currentParent.isValid() == false || xml.empty() )
            return;
        XmlDocument rawDoc;
        if ( rawDoc.parse( xml, "<kept element>" ) == false )
            return;
        const XmlNode rawRoot = rawDoc.getRoot();
        if ( rawRoot.isValid() )
            (void)_impl->_currentParent.appendClone( rawRoot );
    }

    bool XmlDocumentBackend::iterateChildren( const XmlChildVisitDelegate& callback )
    {
        if ( _impl->_currentParent.isValid() == false || callback.isBound() == false )
            return false;

        const XmlNode parent = _impl->_currentParent;
        bool          bAny{ false };
        for ( XmlNode child = parent.findChild( nullptr, ignoresCaseKeys() ); child.isValid(); child = child.findNextSibling( nullptr, ignoresCaseKeys() ) )
        {
            // 콜백이 도는 동안 그 자식이 현재 노드가 되어야 재귀 순회가 가능하다.
            _impl->_listNodeStack.push_back( child );
            _impl->_currentParent = child;

            const utf8* pName = child.getName();
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
        backend.initializeXmlSerialization( typeInfo._name.c_str() );
        XmlSerializerInternal::writeXmlProperties( pInstance, typeInfo, backend, ctx );
        return backend.endSerialize();
    }

    bool XmlSerializer::deserialize( void* pInstance, const TypeInfo& typeInfo,
                                     IXmlBackend& backend, string_view xmlStr,
                                     const SerializeContext& ctx )
    {
        if ( xmlStr.empty() )
            return false;

        backend.setIgnoreCaseKeys( ctx.ignoresCaseKeys() );

        if ( backend.initializeXmlDeserialization( xmlStr, typeInfo._name.c_str() ) == false )
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

        // **문서 하나로 셋을 다 한다.** 버전 속성 · 값 읽기 · orphan 자식 훑기 — 같은 문자열을 두 번 파싱하지 않는다.
        // 씬 · 프리팹 로드가 엔티티마다 이 경로로 간다(형제 `JsonSerializer::deserializeSoft` 도 문서 하나만 쓴다).
        const bool         bIgnore = ctx.ignoresCaseKeys();
        XmlDocumentBackend backend;
        backend.setIgnoreCaseKeys( bIgnore );
        if ( backend.initializeXmlDeserialization( xmlStr, typeInfo._name.c_str() ) == false )
            return false;

        const XmlNode root = backend.getDeserializationRoot();
        if ( root.isValid() == false )
            return false;

        if ( pOutVersion != nullptr )
        {
            *pOutVersion     = 0;
            const utf8* pVer = root.findAttribute( kSchemaVersionKey, bIgnore );
            if ( pVer != nullptr )
            {
                uint64 ver{ 0 };
                if ( StringUtil::parseUint64( pVer, ver, 10 ) == false )
                    SW_LOG_WARNING( "_schemaVersion '%#' is not a number - reading as version 0", pVer );
                *pOutVersion = static_cast<uint32>( ver );
            }
        }

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
        backend.initializeXmlSerialization( typeInfo._name.c_str() );
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
        // 절차는 JSON · XML · Binary 가 공통이다(`runVersionedDeserialize`). 여기서 정하는 것은 두 가지뿐이다.
        // **버전이 어디서 오는가**(본문 안의 `_schemaVersion`)와 **orphan 만 있을 때의 정책**이다.
        return runVersionedDeserialize(
            outVersion, pInstance, typeInfo, currentVersion, migrate, pLegacyTypeInfo, ctx,
            SchemaVersionSource::Payload, SchemaOrphanPolicy::Ignore,
            SW_DELEGATE_LAMBDA( SoftDeserializeFn,
                                [&]( void* pTarget, const TypeInfo& targetType, vector<SchemaOrphanValue>& listOrphan, uint32& outSoftVersion ) -> bool
        {
            return deserializeSoft( pTarget, targetType, xmlStr, &listOrphan, &outSoftVersion, ctx );
        } ) );
    }

} // namespace sw
