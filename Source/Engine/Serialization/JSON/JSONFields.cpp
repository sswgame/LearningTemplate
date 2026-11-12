#include "pch.h"

#include "Engine/Serialization/JSON/JSONFields.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/formatString.h"

#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/Serializer/ContainerVisitor.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw
{
    bool JSONFields::readProperties( const JSONValue& object, void* pInstance, const TypeInfo& typeInfo, string_view sourceName, std::initializer_list<string_view> listHandRead )
    {
        if ( object.isObject() == false )
        {
            SW_LOG_ERROR_ID( "JSONFields.ExpectedObject", "%#: must be a JSON object (%#)", sourceName, typeInfo._name.c_str() );
            return false;
        }
        const SerializeContext& context      = SerializeContext::getDefault( SerializePurpose::Authoring );
        const auto&             listProperty = typeInfo.getPropertiesWithBase();
        // 칸마다의 키 이름은 한 번만 만든다.
        vector<string> listKeyName;
        listKeyName.reserve( listProperty.size() );
        for ( const PropertyInfo& prop : listProperty )
        {
            listKeyName.push_back( XMLFields::getAttributeName( prop ) );
        }

        // 1) 적힌 키마다 칸을 찾아 읽는다. 모르는 이름은 오타다 — 조용히 넘기면 그 칸은 기본값으로 남는다.
        bool bOk = true;
        for ( const string& key : object.getMemberNames() )
        {
            if ( VectorUtil::contains( listHandRead, string_view( key ) ) )
                continue;
            size_t propIndex = 0;
            while ( propIndex < listKeyName.size() && listKeyName[propIndex] != key )
            {
                ++propIndex;
            }
            if ( propIndex >= listProperty.size() )
            {
                SW_LOG_ERROR_ID( "JSONFields.UnknownField", "%#: '%#' is not a field of %#", sourceName, key, typeInfo._name.c_str() );
                bOk = false;
                continue;
            }
            const PropertyInfo& prop  = listProperty[propIndex];
            const JSONValue     field = object.get( key );
            // 구조체 칸은 같은 규칙으로 들어간다(`readObject` 로 넘기면 적지 않은 칸이 되돌아가고 키 이름 규칙도 다르다).
            const TypeInfo* const pNested = prop._bIsContainer == SW_TRUE ? nullptr : SerializerUtil::findNestedObjectType( prop._typeName, context );
            if ( pNested != nullptr )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#.%#", sourceName, key );
                bOk = readProperties( field, prop.getRawPtr( pInstance ), *pNested, textBuilder.view() ) && bOk;
                continue;
            }
            // 고정 배열의 길이가 다르면 남는 칸이 조용히 옛 값으로 남는다. 저작 파일에서는 오류다(일반 읽기는 판이 달라진 데이터를 위해 받는다).
            const ISequenceContainerWrapper* const pSequence    = prop._bIsContainer == SW_TRUE && prop._containerWrapper != nullptr ? prop._containerWrapper->asSequence() : nullptr;
            const bool                             bWrongLength = pSequence != nullptr && pSequence->isFixedSize() && field.size() != pSequence->getSize( prop.getRawPtr( pInstance ) );
            if ( bWrongLength || JSONSerializer::readField( field, prop, pInstance, context ) != FieldReadResult::Read )
            {
                SW_LOG_ERROR_ID( "JSONFields.ValueUnreadable", "%#: '%#' = %# cannot be read as %#", sourceName, key, field.describe(), prop._typeName.c_str() );
                bOk = false;
                continue;
            }
            // 범위는 자르지 않고 알린다. 적지 않은 칸은 코드의 초기값이라 파일의 실수가 아니어서 적힌 칸만 본다.
            vector<string> listRangeError;
            SerializerUtil::collectPropertyRangeErrors( prop, ObjectAddress{ pInstance }, string(), listRangeError );
            for ( const string& rangeError : listRangeError )
            {
                SW_LOG_ERROR_ID( "JSONFields.ValueOutOfRange", "%#: %#", sourceName, rangeError.c_str() );
                bOk = false;
            }
        }

        // 2) 필수 칸이 빠졌는지 본다. 초기값은 형식일 뿐이라 대신 쓰지 않는다.
        const RequiredScope scope = SerializerUtil::findRequiredScope( listProperty, [&object, &listKeyName]( const size_t propIndex )
        {
            const JSONValue field = object.get( listKeyName[propIndex].c_str() );
            return field.isValid() && field.isString() ? field.asString() : string{};
        } );
        for ( size_t propIndex = 0; propIndex < listProperty.size(); ++propIndex )
        {
            // 판정은 XML · JSON 직렬화기와 한 함수다 — 빠졌거나, 이름 · 경로 형식인데 빈 글이다.
            const JSONValue   field     = object.get( listKeyName[propIndex].c_str() );
            const string      fieldText = field.isValid() && field.isString() ? field.asString() : string{};
            const string_view textView( fieldText );
            if ( SerializerUtil::isRequiredValueMissing( listProperty[propIndex], field.isValid(), field.isValid() && field.isString() ? &textView : nullptr, scope ) == false )
                continue;
            SW_LOG_ERROR_ID( "JSONFields.RequiredFieldMissing", "%#: needs '%#' (%#)", sourceName, listKeyName[propIndex], typeInfo._name.c_str() );
            bOk = false;
        }
        return bOk;
    }

    bool JSONFields::hasOnlyKnownKeys( const JSONValue& object, std::initializer_list<string_view> listKnownKey, string_view sourceName )
    {
        if ( object.isObject() == false )
        {
            SW_LOG_ERROR_ID( "JSONFields.ExpectedObject", "%#: must be a JSON object", sourceName );
            return false;
        }
        bool bKnown = true;
        for ( const string& key : object.getMemberNames() )
        {
            if ( VectorUtil::contains( listKnownKey, string_view( key ) ) )
                continue;
            SW_LOG_ERROR_ID( "JSONFields.UnknownKey", "%#: unknown key '%#'", sourceName, key );
            bKnown = false;
        }
        return bKnown;
    }
} // namespace sw
