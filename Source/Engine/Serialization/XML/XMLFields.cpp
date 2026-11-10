#include "pch.h"

#include "Engine/Serialization/XML/XMLFields.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct XMLFieldsInternal
        {
            /** @brief 한 줄 목록으로 적는 칸이면 그 시퀀스입니다 — 원소가 값 글 하나로 읽히는 시퀀스(이름 · 태그 · 수 · 열거). */
            [[nodiscard]] static const ISequenceContainerWrapper* findTextList( const PropertyInfo& prop, const SerializeContext& context )
            {
                const bool bSequence = prop._bIsContainer == SW_TRUE && prop._containerWrapper != nullptr && ( prop._nestedContainer == nullptr || prop._nestedContainer->_elementNested == nullptr );
                if ( bSequence == false || SerializerUtil::canCarryValueType( prop._elementTypeName, context ) == false )
                    return nullptr;
                return prop._containerWrapper->asSequence();
            }

            /** @brief 속성 하나에 이은 낱말을 칸의 목록으로 읽습니다(적은 값으로 바뀐다). 읽지 못한 낱말이 있으면 false 입니다. */
            [[nodiscard]] static bool readTextList( const PropertyInfo& prop, const ISequenceContainerWrapper& sequence, void* pInstance, string_view text, const SerializeContext& context )
            {
                void* const pContainer = prop.getRawPtr( pInstance );
                sequence.clear( pContainer );
                bool bValid = true;
                StringUtil::forEachToken( text, ",;| \t", [&]( string_view token )
                {
                    sequence.addElementDefault( pContainer );
                    void* const pElement = sequence.getElement( pContainer, sequence.getSize( pContainer ) - 1 );
                    bValid               = SerializerUtil::parseTextValue( MutableValueAddress{ pElement }, prop._elementTypeName, token, context ) && bValid;
                } );
                // 읽지 못한 낱말의 자리에는 기본값이 남아 있다 — 범위 검사가 그 값을 또 오류로 알리지 않게 비운다.
                if ( bValid == false )
                    sequence.clear( pContainer );
                return bValid;
            }

            static bool isHandRead( string_view attribute, std::initializer_list<string_view> listHandRead )
            {
                return VectorUtil::contains( listHandRead, attribute );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string XMLFields::getAttributeName( const PropertyInfo& prop )
    {
        string_view name = prop._name.view();
        // `PROPERTY( Name = "bSrgb" )` 처럼 직접 적은 이름은 글자 그대로다 — 이름 규칙은 C++ 칸 이름(`_` 로 시작)에만 건다.
        if ( name.empty() || name.front() != '_' )
            return string( name );
        name.remove_prefix( 1 );
        const bool bBooleanPrefix = name.size() > 1 && name[0] == 'b' && 'A' <= name[1] && name[1] <= 'Z';
        if ( bBooleanPrefix == false )
            return string( name );
        string attribute( name.substr( 1 ) );
        attribute[0] = static_cast<utf8>( attribute[0] - 'A' + 'a' );
        return attribute;
    }

    bool XMLFields::readFields( const XMLNode& node, void* pInstance, const TypeInfo& typeInfo, string_view sourceName, std::initializer_list<string_view> listHandRead,
                                AcceptAttributeFunction pfnHandRead, const XMLFieldsIssueSink& sink )
    {
        using Internal = XMLFieldsInternal;
        // 오류는 받는 곳이 있으면 그리로, 없으면 로그로 간다(id 는 글자 상수라 로그 쪽은 갈래마다 적는다).
        const auto report = [&sink]( const utf8* pID, const string& text )
        {
            if ( sink._pfnAdd != nullptr )
                sink._pfnAdd( sink._pContext, pID, text );
            else if ( StringUtil::equals( pID, "XMLFields.UnknownField" ) )
                SW_LOG_ERROR_ID( "XMLFields.UnknownField", "%#", text );
            else if ( StringUtil::equals( pID, "XMLFields.ValueUnreadable" ) )
                SW_LOG_ERROR_ID( "XMLFields.ValueUnreadable", "%#", text );
            else if ( StringUtil::equals( pID, "XMLFields.RequiredFieldMissing" ) )
                SW_LOG_ERROR_ID( "XMLFields.RequiredFieldMissing", "%#", text );
            else
                SW_LOG_ERROR_ID( "XMLFields.ValueOutOfRange", "%#", text );
        };
        // 오류 줄에 원소 이름과 id 를 함께 적는다(같은 원소가 여럿이면 어느 것인지 알아야 고친다).
        const utf8* const pID = node.findAttribute( "id" );
        string            element( node.getName() != nullptr ? node.getName() : "?" );
        if ( pID != nullptr )
            element += string( " id=\"" ) + pID + "\"";
        const utf8* const       pElement     = element.c_str();
        const SerializeContext& context      = SerializeContext::getDefault( SerializePurpose::Authoring );
        const auto&             listProperty = typeInfo.getPropertiesWithBase();
        bool                    bOk          = true;
        // 칸마다의 속성 이름은 한 번만 만든다 — 속성마다 칸 수만큼 글을 새로 만들지 않는다.
        vector<string> listAttributeName;
        vector<uint8>  listWritten( listProperty.size(), uint8{ 0 } ); // 범위는 적힌 칸만 본다 — 적지 않은 칸은 코드의 초기값이다
        listAttributeName.reserve( listProperty.size() );
        for ( const PropertyInfo& prop : listProperty )
        {
            listAttributeName.push_back( XMLFields::getAttributeName( prop ) );
        }

        // 1) 적힌 속성마다 칸을 찾아 읽는다. 모르는 이름은 오타다 — 조용히 넘기면 그 칸은 기본값으로 남는다.
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            const string_view attributeName{ attribute.getName() != nullptr ? attribute.getName() : "" };
            const bool        bHandRead = Internal::isHandRead( attributeName, listHandRead ) || ( pfnHandRead != nullptr && pfnHandRead( attributeName.data() ) );
            if ( bHandRead )
                continue;
            const PropertyInfo* pFound = nullptr;
            for ( size_t propIndex = 0; propIndex < listProperty.size(); ++propIndex )
            {
                if ( listAttributeName[propIndex] != attributeName )
                    continue;
                pFound                 = &listProperty[propIndex];
                listWritten[propIndex] = 1;
            }
            if ( pFound == nullptr )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#: <%# %#=...> is not a field of %#", sourceName, pElement, attributeName, typeInfo._name.c_str() );
                report( "XMLFields.UnknownField", string( textBuilder.view() ) );
                bOk = false;
                continue;
            }
            const utf8* const pValue = attribute.getValue() != nullptr ? attribute.getValue() : "";
            // 목록 칸은 속성 하나에 낱말을 잇는다(`days="Mon, Tue"`, `times="60, 120"`) — 데이터가 쓰는 구분자를 모두 받는다.
            const ISequenceContainerWrapper* const pList = Internal::findTextList( *pFound, context );
            if ( pList != nullptr )
            {
                if ( Internal::readTextList( *pFound, *pList, pInstance, pValue, context ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "%#: <%# %#=\"%#\"> cannot be read as %#", sourceName, pElement, attributeName, pValue, pFound->_typeName.c_str() );
                    report( "XMLFields.ValueUnreadable", string( textBuilder.view() ) );
                    bOk = false;
                }
                continue;
            }
            if ( SerializerUtil::applyPropertyText( *pFound, MutableObjectAddress{ pInstance }, pValue, context ) == false )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#: <%# %#=\"%#\"> cannot be read as %#", sourceName, pElement, attributeName, pValue, pFound->_typeName.c_str() );
                report( "XMLFields.ValueUnreadable", string( textBuilder.view() ) );
                bOk = false;
            }
        }

        // 2) 필수 칸이 빠졌는지 본다. 초기값은 형식일 뿐이라 대신 쓰지 않는다. 부모를 가리키는 원소는 `RequiredAtRoot` 칸을 물려받는다.
        const RequiredScope scope = SerializerUtil::findRequiredScope( listProperty, [&node, &listAttributeName]( const size_t propIndex )
        {
            const utf8* const pText = node.findAttribute( listAttributeName[propIndex].c_str() );
            return pText != nullptr ? string_view( pText ) : string_view{};
        } );
        for ( size_t propIndex = 0; propIndex < listProperty.size(); ++propIndex )
        {
            const string&     attributeName = listAttributeName[propIndex];
            const utf8* const pAttribute    = node.findAttribute( attributeName.c_str() );
            const string_view writtenText   = pAttribute != nullptr ? string_view( pAttribute ) : string_view{};
            // 판정은 XML · JSON 직렬화기와 한 함수다 — 빠졌거나, 이름 · 경로 형식인데 빈 글이다.
            if ( SerializerUtil::isRequiredValueMissing( listProperty[propIndex], pAttribute != nullptr, pAttribute != nullptr ? &writtenText : nullptr, scope ) )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#: <%#> needs %#=\"...\" (%#)", sourceName, pElement, attributeName.c_str(), typeInfo._name.c_str() );
                report( "XMLFields.RequiredFieldMissing", string( textBuilder.view() ) );
                bOk = false;
            }
        }

        // 3) 범위는 자르지 않고 알린다 — 잘라 쓰면 데이터의 실수가 다른 값으로 조용히 돈다. 범위의 원본은 `PROPERTY( Min · Max )` 하나다.
        //    적힌 칸만 본다: 적지 않은 칸은 코드의 초기값이라 파일의 실수가 아니다(필수 칸이 빠지면 그 초기값의 범위 오류가 덧붙어 원인을 가렸다).
        vector<string> listRangeError;
        for ( size_t propIndex = 0; propIndex < listProperty.size(); ++propIndex )
        {
            if ( listWritten[propIndex] != 0 )
                SerializerUtil::collectPropertyRangeErrors( listProperty[propIndex], ObjectAddress{ pInstance }, string(), listRangeError );
        }
        for ( const string& rangeError : listRangeError )
        {
            // 받는 곳이 없으면 바이너리 · JSON 의 읽기 공통 끝과 같은 id 로도 남긴다.
            if ( sink._pfnAdd == nullptr )
                SW_LOG_ERROR_ID( "Serialization.ValueOutOfRange", "%#: %# - the read fails", typeInfo._fullyQualifiedName.c_str(), rangeError.c_str() );
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "%#: <%#> %#", sourceName, pElement, rangeError.c_str() );
            report( "XMLFields.ValueOutOfRange", string( textBuilder.view() ) );
            bOk = false;
        }
        return bOk;
    }

    void XMLFields::writeFields( const XMLNode& node, const void* pInstance, const void* pDefaults, const TypeInfo& typeInfo )
    {
        const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
        for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
        {
            if ( prop._bIsContainer == SW_TRUE || SerializerUtil::canCarryValueType( prop._typeName, context ) == false )
                continue;
            const string text         = SerializerUtil::formatPropertyText( prop, ObjectAddress{ pInstance }, context );
            const bool   bOmitDefault = pDefaults != nullptr && prop._metadata->_bRequired == SW_FALSE;
            if ( bOmitDefault && text == SerializerUtil::formatPropertyText( prop, ObjectAddress{ pDefaults }, context ) )
                continue;
            node.appendAttribute( XMLFields::getAttributeName( prop ).c_str(), string_view{ text } );
        }
    }

    bool XMLFields::readOptionalEnumText( const XMLNode& node, const utf8* pName, void* pValue, const hashed_string& enumTypeFqn, string_view sourceName, bool& outbPresent )
    {
        const utf8* const pText = node.findAttribute( pName );
        outbPresent             = pText != nullptr;
        if ( pText == nullptr )
            return true;
        if ( SerializerUtil::parseTextValue( MutableValueAddress{ pValue }, enumTypeFqn, pText, SerializeContext::getDefault( SerializePurpose::Authoring ) ) )
            return true;
        SW_LOG_ERROR_ID( "XMLFields.UnknownEnumValue", "%#: <%# %#=\"%#\"> is not a value of %#", sourceName, node.getName(), pName, pText, enumTypeFqn.c_str() );
        return false;
    }
} // namespace sw
