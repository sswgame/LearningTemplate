#include "pch.h"

#include "Engine/Serialization/Format/JsonSerializer.h"

#include "Core/Container/InlineAllocator.h"
#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/ContainerVisitor.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
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

            /** @brief 컨테이너를 자연스러운 JSON 으로 적습니다 — 시퀀스는 배열, 맵은 오브젝트, 소유 포인터 원소만 `{ "TypeName": {...} }` 로 감쌉니다. */
            class ContainerWriter final : public IContainerWriter
            {
            public:
                ContainerWriter( const JsonValue& root, const SerializeContext& ctx )
                    : _listSlot{}
                    , _ctx{ ctx }
                {
                    _listSlot.push_back( root );
                }

                void beginSequence( size_t ) override { _listSlot.back().setArray(); }
                void endSequence() override {}
                void beginMap( size_t ) override { _listSlot.back().setObject(); }
                void endMap() override {}

                // `JsonValue` 는 빌린 포인터다 — 같은 부모에 새 키 · 원소를 더하면 앞서 꺼낸 형제 핸들이 죽으므로, 꺼낸 자리는 다 채운 뒤 버린다(스택).
                void beginMapEntry( const void* pKey, const hashed_string& keyTypeName ) override
                {
                    StringBuilder<constant::kMaxBuffer8192> keyText;
                    SerializerUtil::valueToText( keyText, pKey, keyTypeName, _ctx );
                    const JsonValue entry = _listSlot.back().set( keyText.view(), false );
                    _listSlot.push_back( entry );
                }

                void endMapEntry() override { _listSlot.pop_back(); }

                void beginNestedContainer( const ContainerSlot slot ) override
                {
                    if ( slot == ContainerSlot::SequenceElement )
                    {
                        const JsonValue element = _listSlot.back().pushBack();
                        _listSlot.push_back( element );
                    }
                }

                void endNestedContainer( const ContainerSlot slot ) override
                {
                    if ( slot == ContainerSlot::SequenceElement )
                        _listSlot.pop_back();
                }

                void writeOwnedPointer( const void* pObject ) override
                {
                    if ( pObject == nullptr )
                        return; // 널 원소는 배열에 넣지 않는다
                    const JsonValue dst = _listSlot.back().pushBack();
                    // 맡아 둔 원소(모르는 타입)는 읽은 원문 그대로 다시 쓴다.
                    SerializeContext::OpaqueElementView opaque{};
                    if ( _ctx.queryOpaqueElement( pObject, opaque ) && opaque._format == SerializeContext::OpaqueFormat::Json )
                    {
                        JsonDocument rawDoc;
                        if ( rawDoc.tryParse( opaque._text ) )
                        {
                            dst.assignFrom( rawDoc.getRoot() );
                            return;
                        }
                    }
                    dst.setObject();
                    const TypeInfo* pRuntimeType = _ctx.getRuntimeTypeInfo( pObject );
                    if ( pRuntimeType == nullptr )
                        return;
                    const JsonValue body = dst.set( pRuntimeType->_name.c_str(), false );
                    JsonSerializer::writeObject( body, pObject, *pRuntimeType, _ctx );
                    body.set( kSchemaVersionKey, false ).setUint( 0 );
                }

                // 값 구조체는 타입 래핑 없이 본문을 그대로 쓴다(리더도 본문만 받는다).
                void writeValueObject( const void* pValue, const hashed_string&, const TypeInfo& typeInfo, const ContainerSlot slot ) override
                {
                    JsonSerializer::writeObject( openElementSlot( slot ), pValue, typeInfo, _ctx );
                }

                void writeScalar( const void* pValue, const hashed_string& typeName, const ContainerSlot slot ) override
                {
                    writeJsonValue( openElementSlot( slot ), pValue, typeName, _ctx );
                }

            private:
                /** @brief 원소 하나를 적을 자리입니다 — 시퀀스면 새 배열 원소, 맵 값이면 항목이 연 자리입니다. */
                JsonValue openElementSlot( const ContainerSlot slot ) const
                {
                    if ( slot == ContainerSlot::SequenceElement )
                        return _listSlot.back().pushBack();
                    return _listSlot.back();
                }

                vector<JsonValue, InlineAllocator<JsonValue, 4>> _listSlot;
                const SerializeContext&                          _ctx;
            };

            /** @brief 컨테이너를 자연스러운 JSON 에서 읽습니다. 모양이 다른 값(배열 자리에 글)은 그 칸만 실패입니다 — 다음 자리는 압니다. */
            class ContainerReader final : public IContainerReader
            {
            public:
                ContainerReader( const JsonValue& root, const SerializeContext& ctx, vector<SchemaOrphanValue>* pOutListOrphan )
                    : _listCursor{}
                    , _pCurrentKey{ nullptr }
                    , _ctx{ ctx }
                    , _pOutListOrphan{ pOutListOrphan }
                {
                    _listCursor.push_back( root );
                }

                ContainerReadResult beginSequence( size_t& outCountHint ) override
                {
                    const JsonValue& current = _listCursor.back();
                    if ( current.isArray() == false )
                        return ContainerReadResult::FieldFailed;
                    outCountHint = current.size();
                    return ContainerReadResult::Read;
                }

                ContainerReadResult beginMap( size_t& outCountHint ) override
                {
                    outCountHint = 0;
                    return _listCursor.back().isObject() ? ContainerReadResult::Read : ContainerReadResult::FieldFailed;
                }

                ContainerReadResult forEachElement( const ContainerElementVisitDelegate& visit ) override
                {
                    const JsonValue     container = _listCursor.back();
                    ContainerReadResult total{ ContainerReadResult::Read };
                    if ( container.isArray() )
                    {
                        for ( size_t elementIndex = 0; elementIndex < container.size() && total != ContainerReadResult::StreamBroken; ++elementIndex )
                        {
                            _listCursor.push_back( container.at( elementIndex ) );
                            total = ContainerVisitor::mergeResult( total, visit( elementIndex ) );
                            _listCursor.pop_back();
                        }
                        return total;
                    }
                    const vector<string> listKey = container.getMemberNames();
                    for ( size_t entryIndex = 0; entryIndex < listKey.size() && total != ContainerReadResult::StreamBroken; ++entryIndex )
                    {
                        _pCurrentKey = &listKey[entryIndex];
                        _listCursor.push_back( container.get( listKey[entryIndex], false ) );
                        total = ContainerVisitor::mergeResult( total, visit( entryIndex ) );
                        _listCursor.pop_back();
                    }
                    _pCurrentKey = nullptr;
                    return total;
                }

                // 키는 멤버 이름(글)이다 — XML 의 `key` 속성과 같은 길로 읽는다.
                ContainerReadResult readMapKey( void* pKey, const hashed_string& keyTypeName ) override
                {
                    const bool bRead = _pCurrentKey != nullptr && parseTextValueCoerced( pKey, keyTypeName, *_pCurrentKey, _ctx );
                    return bRead ? ContainerReadResult::Read : ContainerReadResult::FieldFailed;
                }

                ContainerReadResult readOwnedPointer() override
                {
                    // 타입 이름 키 하나짜리 오브젝트가 아니면 원소로 보지 않고 건너뛴다.
                    const JsonValue& element = _listCursor.back();
                    if ( element.isObject() == false )
                        return ContainerReadResult::Read;
                    const vector<string> listKey = element.getMemberNames();
                    if ( listKey.size() != 1 )
                        return ContainerReadResult::Read;
                    const hashed_string typeName = hashed_string::findInterned( listKey[0] );
                    const TypeInfo*     pType    = typeName.empty() ? nullptr : engine::getTypeRegistry().findType( typeName );
                    void*               pObject  = ( pType != nullptr ) ? _ctx.createOwnedPointer( typeName ) : nullptr;
                    if ( pObject == nullptr )
                    {
                        // 모르는(만들 수 없는) 타입이다 — 원문을 맡긴다(다음 저장이 그대로 다시 쓴다). 맡을 곳이 없으면 건너뛴다.
                        const string                        rawJson = element.dump();
                        SerializeContext::OpaqueElementView opaque{};
                        opaque._typeName = listKey[0];
                        opaque._format   = SerializeContext::OpaqueFormat::Json;
                        opaque._text     = rawJson;
                        (void)_ctx.keepOpaqueElement( opaque ); // 맡지 못하면 건너뛴다
                        return ContainerReadResult::Read;
                    }
                    // 원소 안의 못 읽은 칸은 바깥 orphan 목록으로(XML 과 같다) — 엄격하게 읽으면 칸 하나가 `_listComponent` 칸 전체를 실패로 만든다.
                    const bool bRead = JsonSerializer::readObject( element.get( listKey[0], false ), pObject, *pType, _pOutListOrphan, nullptr, _ctx );
                    return bRead ? ContainerReadResult::Read : ContainerReadResult::FieldFailed;
                }

                // 값 구조체 원소는 본문 그대로다(쓰는 쪽이 타입 이름으로 감싸지 않는다). 감싼 원소는 모르는 키 하나로 읽힌다.
                ContainerReadResult readValueObject( void* pValue, const hashed_string&, const TypeInfo& typeInfo, ContainerSlot ) override
                {
                    const JsonValue& element = _listCursor.back();
                    const bool       bRead   = element.isObject() && JsonSerializer::readObject( element, pValue, typeInfo, nullptr, nullptr, _ctx );
                    return bRead ? ContainerReadResult::Read : ContainerReadResult::FieldFailed;
                }

                ContainerReadResult readScalar( void* pValue, const hashed_string& typeName, ContainerSlot ) override
                {
                    return readJsonValue( pValue, typeName, _listCursor.back(), _ctx ) ? ContainerReadResult::Read : ContainerReadResult::FieldFailed;
                }

                ContainerReadResult skipElement( size_t ) override { return ContainerReadResult::FieldFailed; }

            private:
                vector<JsonValue, InlineAllocator<JsonValue, 4>> _listCursor;
                const string*                                    _pCurrentKey;
                const SerializeContext&                          _ctx;
                vector<SchemaOrphanValue>*                       _pOutListOrphan;
            };

            [[nodiscard]] static bool readJsonValue( void* pValPtr, const hashed_string& typeName, const JsonValue& src, const SerializeContext& ctx )
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
                    // 모르는 이름 · 값이면 쓰지 않고 실패한다(XML 과 같다 — `EnumInfo::tryParseText`).
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
                    // 프로퍼티 이름 아래에 바로 배열/오브젝트로 쓴다.
                    ContainerWriter writer( parent.set( prop._name.c_str(), false ), ctx );
                    ContainerVisitor::write( pPropPtr, prop.getContainerShape(), writer, ctx );
                    return;
                }
                writeJsonValue( parent.set( prop._name.c_str(), false ), pPropPtr, prop._typeName, ctx );
            }

            [[nodiscard]] static bool readProperty( const JsonValue& field, const PropertyInfo& prop, void* pInstance, const SerializeContext& ctx,
                                                    vector<SchemaOrphanValue>* pOutListOrphan = nullptr )
            {
                if ( prop._bIsBitField == SW_TRUE )
                {
                    // 불리언 · 숫자 · 불리언 글만 받는다. 그 밖의 것("ture" · 오브젝트 · null)은 실패다.
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
                {
                    ContainerReader reader( field, ctx, pOutListOrphan );
                    return ContainerVisitor::read( pPropPtr, prop.getContainerShape(), reader, ctx ) == ContainerReadResult::Read;
                }
                return readJsonValue( pPropPtr, prop._typeName, field, ctx );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "JsonSerializer" );

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
            bool bCaseVariant{ false };
            // 대소문자를 가리는 문맥에서 대소문자만 다른 키: orphan 목록이 없으면 건너뛴다(그 문맥의 계약 — 묶지 않을 뿐 실패는 아니다).
            // 목록이 있으면(설정 읽기 `ConfigManager::readConfigJson`) 아래에서 모르는 키로 이름을 알린다 — 조용히 버리면 `_Width` 오타가 사라진다.
            const PropertyInfo* pMatched = SerializerUtil::matchProperty( listProp, keyRaw, bIgnoreCaseKeys, bCaseVariant );
            if ( pMatched == nullptr && bCaseVariant && pOutListOrphan == nullptr )
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
            if ( JsonSerializerInternal::readProperty( field, *pMatched, pInstance, ctx, pOutListOrphan ) == false )
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
