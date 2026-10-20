#include "pch.h"

#include "Engine/Serialization/Format/JSONSerializer.h"

#include "Core/Container/InlineAllocator.h"
#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Base/ContainerVisitor.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct JSONSerializerInternal
        {
            // 주소는 값 자체다(프로퍼티 오프셋을 더한 뒤). 객체 주소는 `writeProperty` 가 받는다.
            static void writeJSONValue( JSONValue dst, const ValueAddress value, const hashed_string& typeName, const SerializeContext& context )
            {
                if ( dst.isValid() == false )
                    return;

                const hashed_string resolved  = SerializerUtil::resolveHandlerTypeName( typeName, context );
                const bool          bIsString = ( resolved.isPredefinedType( PredefinedNameType::NameType_string ) ||
                                         resolved.isPredefinedType( PredefinedNameType::NameType_hashed_string ) ||
                                         resolved.isPredefinedType( PredefinedNameType::NameType_TagID ) );
                const bool          bIsBool   = ( resolved.isPredefinedType( PredefinedNameType::NameType_bool ) ||
                                       resolved.isPredefinedType( PredefinedNameType::NameType_atomic_bool ) );
                const bool          bIsEnum   = ( engine::getSubsystem<TypeRegistry>().findEnum( typeName ) != nullptr );

                if ( bIsString || bIsEnum )
                {
                    StringBuilder<constant::kMaxBuffer8192> text;
                    SerializerUtil::valueToText( text, value, typeName, context );
                    dst.setString( text.view() );
                    return;
                }

                if ( bIsBool )
                {
                    StringBuilder<constant::kMaxBuffer8192> text;
                    SerializerUtil::valueToText( text, value, typeName, context );
                    dst.setBool( StringUtil::parseBool( text.view(), false ) );
                    return;
                }

                if ( context.findTextWriter( resolved ) != nullptr )
                {
                    StringBuilder<constant::kMaxBuffer8192> text;
                    SerializerUtil::valueToText( text, value, typeName, context );
                    JSONDocument parsed;
                    const bool   bParsedScalar = parsed.tryParse( text.view() ) && parsed.getRoot().isObject() == false && parsed.getRoot().isArray() == false;
                    if ( bParsedScalar )
                    {
                        dst.assignFrom( parsed.getRoot() );
                        return;
                    }
                    dst.setString( text.view() );
                    return;
                }

                const TypeInfo* pStructInfo = engine::getSubsystem<TypeRegistry>().findType( typeName );
                if ( pStructInfo != nullptr )
                {
                    if ( pStructInfo->isPrimitive() == false )
                    {
                        JSONSerializer::writeObject( dst, value.get(), *pStructInfo, context );
                        return;
                    }
                }

                StringBuilder<constant::kMaxBuffer8192> text;
                SerializerUtil::valueToText( text, value, typeName, context );
                dst.setString( text.view() );
            }

            /** @brief 컨테이너를 자연스러운 JSON 으로 적습니다 — 시퀀스는 배열, 맵은 오브젝트, 소유 포인터 원소만 `{ "TypeName": {...} }` 로 감쌉니다. */
            class ContainerWriter final : public IContainerWriter
            {
            public:
                ContainerWriter( const JSONValue& root, const SerializeContext& context )
                    : _listSlot{}
                    , _context{ context }
                {
                    _listSlot.push_back( root );
                }

                void beginSequence( size_t ) override { _listSlot.back().setArray(); }
                void endSequence() override {}
                void beginMap( size_t ) override { _listSlot.back().setObject(); }
                void endMap() override {}

                // `JSONValue` 는 빌린 포인터다 — 같은 부모에 새 키 · 원소를 더하면 앞서 꺼낸 형제 핸들이 죽으므로, 꺼낸 자리는 다 채운 뒤 버린다(스택).
                void beginMapEntry( const ValueAddress key, const hashed_string& keyTypeName ) override
                {
                    StringBuilder<constant::kMaxBuffer8192> keyText;
                    SerializerUtil::valueToText( keyText, key, keyTypeName, _context );
                    const JSONValue entry = _listSlot.back().set( keyText.view(), false );
                    _listSlot.push_back( entry );
                }

                void endMapEntry() override { _listSlot.pop_back(); }

                void beginNestedContainer( const ContainerSlot slot ) override
                {
                    if ( slot == ContainerSlot::SequenceElement )
                    {
                        const JSONValue element = _listSlot.back().pushBack();
                        _listSlot.push_back( element );
                    }
                }

                void endNestedContainer( const ContainerSlot slot ) override
                {
                    if ( slot == ContainerSlot::SequenceElement )
                        _listSlot.pop_back();
                }

                void writeOwnedPointer( const ObjectAddress object ) override
                {
                    const void* const pObject = object.get();
                    if ( pObject == nullptr )
                        return; // 널 원소는 배열에 넣지 않는다
                    const JSONValue dst = _listSlot.back().pushBack();
                    // 맡아 둔 원소(모르는 타입)는 읽은 원문 그대로 다시 쓴다.
                    SerializeContext::OpaqueElementView opaque{};
                    if ( _context.queryOpaqueElement( pObject, opaque ) && opaque._format == SerializeContext::OpaqueFormat::JSON )
                    {
                        JSONDocument rawDoc;
                        if ( rawDoc.tryParse( opaque._text ) )
                        {
                            dst.assignFrom( rawDoc.getRoot() );
                            return;
                        }
                    }
                    dst.setObject();
                    const TypeInfo* pRuntimeType = _context.getRuntimeTypeInfo( pObject );
                    if ( pRuntimeType == nullptr )
                        return;
                    const JSONValue body = dst.set( pRuntimeType->_name.c_str(), false );
                    JSONSerializer::writeObject( body, pObject, *pRuntimeType, _context );
                    body.set( kSchemaVersionKey, false ).setUint( 0 );
                }

                // 값 구조체는 타입 래핑 없이 본문을 그대로 쓴다(리더도 본문만 받는다).
                void writeValueObject( const ValueAddress value, const hashed_string&, const TypeInfo& typeInfo, const ContainerSlot slot ) override
                {
                    JSONSerializer::writeObject( openElementSlot( slot ), value.get(), typeInfo, _context );
                }

                void writeScalar( const ValueAddress value, const hashed_string& typeName, const ContainerSlot slot ) override
                {
                    writeJSONValue( openElementSlot( slot ), value, typeName, _context );
                }

            private:
                /** @brief 원소 하나를 적을 자리입니다 — 시퀀스면 새 배열 원소, 맵 값이면 항목이 연 자리입니다. */
                JSONValue openElementSlot( const ContainerSlot slot ) const
                {
                    if ( slot == ContainerSlot::SequenceElement )
                        return _listSlot.back().pushBack();
                    return _listSlot.back();
                }

                vector<JSONValue, InlineAllocator<JSONValue, 4>> _listSlot;
                const SerializeContext&                          _context;
            };

            /** @brief 컨테이너를 자연스러운 JSON 에서 읽습니다. 모양이 다른 값(배열 자리에 글)은 그 칸만 실패입니다 — 다음 자리는 압니다. */
            class ContainerReader final : public IContainerReader
            {
            public:
                ContainerReader( const JSONValue& root, const SerializeContext& context, vector<SchemaOrphanValue>* pOutListOrphan,
                                 const PropertyInfo& propForOrphan )
                    : _listCursor{}
                    , _pCurrentKey{ nullptr }
                    , _context{ context }
                    , _pOutListOrphan{ pOutListOrphan }
                    , _propForOrphan{ propForOrphan }
                {
                    _listCursor.push_back( root );
                }

                ContainerReadResult beginSequence( size_t& outCountHint ) override
                {
                    const JSONValue& current = _listCursor.back();
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
                    const JSONValue     container = _listCursor.back();
                    ContainerReadResult total{ ContainerReadResult::Read };
                    if ( container.isArray() )
                    {
                        for ( size_t elementIndex = 0; elementIndex < container.size() && ContainerVisitor::stopsReading( total ) == false; ++elementIndex )
                        {
                            _listCursor.push_back( container.at( elementIndex ) );
                            total = ContainerVisitor::mergeResult( total, visit( elementIndex ) );
                            _listCursor.pop_back();
                        }
                        return total;
                    }
                    const vector<string> listKey = container.getMemberNames();
                    for ( size_t entryIndex = 0; entryIndex < listKey.size() && ContainerVisitor::stopsReading( total ) == false; ++entryIndex )
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
                ContainerReadResult readMapKey( const MutableValueAddress key, const hashed_string& keyTypeName ) override
                {
                    const bool bRead = _pCurrentKey != nullptr && parseTextValueCoerced( key, keyTypeName, *_pCurrentKey, _context );
                    return bRead ? ContainerReadResult::Read : ContainerReadResult::FieldFailed;
                }

                ContainerReadResult readOwnedPointer() override
                {
                    // `{ "타입이름": {...} }` 꼴이 아니면 원소로 읽을 수 없다 - 그 원소만 orphan 으로 남긴다(XML 의 못 읽은 원소와 같다).
                    const JSONValue& element = _listCursor.back();
                    if ( element.isObject() == false )
                        return rejectElement( element );
                    const vector<string> listKey = element.getMemberNames();
                    if ( listKey.size() != 1 )
                        return rejectElement( element );
                    const hashed_string typeName = hashed_string::findInterned( listKey[0] );
                    const TypeInfo*     pType    = typeName.empty() ? nullptr : engine::getSubsystem<TypeRegistry>().findType( typeName );
                    void*               pObject  = ( pType != nullptr ) ? _context.createOwnedPointer( typeName ) : nullptr;
                    if ( pObject == nullptr )
                    {
                        // 모르는(만들 수 없는) 타입이다 — 원문을 맡긴다(다음 저장이 그대로 다시 쓴다). 맡을 곳이 없으면 건너뛴다.
                        const string                        rawJSON = element.dump();
                        SerializeContext::OpaqueElementView opaque{};
                        opaque._typeName = listKey[0];
                        opaque._format   = SerializeContext::OpaqueFormat::JSON;
                        opaque._text     = rawJSON;
                        (void)_context.keepOpaqueElement( opaque ); // 맡지 못하면 건너뛴다
                        return ContainerReadResult::Read;
                    }
                    // 원소 안의 못 읽은 칸은 바깥 orphan 목록으로(XML 과 같다) — 엄격하게 읽으면 칸 하나가 `_listComponent` 칸 전체를 실패로 만든다.
                    // 금지된 필드 · 되돌리지 못한 필드는 ObjectRejected 로 올라간다(칸 실패로 낮추지 않는다).
                    return ContainerVisitor::toElementResult( JSONSerializer::readObject( element.get( listKey[0], false ), pObject, *pType, _pOutListOrphan, nullptr, _context ) );
                }

                // 값 구조체 원소는 본문 그대로다(쓰는 쪽이 타입 이름으로 감싸지 않는다). 감싼 원소는 모르는 키 하나로 읽힌다.
                ContainerReadResult readValueObject( const MutableValueAddress value, const hashed_string&, const TypeInfo& typeInfo, ContainerSlot ) override
                {
                    const JSONValue& element = _listCursor.back();
                    if ( element.isObject() == false )
                        return ContainerReadResult::FieldFailed;
                    return ContainerVisitor::toElementResult( JSONSerializer::readObject( element, value.get(), typeInfo, nullptr, nullptr, _context ) );
                }

                ContainerReadResult readScalar( const MutableValueAddress value, const hashed_string& typeName, ContainerSlot ) override
                {
                    return ContainerVisitor::toElementResult( readJSONValue( value, typeName, _listCursor.back(), _context ) );
                }

                ContainerReadResult skipElement( size_t ) override { return ContainerReadResult::FieldFailed; }

            private:
                /**
                 * @brief 모양이 틀린 소유 포인터 원소를 알리고 orphan 으로 남깁니다.
                 * @details orphan 목록이 있으면 원소 하나만 거기 남고 Read 다 - 돌려주는 실패가 `_listComponent` 칸 전체를 orphan 으로 만들지 않게 한다.
                 *          엄격 읽기(목록 없음)면 FieldFailed 다.
                 */
                ContainerReadResult rejectElement( const JSONValue& element ) const
                {
                    SW_LOG_WARNING( "'%#' has an owned-pointer element that is not a single-key type object - element dropped", _propForOrphan._name.c_str() );
                    if ( _pOutListOrphan == nullptr )
                        return ContainerReadResult::FieldFailed;
                    SchemaOrphanValue orphan;
                    orphan._name         = _propForOrphan._name;
                    orphan._nameHash     = _propForOrphan.getNameHash();
                    orphan._wireTypeHash = 0;
                    orphan._text         = element.dump();
                    _pOutListOrphan->push_back( std::move( orphan ) );
                    return ContainerReadResult::Read;
                }

                vector<JSONValue, InlineAllocator<JSONValue, 4>> _listCursor;
                const string*                                    _pCurrentKey;
                const SerializeContext&                          _context;
                vector<SchemaOrphanValue>*                       _pOutListOrphan;
                const PropertyInfo&                              _propForOrphan;
            };

            /** @brief 값 하나를 읽었는지(`bool`)를 필드 결과로 바꿉니다. 값 하나의 실패는 그 필드의 실패입니다 — 객체째 실패는 구조체 읽기만 압니다. */
            static FieldReadResult fieldResultOf( const bool bRead )
            {
                return bRead ? FieldReadResult::Read : FieldReadResult::FieldFailed;
            }

            /** @brief @p src 를 @p typeName 값 하나로 읽습니다. 구조체이면 그 객체의 결과(금지된 필드면 ObjectRejected)를 그대로 돌려줍니다. */
            [[nodiscard]] static FieldReadResult readJSONValue( const MutableValueAddress value, const hashed_string& typeName, const JSONValue& src, const SerializeContext& context )
            {
                void* const pValPtr = value.get();
                if ( pValPtr == nullptr || src.isValid() == false )
                    return FieldReadResult::FieldFailed;

                const hashed_string                 resolved    = SerializerUtil::resolveHandlerTypeName( typeName, context );
                const SerializeContext::TextReadFn* pTextReader = context.findTextReader( resolved );
                if ( pTextReader != nullptr )
                {
                    if ( src.isString() )
                        return fieldResultOf( ( *pTextReader )( pValPtr, src.asString() ) );
                    return fieldResultOf( ( *pTextReader )( pValPtr, src.dump() ) );
                }

                const EnumInfo* pEnumInfo = engine::getSubsystem<TypeRegistry>().findEnum( typeName );
                if ( pEnumInfo != nullptr )
                {
                    // 모르는 이름 · 값이면 쓰지 않고 실패한다(XML 과 같다 — `EnumInfo::tryParseText`).
                    int64                      parsedValue{ 0 };
                    const std::optional<int64> number = src.isNumber() ? src.read<int64>() : std::nullopt;
                    const string               text   = src.isString() ? string( src.asString() ) : number.has_value() ? sw::to_string( *number )
                                                                                                                       : src.dump();
                    if ( ( src.isString() || number.has_value() ) && pEnumInfo->tryParseText( text, parsedValue ) )
                    {
                        pEnumInfo->writeValueToMemory( pValPtr, parsedValue );
                        return FieldReadResult::Read;
                    }
                    SW_LOG_WARNING( "'%#' is not a value of enum %# - the field keeps its current value", text, pEnumInfo->_fullyQualifiedName.c_str() );
                    return FieldReadResult::FieldFailed;
                }

                const TypeInfo* pStructInfo = engine::getSubsystem<TypeRegistry>().findType( typeName );
                if ( pStructInfo != nullptr )
                {
                    if ( pStructInfo->isPrimitive() == false )
                    {
                        if ( src.isObject() )
                            return JSONSerializer::readObject( src, pValPtr, *pStructInfo, nullptr, nullptr, context );
                        return fieldResultOf( JSONSerializer::deserialize( pValPtr, *pStructInfo, src.dump(), context ) );
                    }
                }

                if ( src.isString() )
                    return fieldResultOf( parseTextValueCoerced( value, typeName, src.asString(), context ) );
                return fieldResultOf( parseTextValueCoerced( value, typeName, src.dump(), context ) );
            }
            static void writeProperty( JSONValue parent, const PropertyInfo& prop, const ObjectAddress instance, const SerializeContext& context )
            {
                if ( prop._bIsBitField == SW_TRUE )
                {
                    const bool bVal = prop.getValue<bool>( instance.get() );
                    parent.set( prop._name.c_str(), false ).setBool( bVal );
                    return;
                }
                const ValueAddress propValue{ prop.getRawPtr( instance.get() ) };
                if ( prop._bIsContainer && prop.hasContainerWrapper() )
                {
                    // 프로퍼티 이름 아래에 바로 배열/오브젝트로 쓴다.
                    ContainerWriter writer( parent.set( prop._name.c_str(), false ), context );
                    ContainerVisitor::write( propValue, prop.getContainerShape(), writer, context );
                    return;
                }
                writeJSONValue( parent.set( prop._name.c_str(), false ), propValue, prop._typeName, context );
            }

            [[nodiscard]] static FieldReadResult readProperty( const JSONValue& field, const PropertyInfo& prop, const MutableObjectAddress instance, const SerializeContext& context,
                                                               vector<SchemaOrphanValue>* pOutListOrphan = nullptr )
            {
                if ( prop._bIsBitField == SW_TRUE )
                {
                    // 불리언 · 정수 · 불리언 글만 받는다. 그 밖의 것("ture" · 0.5 · 오브젝트 · null)은 실패다.
                    bool bVal = false;
                    if ( field.isBool() || field.isNumber() )
                    {
                        const std::optional<bool>  flag   = field.isBool() ? field.read<bool>() : std::nullopt;
                        const std::optional<int64> number = field.isNumber() ? field.read<int64>() : std::nullopt;
                        if ( flag.has_value() == false && number.has_value() == false )
                            return false;
                        bVal = flag.has_value() ? *flag : *number != 0;
                    }
                    else if ( field.isString() == false || StringUtil::tryParseBool( field.asString(), bVal ) == false )
                        return FieldReadResult::FieldFailed;
                    prop.setValue<bool>( instance.get(), bVal );
                    return FieldReadResult::Read;
                }
                const MutableValueAddress propValue{ prop.getRawPtr( instance.get() ) };
                if ( prop._bIsContainer && prop.hasContainerWrapper() )
                {
                    ContainerReader reader( field, context, pOutListOrphan, prop );
                    return ContainerVisitor::toFieldResult( ContainerVisitor::read( propValue, prop.getContainerShape(), reader, context ) );
                }
                return readJSONValue( propValue, prop._typeName, field, context );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "JSONSerializer" );

    string JSONSerializer::serialize( const void* pInstance, const TypeInfo& typeInfo, const SerializeContext& context )
    {
        JSONDocument doc;
        writeObject( doc.makeObject(), pInstance, typeInfo, context );
        return doc.dump();
    }

    string JSONSerializer::serializePretty( const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces,
                                            const SerializeContext& context )
    {
        JSONDocument doc;
        writeObject( doc.makeObject(), pInstance, typeInfo, context );
        return doc.dump( static_cast<int32>( indentSpaces == 0 ? 4 : indentSpaces ) );
    }

    bool JSONSerializer::deserialize( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                      const SerializeContext& context )
    {
        return deserializeSoft( pInstance, typeInfo, jsonStr, nullptr, nullptr, context ) == SoftReadResult::Read;
    }

    bool JSONSerializer::saveFile( string_view absPath, const void* pInstance, const TypeInfo& typeInfo, uint32 indentSpaces,
                                   const SerializeContext& context )
    {
        JSONDocument doc;
        writeObject( doc.makeObject(), pInstance, typeInfo, context );
        const int32 indent = static_cast<int32>( indentSpaces == 0 ? 4 : indentSpaces );
        return doc.saveFile( absPath, indent );
    }

    bool JSONSerializer::loadFile( string_view path, void* pInstance, const TypeInfo& typeInfo, const SerializeContext& context )
    {
        JSONDocument doc;
        if ( doc.loadResource( path ) == false )
            return false;
        return readObject( doc.getRoot(), pInstance, typeInfo, nullptr, nullptr, context ) == FieldReadResult::Read;
    }

    void JSONSerializer::writeObject( JSONValue dst, const void* pInstance, const TypeInfo& typeInfo,
                                      const SerializeContext& context )
    {
        if ( dst.isValid() == false || pInstance == nullptr )
            return;
        dst.setObject();
        typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
        {
            if ( SerializerUtil::shouldSerializeProperty( typeInfo, prop, context ) == false )
                return;
            JSONSerializerInternal::writeProperty( dst, prop, ObjectAddress{ pInstance }, context );
        }, true /* 상속 PROPERTY 포함 */ );
    }

    FieldReadResult JSONSerializer::readObject( JSONValue src, void* pInstance, const TypeInfo& typeInfo,
                                                vector<SchemaOrphanValue>* pOutListOrphan, uint32* pOutVersion,
                                                const SerializeContext& context )
    {
        if ( pInstance == nullptr || src.isObject() == false )
            return FieldReadResult::FieldFailed;

        const bool                  bIgnoreCaseKeys = context.ignoresCaseKeys();
        const vector<PropertyInfo>& listProp        = typeInfo.getPropertiesWithBase();
        unordered_set<uint32>       uniqueMatched;
        bool                        bFieldError{ false };
        bool                        bForbiddenField{ false };
        // 소프트 읽기(orphan 목록이 있음)는 읽지 못한 필드를 orphan 으로 넘기고 이어 읽는다. 그 필드는 **읽기 전 값**이다(바이너리 · XML 과 같다).
        const bool           bContinuesAfterUnreadable = pOutListOrphan != nullptr;
        PropertyReadRollback rollback;

        if ( pOutVersion != nullptr )
            *pOutVersion = 0;

        for ( const string& keyRaw : src.getMemberNames() )
        {
            const JSONValue field = src.get( keyRaw, false );
            if ( SerializerUtil::keysEqual( keyRaw, kSchemaVersionKey, bIgnoreCaseKeys ) )
            {
                if ( pOutVersion == nullptr )
                    continue;
                // 판 번호가 부호 없는 정수가 아니면 이관을 고를 수 없다 — 0 판으로 읽지 않고 실패한다(읽기가 값을 알린다).
                const std::optional<uint32> version = field.read<uint32>();
                if ( version.has_value() == false )
                    return false;
                *pOutVersion = *version;
                continue;
            }
            bool bCaseVariant{ false };
            // 대소문자를 가리는 문맥에서 대소문자만 다른 키: orphan 목록이 없으면 건너뛴다(그 문맥의 계약 — 묶지 않을 뿐 실패는 아니다).
            // 목록이 있으면(설정 읽기 `ConfigManager::readConfigJSON`) 아래에서 모르는 키로 이름을 알린다 — 조용히 버리면 `_Width` 오타가 사라진다.
            const PropertyInfo* pMatched            = SerializerUtil::matchProperty( listProp, keyRaw, bIgnoreCaseKeys, bCaseVariant );
            const bool          bSkipCaseVariantKey = pMatched == nullptr && bCaseVariant && pOutListOrphan == nullptr;
            if ( bSkipCaseVariantKey )
                continue;

            if ( pMatched == nullptr || pMatched->_metadata->_bTransient == SW_TRUE )
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
            // 저작 파일의 런타임 상태는 로드 오류다. 이 목적이 다루지 않는 그 밖의 필드(옵트인 세이브의 나머지)는 지금 값 그대로 둔다.
            if ( SerializerUtil::isForbiddenInFile( *pMatched, context ) )
            {
                SerializerUtil::reportForbiddenProperty( typeInfo, *pMatched );
                bForbiddenField = true;
                continue;
            }
            if ( SerializerUtil::shouldSerializeProperty( typeInfo, *pMatched, context ) == false )
                continue;

            uniqueMatched.insert( pMatched->getNameHash() );
            if ( bContinuesAfterUnreadable && rollback.keepValueBeforeRead( typeInfo, *pMatched, ObjectAddress{ pInstance }, context ) == false )
                return FieldReadResult::ObjectRejected;
            const FieldReadResult fieldResult = JSONSerializerInternal::readProperty( field, *pMatched, MutableObjectAddress{ pInstance }, context, pOutListOrphan );
            switch ( fieldResult )
            {
                case FieldReadResult::Read:
                {
                    break;
                }
                case FieldReadResult::FieldFailed:
                {
                    if ( pOutListOrphan != nullptr )
                    {
                        // 읽다 만 값(원소를 채우다 만 컨테이너, 필드를 채우다 만 구조체)을 읽기 전 값으로 되돌린다. 되돌릴 수 없으면 객체째 실패다.
                        if ( rollback.restoreValueBeforeRead( MutableObjectAddress{ pInstance }, context ) == false )
                            return FieldReadResult::ObjectRejected;
                        SchemaOrphanValue orphan;
                        orphan._name     = pMatched->_name;
                        orphan._nameHash = pMatched->getNameHash();
                        orphan._text     = field.dump();
                        pOutListOrphan->push_back( std::move( orphan ) );
                    }
                    else
                    {
                        bFieldError = true;
                    }
                    break;
                }
                case FieldReadResult::ObjectRejected:
                {
                    // 안쪽(구조체 칸, 컨테이너 원소인 구조체 · 컴포넌트)의 금지된 필드 · 되돌리지 못한 필드다. 이 필드를 orphan 으로 넘기지 않고 객체째 실패한다.
                    return FieldReadResult::ObjectRejected;
                }
            }
        }

        for ( const PropertyInfo& prop : listProp )
        {
            const bool bKeepCurrent = uniqueMatched.find( prop.getNameHash() ) != uniqueMatched.end() ||
                                      SerializerUtil::shouldSerializeProperty( typeInfo, prop, context ) == false;
            if ( bKeepCurrent )
                continue;
            SerializerUtil::applyPropertyDefault( prop, MutableObjectAddress{ pInstance }, context );
        }

        // 금지된 필드는 소프트 읽기에서도 실패다(XML · 바이너리와 같다). 바깥 칸도 이것을 칸 실패로 낮추지 않는다.
        if ( bForbiddenField )
            return FieldReadResult::ObjectRejected;
        // 소프트 읽기(orphan 목록이 있음)는 읽지 못한 칸을 목록으로 넘기고 계속한다. 선언된 범위는 어느 쪽이든 읽기 공통 끝에서 본다(바이너리 · XML 과 같다).
        const bool bStrictFieldError = pOutListOrphan == nullptr && bFieldError;
        if ( bStrictFieldError )
            return FieldReadResult::FieldFailed;
        return SerializerUtil::isWithinDeclaredRanges( typeInfo, ObjectAddress{ pInstance } ) ? FieldReadResult::Read : FieldReadResult::FieldFailed;
    }

    SoftReadResult JSONSerializer::deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr,
                                                    vector<SchemaOrphanValue>* pOutListOrphan, uint32* pOutVersion,
                                                    const SerializeContext& context )
    {
        JSONDocument doc;
        const bool   bNotObject = pInstance == nullptr || doc.parse( jsonStr ) == false || doc.getRoot().isObject() == false;
        if ( bNotObject )
            return SoftReadResult::NotParsed;
        // 객체를 읽은 뒤의 실패는 칸 값이다(범위 밖 · orphan 목록 없이 읽지 못한 칸). `readObject` 는 그 전에 모든 키를 지난다.
        return readObject( doc.getRoot(), pInstance, typeInfo, pOutListOrphan, pOutVersion, context ) == FieldReadResult::Read ? SoftReadResult::Read : SoftReadResult::Rejected;
    }

    string JSONSerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
                                               const SerializeContext& context )
    {
        JSONDocument doc;
        JSONValue    root = doc.makeObject();
        root.set( kSchemaVersionKey, false ).setUint( version );
        typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
        {
            if ( SerializerUtil::shouldSerializeProperty( typeInfo, prop, context ) == false )
                return;
            JSONSerializerInternal::writeProperty( root, prop, ObjectAddress{ pInstance }, context );
        }, true /* 상속 PROPERTY 포함 */ );
        return doc.dump();
    }

    bool JSONSerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
                                               string_view jsonStr, uint32 currentVersion,
                                               const TypeInfo* pLegacyTypeInfo, const SerializeContext& context )
    {
        // 절차는 JSON · XML · Binary 가 공통이다(`runVersionedDeserialize`). 여기서 정하는 것은 두 가지뿐이다.
        // **버전이 어디서 오는가**(본문 안의 `_schemaVersion`)와 **orphan 만 있을 때의 정책**이다.
        return runVersionedDeserialize(
            outVersion, MutableObjectAddress{ pInstance }, typeInfo, currentVersion, pLegacyTypeInfo, context,
            SchemaVersionSource::Payload, SchemaOrphanPolicy::Ignore,
            SW_DELEGATE_LAMBDA( SoftDeserializeFn,
                                [&]( const MutableObjectAddress target, const TypeInfo& targetType, vector<SchemaOrphanValue>& listOrphan, uint32& outSoftVersion ) -> bool
        {
            return deserializeSoft( target.get(), targetType, jsonStr, &listOrphan, &outSoftVersion, context ) == SoftReadResult::Read;
        } ) );
    }

} // namespace sw
