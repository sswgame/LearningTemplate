#include "pch.h"

#include "Engine/Serialization/XML/XMLFields.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct XMLFieldsInternal
        {
            /** @brief PROPERTY 이름의 데이터 속성 철자입니다(`readFields` 의 규칙 — 앞의 `_` 를 떼고, 불리언 `b` 접두는 떼어 첫 글자를 소문자로). */
            static string makeAttributeName( const PropertyInfo& prop )
            {
                string_view name = prop._name.view();
                if ( name.empty() == false && name.front() == '_' )
                    name.remove_prefix( 1 );
                const bool bBooleanPrefix = name.size() > 1 && name[0] == 'b' && 'A' <= name[1] && name[1] <= 'Z';
                if ( bBooleanPrefix == false )
                    return string( name );
                string attribute( name.substr( 1 ) );
                attribute[0] = static_cast<utf8>( attribute[0] - 'A' + 'a' );
                return attribute;
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
    bool XMLFields::readFields( const XMLNode& node, void* pInstance, const TypeInfo& typeInfo, string_view sourceName, std::initializer_list<string_view> listHandRead,
                                AcceptAttributeFunction pfnHandRead )
    {
        using Internal = XMLFieldsInternal;
        // 오류 줄에 원소 이름과 id 를 함께 적는다(같은 원소가 여럿이면 어느 것인지 알아야 고친다).
        const utf8* const pID = node.findAttribute( "id" );
        string            element( node.getName() != nullptr ? node.getName() : "?" );
        if ( pID != nullptr )
            element += string( " id=\"" ) + pID + "\"";
        const utf8* const       pElement     = element.c_str();
        const SerializeContext& context      = SerializeContext::getDefault( SerializePurpose::Authoring );
        const auto&             listProperty = typeInfo.getPropertiesWithBase();
        bool                    bOk          = true;

        // 1) 적힌 속성마다 칸을 찾아 읽는다. 모르는 이름은 오타다 — 조용히 넘기면 그 칸은 기본값으로 남는다.
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            const string_view attributeName{ attribute.getName() != nullptr ? attribute.getName() : "" };
            const bool        bHandRead = Internal::isHandRead( attributeName, listHandRead ) || ( pfnHandRead != nullptr && pfnHandRead( attributeName.data() ) );
            if ( bHandRead )
                continue;
            const PropertyInfo* pFound = nullptr;
            for ( const PropertyInfo& prop : listProperty )
            {
                if ( Internal::makeAttributeName( prop ) == attributeName )
                    pFound = &prop;
            }
            if ( pFound == nullptr )
            {
                SW_LOG_ERROR_ID( "XMLFields.UnknownField", "%#: <%# %#=...> is not a field of %#", sourceName, pElement, attributeName, typeInfo._name.c_str() );
                bOk = false;
                continue;
            }
            const utf8* const pValue = attribute.getValue() != nullptr ? attribute.getValue() : "";
            if ( SerializerUtil::applyPropertyText( *pFound, MutableObjectAddress{ pInstance }, pValue, context ) == false )
            {
                SW_LOG_ERROR_ID( "XMLFields.ValueUnreadable", "%#: <%# %#=\"%#\"> cannot be read as %#", sourceName, pElement, attributeName, pValue, pFound->_typeName.c_str() );
                bOk = false;
            }
        }

        // 2) 필수 칸이 빠졌는지 본다. 초기값은 형식일 뿐이라 대신 쓰지 않는다.
        for ( const PropertyInfo& prop : listProperty )
        {
            if ( prop._metadata->_bRequired == SW_FALSE )
                continue;
            const string attributeName = Internal::makeAttributeName( prop );
            if ( node.findAttribute( attributeName.c_str() ) == nullptr )
            {
                SW_LOG_ERROR_ID( "XMLFields.RequiredFieldMissing", "%#: <%#> needs %#=\"...\" (%#)", sourceName, pElement, attributeName.c_str(), typeInfo._name.c_str() );
                bOk = false;
            }
        }

        // 3) 범위는 자르지 않고 알린다 — 잘라 쓰면 데이터의 실수가 다른 값으로 조용히 돈다. 범위의 원본은 `PROPERTY( Min · Max )` 하나이고,
        //    바이너리 · JSON · XML 과 같은 읽기 공통 끝이 칸마다 알린다. 여기서는 어느 파일 · 원소인지만 덧붙인다.
        if ( SerializerUtil::isWithinDeclaredRanges( typeInfo, ObjectAddress{ pInstance } ) == false )
        {
            SW_LOG_ERROR_ID( "XMLFields.ValueOutOfRange", "%#: <%#> has values outside PROPERTY( Min, Max ) of %# (listed above)", sourceName, pElement, typeInfo._name.c_str() );
            bOk = false;
        }
        return bOk;
    }

    void XMLFields::writeFields( const XMLNode& node, const void* pInstance, const void* pDefaults, const TypeInfo& typeInfo )
    {
        using Internal                  = XMLFieldsInternal;
        const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
        for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
        {
            if ( prop._bIsContainer == SW_TRUE || SerializerUtil::canCarryValueType( prop._typeName, context ) == false )
                continue;
            const string text = SerializerUtil::formatPropertyText( prop, ObjectAddress{ pInstance }, context );
            if ( prop._metadata->_bRequired == SW_FALSE && text == SerializerUtil::formatPropertyText( prop, ObjectAddress{ pDefaults }, context ) )
                continue;
            node.appendAttribute( Internal::makeAttributeName( prop ).c_str(), string_view{ text } );
        }
    }
} // namespace sw
