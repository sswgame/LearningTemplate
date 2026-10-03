#include "pch.h"

#include "Engine/Serialization/Format/XmlSerializer.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
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
             * @details 자식 요소 들어가기(컨테이너 · 중첩 구조체)와 속성 읽기가 같은 "이름 → 별칭" 루프를 세 벌 들고 있었습니다.
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

            static void recordCoerceFailure( vector<SchemaOrphanValue>* pOutListOrphan, bool& bFieldError, const PropertyInfo& prop, string_view strValue )
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
             * @details 시퀀스 원소는 구조체면 타입 이름 태그, 그 외에는 <item> 입니다.
             *          맵 항목은 <entry key="K"> 입니다. 값은 그 노드 안에 같은 규칙으로 재귀합니다.
             *          리더는 태그 이름에 의존하지 않으므로(다형 포인터 제외) 얼마든지 중첩할 수 있습니다.
             */
            static void writeContainerXml( const void* pContainerPtr, const NestedContainerInfo& nested,
                                           IXmlBackend& backend, const SerializeContext& ctx )
            {
                if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
                    return;

                const bool                 bOwnedPtr = SerializerUtil::isOwnedPointerElementType( nested._elementTypeName );
                ISequenceContainerWrapper* pSeq      = nested._wrapper->asSequence();
                IMapContainerWrapper*      pMapWrap  = nested._wrapper->asMap();

                if ( pSeq != nullptr )
                {
                    const size_t elementCount = pSeq->getSize( pContainerPtr );
                    for ( size_t elemIndex = 0; elemIndex < elementCount; ++elemIndex )
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
                            // 맡아 둔 원소(모르는 타입)는 읽은 원문 그대로 다시 쓴다.
                            SerializeContext::OpaqueElementView opaque{};
                            if ( ctx.queryOpaqueElement( pObj, opaque ) && opaque._format == SerializeContext::OpaqueFormat::Xml )
                            {
                                backend.writeRawElement( opaque._text );
                                continue;
                            }
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
                            const TypeInfo* pElemType = SerializerUtil::findNestedObjectType( nested._elementTypeName, ctx );
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
                            const TypeInfo* pElemType = SerializerUtil::findNestedObjectType( nested._elementTypeName, ctx );
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
             *          원소가 또 컨테이너면 그 자식 노드에서 재귀하므로 얼마든지 중첩할 수 있습니다.
             */
            [[nodiscard]] static bool readContainerXml( void* pContainerPtr, const NestedContainerInfo& nested, IXmlBackend& backend,
                                                        const SerializeContext& ctx, bool& bOutFieldError,
                                                        vector<SchemaOrphanValue>* pOutListOrphan, const PropertyInfo& propForOrphan )
            {
                if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
                    return false;

                const bool bOwnedPtr = SerializerUtil::isOwnedPointerElementType( nested._elementTypeName );
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
                            const hashed_string typeName = hashed_string::findInterned( tagName );
                            const TypeInfo*     pType    = typeName.empty() ? nullptr : engine::getTypeRegistry().findType( typeName );
                            void*               pObj     = ( pType != nullptr ) ? ctx.createOwnedPointer( typeName ) : nullptr;
                            if ( pObj == nullptr || pType == nullptr )
                            {
                                // 모르는(만들 수 없는) 타입이다 — 원문을 맡긴다(다음 저장이 그대로 다시 쓴다). 맡을 곳이 없으면 예전처럼 건너뛴다.
                                string rawXml;
                                if ( backend.readCurrentNodeXml( rawXml ) )
                                {
                                    SerializeContext::OpaqueElementView opaque{};
                                    opaque._typeName = tagName;
                                    opaque._format   = SerializeContext::OpaqueFormat::Xml;
                                    opaque._text     = rawXml;
                                    (void)ctx.keepOpaqueElement( opaque ); // 맡지 못하면 건너뛴다(예전과 같다)
                                }
                                return;
                            }
                            // 원소 안의 못 읽은 칸 — orphan 목록이 있으면 거기 남고 true, 엄격 읽기면 false 다.
                            if ( readNestedXmlIntoInstance( pObj, *pType, backend, ctx, pOutListOrphan ) == false )
                                bOutFieldError = true;
                            return;
                        }

                        // 읽기는 여기서, **넣는 방법은 컨테이너가** 정한다. `set` 은 다 읽은 뒤 insert 해야 한다.
                        const size_t elementOrdinal = elemIndex++;
                        const bool   bAppended      = pSeq->appendElement( pContainerPtr, elementOrdinal, SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElemPtr ) -> bool
                               {
                            if ( nested._elementNested != nullptr )
                            {
                                // 반환값은 "원소가 하나라도 있었나" 다 — 빈 안쪽 컨테이너는 실패가 아니다. 칸 실패는 bOutFieldError 로 온다.
                                (void)readContainerXml( pElemPtr, *nested._elementNested, backend, ctx, bOutFieldError, pOutListOrphan, propForOrphan );
                                return true;
                            }

                            const TypeInfo* pElemType = SerializerUtil::findNestedObjectType( nested._elementTypeName, ctx );
                            if ( pElemType != nullptr )
                            {
                                if ( readNestedXmlIntoInstance( pElemPtr, *pElemType, backend, ctx, pOutListOrphan ) == false )
                                    bOutFieldError = true;
                                return true;
                            }

                            string itemText;
                            (void)backend.readText( itemText ); // 없으면 빈 글 — 아래 파싱이 실패로 알린다
                            if ( parseTextValueCoerced( pElemPtr, nested._elementTypeName, itemText, ctx ) == false )
                                recordCoerceFailure( pOutListOrphan, bOutFieldError, propForOrphan, itemText );
                            return true;
                        } ) );
                        // 넣을 자리가 없다(고정 배열보다 원소가 많다) — 버리되 그 글을 orphan 으로 남겨 실패로 알린다.
                        if ( bAppended == false )
                        {
                            SW_LOG_WARNING( "'%#' has more elements than it can hold - element %# dropped", propForOrphan._name.c_str(), elementOrdinal );
                            string droppedText;
                            (void)backend.readText( droppedText ); // 구조체 원소면 비어 있다 — 실패로 알리는 것이 목적이다
                            recordCoerceFailure( pOutListOrphan, bOutFieldError, propForOrphan, droppedText );
                        }
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
                        (void)backend.readAttribute( kXmlKeyAttr, keyText ); // 없으면 빈 키 — 아래 키 파싱이 거른다

                        pMapWrap->defaultConstructKey( listKBuf.data() );
                        pMapWrap->defaultConstructValue( listVBuf.data() );

                        const bool kOk = parseTextValueCoerced( listKBuf.data(), nested._keyTypeName, keyText, ctx );
                        bool       vOk{ true };
                        if ( nested._elementNested != nullptr )
                        {
                            // 반환값은 "원소가 하나라도 있었나" 다 — 빈 안쪽 컨테이너는 실패가 아니다. 칸 실패는 bOutFieldError 로 온다.
                            (void)readContainerXml( listVBuf.data(), *nested._elementNested, backend, ctx, bOutFieldError, pOutListOrphan, propForOrphan );
                        }
                        else
                        {
                            const TypeInfo* pElemType = SerializerUtil::findNestedObjectType( nested._elementTypeName, ctx );
                            if ( pElemType != nullptr )
                            {
                                // 구조체 값은 <entry> 안의 <TypeName> 자식에 들어 있다.
                                if ( backend.pushFirstChild() )
                                {
                                    vOk = readNestedXmlIntoInstance( listVBuf.data(), *pElemType, backend, ctx, pOutListOrphan );
                                    if ( vOk == false )
                                        bOutFieldError = true;
                                    backend.popChild();
                                }
                            }
                            else
                            {
                                string valText;
                                (void)backend.readText( valText ); // 없으면 빈 글 — 아래 파싱이 실패로 알린다
                                vOk = parseTextValueCoerced( listVBuf.data(), nested._elementTypeName, valText, ctx );
                                if ( vOk == false )
                                    recordCoerceFailure( pOutListOrphan, bOutFieldError, propForOrphan, valText );
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
                        NestedContainerInfo shape = prop.getContainerShape();
                        if ( shape._typeName.empty() )
                            shape._typeName = prop._typeName;
                        // 컨테이너는 프로퍼티 이름 요소 안에 들어 있다.
                        const bool entered = tryNameOrAlias( prop, [&]( const utf8* pName )
                        {
                            return backend.pushChild( pName );
                        } );
                        if ( entered )
                        {
                            if ( readContainerXml( pPropPtr, shape, backend, ctx, bFieldError, pOutListOrphan, prop ) == false )
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
                            // 비트필드는 그 비트만 쓴다. 불리언이 아닌 글("ture")은 예전처럼 false 로 삼키지 않고 실패로 남긴다.
                            if ( prop._bIsBitField == SW_TRUE )
                            {
                                if ( SerializerUtil::applyPropertyText( prop, pInstance, strValue, ctx ) == false )
                                    recordCoerceFailure( pOutListOrphan, bFieldError, prop, strValue );
                            }
                            else if ( parseTextValueCoerced( pPropPtr, prop._typeName, strValue, ctx ) == false )
                                recordCoerceFailure( pOutListOrphan, bFieldError, prop, strValue );
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

        // **문서 하나로 셋을 다 한다.** 버전 속성 · 값 읽기 · orphan 자식 훑기.
        // 예전에는 여기서 자기 `XmlDocument` 를 따로 파싱하고, 아래 백엔드가 **같은 문자열을
        // 한 번 더** 파싱했다. 씬 · 프리팹 로드가 엔티티마다 이 경로로 간다. 형제인
        // `JsonSerializer::deserializeSoft` 는 처음부터 문서 하나만 쓴다.
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
