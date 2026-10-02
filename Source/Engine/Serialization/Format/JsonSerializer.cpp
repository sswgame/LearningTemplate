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
            static void writeJsonValue( JsonValue dst, const void* pValPtr, const hashed_string& typeName, const SerializeContext& ctx )
            {
                if ( dst.isValid() == false )
                    return;

                const hashed_string resolved  = SerializerUtil::resolveHandlerTypeName( typeName, ctx );
                const bool          bIsString = ( resolved.isPredefinedType( PredefinedNameType::NameType_string ) ||
                                         resolved.isPredefinedType( PredefinedNameType::NameType_hashed_string ) ||
                                         resolved.isPredefinedType( PredefinedNameType::NameType_TagID ) );
                const bool          bIsBool   = ( resolved.isPredefinedType( PredefinedNameType::NameType_bool ) ||
                                       resolved.isPredefinedType( PredefinedNameType::NameType_atomic_bool ) );
                const bool          bIsEnum   = ( engine::getTypeRegistry().findEnum( typeName ) != nullptr );

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
                    if ( parsed.tryParse( text.view() ) && parsed.getRoot().isObject() == false && parsed.getRoot().isArray() == false )
                    {
                        dst.assignFrom( parsed.getRoot() );
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
             * @brief 컨테이너 원소 하나를 dst 에 씁니다(중첩 컨테이너 / 구조체 / 소유 포인터 / 값).
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
                    void*        pObj  = ppObj != nullptr ? *ppObj : nullptr;
                    if ( pObj == nullptr )
                    {
                        dst.setObject();
                        return;
                    }
                    // 맡아 둔 원소(모르는 타입)는 읽은 원문 그대로 다시 쓴다.
                    SerializeContext::OpaqueElementView opaque{};
                    if ( ctx.queryOpaqueElement( pObj, opaque ) && opaque._format == SerializeContext::OpaqueFormat::Json )
                    {
                        JsonDocument rawDoc;
                        if ( rawDoc.tryParse( opaque._text ) )
                        {
                            dst.assignFrom( rawDoc.getRoot() );
                            return;
                        }
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

                const TypeInfo* pElemType = SerializerUtil::findNestedObjectType( nested._elementTypeName, ctx );
                if ( pElemType != nullptr )
                {
                    // 값 구조체는 타입 래핑 없이 본문을 그대로 쓴다(리더가 양쪽 다 받는다).
                    JsonSerializer::writeObject( dst, pElemPtr, *pElemType, ctx );
                    return;
                }

                writeJsonValue( dst, pElemPtr, nested._elementTypeName, ctx );
            }

            /**
             * @brief 컨테이너를 자연스러운 JSON 표현으로 씁니다. 시퀀스는 배열, 맵은 오브젝트입니다.
             */
            static void writeContainerValueJson( JsonValue dst, const void* pContainerPtr, const NestedContainerInfo& nested,
                                                 const SerializeContext& ctx )
            {
                if ( dst.isValid() == false || pContainerPtr == nullptr || nested._wrapper == nullptr )
                    return;

                const bool bOwnedPtr = SerializerUtil::isOwnedPointerElementType( nested._elementTypeName );

                ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
                if ( pSeq != nullptr )
                {
                    dst.setArray();
                    const size_t elementCount = pSeq->getSize( pContainerPtr );
                    for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
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

            // items(JSON 배열)의 각 원소를 시퀀스 컨테이너에 채운다. 부르는 쪽이 미리 wrapper->clear() 를 한다.
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
                        const vector<string> listKey = elem.getMemberNames();
                        if ( listKey.size() != 1 )
                            continue;
                        const hashed_string typeName = hashed_string::findInterned( listKey[0] );
                        const TypeInfo*     pType    = typeName.empty() ? nullptr : engine::getTypeRegistry().findType( typeName );
                        void*               pObj     = ( pType != nullptr ) ? ctx.createOwnedPointer( typeName ) : nullptr;
                        if ( pObj == nullptr )
                        {
                            // 모르는(만들 수 없는) 타입이다 — 원문을 맡긴다(다음 저장이 그대로 다시 쓴다). 맡을 곳이 없으면 예전처럼 건너뛴다.
                            const string                        rawJson = elem.dump();
                            SerializeContext::OpaqueElementView opaque{};
                            opaque._typeName = listKey[0];
                            opaque._format   = SerializeContext::OpaqueFormat::Json;
                            opaque._text     = rawJson;
                            (void)ctx.keepOpaqueElement( opaque ); // 맡지 못하면 건너뛴다(예전과 같다)
                            continue;
                        }
                        if ( JsonSerializer::readObject( elem.get( listKey[0], false ), pObj, *pType, nullptr, nullptr, ctx ) == false )
                            bOk = false;
                    }
                    return bOk;
                }

                pSeq->reserve( pContainerPtr, items.size() );
                bool bOk{ true };

                // 읽기는 여기서, **넣는 방법은 컨테이너가** 정한다. `set` 은 다 읽은 뒤 insert 해야 한다.
                for ( size_t elementIndex = 0; elementIndex < items.size(); ++elementIndex )
                {
                    const JsonValue elem = items.at( elementIndex );

                    const bool bAppended = pSeq->appendElement( pContainerPtr, SW_DELEGATE_LAMBDA( ElementFillDelegate,
                                                                                                   [&]( void* pElemPtr ) -> bool
                    {
                        if ( nested._elementNested != nullptr )
                            return readTypedContainerJson( pElemPtr, *nested._elementNested, elem, ctx );

                        const TypeInfo* pElemType = SerializerUtil::findNestedObjectType( nested._elementTypeName, ctx );
                        if ( pElemType == nullptr )
                            return readJsonValue( pElemPtr, nested._elementTypeName, elem, ctx );

                        if ( elem.isObject() == false )
                            return false;

                        // 래핑 형식 { "TypeName": {body} } 이면 그 안을 읽고, 아니면 elem 자체를 body 로 본다.
                        const vector<string> listMember = elem.getMemberNames();
                        const bool           bWrapped =
                            ( listMember.size() == 1 &&
                              engine::getTypeRegistry().findType( hashed_string::findInterned( listMember[0] ) ) != nullptr );
                        const JsonValue body = bWrapped ? elem.get( listMember[0], false ) : elem;
                        return JsonSerializer::readObject( body, pElemPtr, *pElemType, nullptr, nullptr, ctx );
                    } ) );

                    if ( bAppended == false )
                        bOk = false;
                }
                return bOk;
            }

            // entries(JSON 오브젝트)의 각 멤버를 맵 컨테이너에 채운다. 부르는 쪽이 미리 wrapper->clear() 를 한다.
            static bool readMapEntriesJson( void* pContainerPtr, const NestedContainerInfo& nested, const JsonValue& entries,
                                            const SerializeContext& ctx )
            {
                IMapContainerWrapper* pMapWrap = nested._wrapper != nullptr ? nested._wrapper->asMap() : nullptr;
                if ( pMapWrap == nullptr || entries.isObject() == false )
                    return false;

                vector<uint8> listKBuf( pMapWrap->getKeySize() );
                vector<uint8> listVBuf( pMapWrap->getValueSize() );
                for ( const string& key : entries.getMemberNames() )
                {
                    pMapWrap->defaultConstructKey( listKBuf.data() );
                    pMapWrap->defaultConstructValue( listVBuf.data() );
                    JsonDocument keyDoc;
                    keyDoc.getRoot().setString( key );
                    bool                                kOk{ false };
                    const SerializeContext::TextReadFn* pKeyReader = ctx.findTextReader( nested._keyTypeName );
                    if ( pKeyReader != nullptr )
                        kOk = ( *pKeyReader )( listKBuf.data(), key );
                    else
                        kOk = readJsonValue( listKBuf.data(), nested._keyTypeName, keyDoc.getRoot(), ctx );

                    bool            vOk{ false };
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

            /**
             * @brief 컨테이너를 자연스러운 JSON 표현으로 읽습니다. 시퀀스는 배열, 맵은 오브젝트입니다.
             * @details 원소가 또 컨테이너면 그 값에서 재귀하므로 얼마든지 중첩할 수 있습니다.
             */
            static bool readTypedContainerJson( void* pContainerPtr, const NestedContainerInfo& nested, const JsonValue& src,
                                                const SerializeContext& ctx )
            {
                if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
                    return false;

                if ( src.isArray() && nested._wrapper->asSequence() != nullptr )
                {
                    const bool bOwnedPtr = SerializerUtil::isOwnedPointerElementType( nested._elementTypeName );
                    if ( bOwnedPtr == false )
                        nested._wrapper->clear( pContainerPtr );
                    return readSequenceItemsJson( pContainerPtr, nested, src, bOwnedPtr, ctx );
                }

                if ( src.isObject() && nested._wrapper->asMap() != nullptr )
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

                const hashed_string                 resolved    = SerializerUtil::resolveHandlerTypeName( typeName, ctx );
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
                    // 모르는 이름 · 값이면 쓰지 않고 실패한다(XML 과 같다 — `EnumInfo::tryParseText`). 예전에는 0 을 썼다.
                    int64        parsedValue{ 0 };
                    const string text = src.isString() ? string( src.asString() ) : sw::to_string( src.asInt( 0 ) );
                    if ( ( src.isString() || src.isNumber() ) && pEnumInfo->tryParseText( text, parsedValue ) )
                    {
                        pEnumInfo->writeValueToMemory( pValPtr, parsedValue );
                        return true;
                    }
                    SW_LOG_WARNING( "'%#' is not a value of enum %# - the field keeps its current value", text, pEnumInfo->_fullyQualifiedName.c_str() );
                    return false;
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

            static bool readProperty( const JsonValue& field, const PropertyInfo& prop, void* pInstance, const SerializeContext& ctx )
            {
                if ( prop._bIsBitField == SW_TRUE )
                {
                    // 불리언 · 숫자 · 불리언 글만 받는다. 예전에는 그 밖의 것("ture" · 오브젝트 · null)을 조용히 false 로 썼다.
                    bool bVal = false;
                    if ( field.isBool() )
                        bVal = field.asBool();
                    else if ( field.isNumber() )
                        bVal = ( field.asInt() != 0 );
                    else if ( field.isString() == false || StringUtil::tryParseBool( field.asString(), bVal ) == false )
                        return false;
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
        return readObject( doc.getRoot(), pInstance, typeInfo, nullptr, nullptr, ctx );
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
        }, true /* 상속 PROPERTY 포함 */ );
    }

    bool JsonSerializer::readObject( JsonValue src, void* pInstance, const TypeInfo& typeInfo,
                                     vector<SchemaOrphanValue>* pOutListOrphan, uint32* pOutVersion,
                                     const SerializeContext& ctx )
    {
        if ( pInstance == nullptr || src.isObject() == false )
            return false;

        const bool                  bIgnoreCaseKeys = ctx.ignoresCaseKeys();
        const vector<PropertyInfo>& listProp        = typeInfo.getPropertiesWithBase();
        unordered_set<uint32>       uniqueMatched;
        bool                        bFieldError{ false };

        if ( pOutVersion != nullptr )
            *pOutVersion = 0;

        for ( const string& keyRaw : src.getMemberNames() )
        {
            const JsonValue field = src.get( keyRaw, false );
            if ( SerializerUtil::keysEqual( keyRaw, kSchemaVersionKey, bIgnoreCaseKeys ) )
            {
                if ( pOutVersion != nullptr )
                    *pOutVersion = static_cast<uint32>( field.asUint( 0 ) );
                continue;
            }
            bool                bCaseVariant{ false };
            const PropertyInfo* pMatched = SerializerUtil::matchProperty( listProp, keyRaw, bIgnoreCaseKeys, bCaseVariant );
            if ( pMatched == nullptr && bCaseVariant )
                continue;

            if ( pMatched == nullptr || pMatched->_metadata._bTransient == SW_TRUE )
            {
                if ( pOutListOrphan != nullptr )
                {
                    // 파일의 모르는 키를 전역 이름 표에 넣지 않는다(XML 과 같다 — 아는 이름이면 그것을, 아니면 해시만).
                    SchemaOrphanValue orphan;
                    orphan._name     = hashed_string::findInterned( string_view{ keyRaw.c_str(), keyRaw.size() } );
                    orphan._nameHash = hashed_string::computeHash( string_view{ keyRaw.c_str(), keyRaw.size() } );
                    orphan._text     = field.dump();
                    // 버렸다고 알릴 때 찍을 이름 — 위의 `_name` 은 intern 된 이름일 때만 찬다
                    orphan._writtenName = keyRaw;
                    pOutListOrphan->push_back( std::move( orphan ) );
                }
                else
                    bFieldError = true;
                continue;
            }

            uniqueMatched.insert( pMatched->getNameHash() );
            if ( JsonSerializerInternal::readProperty( field, *pMatched, pInstance, ctx ) == false )
            {
                if ( pOutListOrphan != nullptr )
                {
                    SchemaOrphanValue orphan;
                    orphan._name     = pMatched->_name;
                    orphan._nameHash = pMatched->getNameHash();
                    orphan._text     = field.dump();
                    pOutListOrphan->push_back( std::move( orphan ) );
                }
                else
                    bFieldError = true;
            }
        }

        for ( const PropertyInfo& prop : listProp )
        {
            if ( uniqueMatched.find( prop.getNameHash() ) != uniqueMatched.end() )
                continue;
            SerializerUtil::applyPropertyDefault( prop, pInstance, ctx );
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
        return readObject( doc.getRoot(), pInstance, typeInfo, pOutListOrphan, pOutVersion, ctx );
    }

    string JsonSerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
                                               const SerializeContext& ctx )
    {
        JsonDocument doc;
        JsonValue    root = doc.makeObject();
        root.set( kSchemaVersionKey, false ).setUint( version );
        typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
        {
            if ( prop._metadata._bTransient == SW_TRUE )
                return;
            JsonSerializerInternal::writeProperty( root, prop, pInstance, ctx );
        }, true /* 상속 PROPERTY 포함 */ );
        return doc.dump();
    }

    bool JsonSerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
                                               string_view jsonStr, uint32 currentVersion, SchemaMigrateFn migrate,
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
            return deserializeSoft( pTarget, targetType, jsonStr, &listOrphan, &outSoftVersion, ctx );
        } ) );
    }

} // namespace sw
