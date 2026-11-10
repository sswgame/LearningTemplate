#include "pch.h"

#include "Engine/Reflection/InstancedStruct.h"

#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw
{
    namespace
    {
        struct InstancedStructInternal
        {
            /** @brief 원소가 값 글 하나로 읽히는 시퀀스 칸이면 그 시퀀스입니다(`XMLFields` 의 한 줄 목록과 같은 기준). */
            [[nodiscard]] static ISequenceContainerWrapper* findTextList( const PropertyInfo& prop, const SerializeContext& context )
            {
                const bool bSequence = prop._bIsContainer == SW_TRUE && prop._containerWrapper != nullptr && ( prop._nestedContainer == nullptr || prop._nestedContainer->_elementNested == nullptr );
                if ( bSequence == false || SerializerUtil::canCarryValueType( prop._elementTypeName, context ) == false )
                    return nullptr;
                return prop._containerWrapper->asSequence();
            }

            /** @brief 칸 하나에 글 하나를 씁니다. 목록 칸이면 뒤에 더합니다. */
            [[nodiscard]] static bool applyToken( const PropertyInfo& prop, void* pInstance, string_view token, const SerializeContext& context )
            {
                ISequenceContainerWrapper* const pList = findTextList( prop, context );
                if ( pList == nullptr )
                    return SerializerUtil::applyPropertyText( prop, MutableObjectAddress{ pInstance }, token, context );
                void* const pContainer = prop.getRawPtr( pInstance );
                pList->addElementDefault( pContainer );
                void* const pElement = pList->getElement( pContainer, pList->getSize( pContainer ) - 1 );
                return SerializerUtil::parseTextValue( MutableValueAddress{ pElement }, prop._elementTypeName, token, context );
            }

            /** @brief 목록 칸의 글입니다. 원소를 쉼표로 잇습니다. */
            [[nodiscard]] static string formatList( const PropertyInfo& prop, ISequenceContainerWrapper& list, const void* pInstance, const SerializeContext& context )
            {
                void* const  pContainer = prop.getRawPtr( const_cast<void*>( pInstance ) ); // 시퀀스 접근이 비 const 다 - 읽기만 한다
                string       text;
                const size_t count = list.getSize( pContainer );
                for ( size_t index = 0; index < count; ++index )
                {
                    text += index == 0 ? "" : ", ";
                    text += SerializerUtil::formatValueText( ValueAddress{ list.getElement( pContainer, index ) }, prop._elementTypeName, context );
                }
                return text;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    InstancedStruct::InstancedStruct()
        : _pOps{ nullptr }
        , _pInstance{}
    {
    }

    InstancedStruct::InstancedStruct( const InstancedStructOps& ops )
        : _pOps{ &ops }
        , _pInstance{ ops._pfnCreate() }
    {
    }

    InstancedStruct::InstancedStruct( const InstancedStruct& other )
        : _pOps{ other._pOps }
        , _pInstance{ other._pInstance != nullptr ? other._pOps->_pfnClone( other._pInstance.get() ) : shared_ptr<void>{} }
    {
    }

    InstancedStruct& InstancedStruct::operator=( const InstancedStruct& other )
    {
        if ( this == &other )
            return *this;
        _pOps      = other._pOps;
        _pInstance = other._pInstance != nullptr ? other._pOps->_pfnClone( other._pInstance.get() ) : shared_ptr<void>{};
        return *this;
    }

    const TypeInfo* InstancedStruct::getType() const
    {
        return _pOps != nullptr ? _pOps->_pfnGetType() : nullptr;
    }
} // namespace sw

namespace sw
{
    void InstancedStructText::splitArguments( string_view text, vector<string_view>& outListToken )
    {
        outListToken.clear();
        StringUtil::forEachToken( text, ",", [&]( string_view token )
        {
            const string_view trimmed = StringUtil::trim( token );
            if ( trimmed.empty() == false )
                outListToken.push_back( trimmed );
        } );
    }

    expected<void> InstancedStructText::read( void* pInstance, const TypeInfo& typeInfo, const vector<string_view>& listToken )
    {
        using Internal                       = InstancedStructInternal;
        const SerializeContext& context      = SerializeContext::getDefault( SerializePurpose::Authoring );
        const auto&             listProperty = typeInfo.getPropertiesWithBase();
        vector<uint8>           listWritten( listProperty.size(), uint8{ 0 } );
        vector<string_view>     listWrittenText( listProperty.size() ); // 필수 칸 판정(빈 이름)에 쓴다 — 글은 @p listToken 안을 가리킨다
        size_t                  nextPositional = 0;
        for ( const string_view token : listToken )
        {
            // 이름을 적었으면 그 칸, 아니면 다음 칸이다. 목록 칸은 남은 글을 모두 받으므로 다음 칸으로 넘어가지 않는다.
            size_t       propIndex = listProperty.size();
            string_view  value     = token;
            const size_t equalPos  = token.find( '=' );
            if ( equalPos != string_view::npos )
            {
                const string_view name = StringUtil::trim( token.substr( 0, equalPos ) );
                value                  = StringUtil::trim( token.substr( equalPos + 1 ) );
                for ( size_t index = 0; index < listProperty.size(); ++index )
                {
                    if ( XMLFields::getAttributeName( listProperty[index] ) == name )
                        propIndex = index;
                }
                if ( propIndex == listProperty.size() )
                    return unexpected( Error{ ErrorKind::NotFound, "not a field of the type", name }.appendDetail( " in " ).appendDetail( typeInfo._name.c_str() ) );
            }
            else
            {
                propIndex = nextPositional;
                if ( propIndex >= listProperty.size() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "too many arguments", typeInfo._name.c_str() }.appendDetail( " at '" ).appendDetail( token ).appendDetail( "'" ) );
                if ( Internal::findTextList( listProperty[propIndex], context ) == nullptr )
                    ++nextPositional;
            }
            if ( Internal::applyToken( listProperty[propIndex], pInstance, value, context ) == false )
            {
                StringBuilder<constant::buffer::kMax256> detailBuilder;
                detailBuilder.appendFormat( "'%#' as %# of %#", value, XMLFields::getAttributeName( listProperty[propIndex] ).c_str(), typeInfo._name.c_str() );
                return unexpected( Error{ ErrorKind::InvalidArgument, "a value cannot be read as its field", detailBuilder.view() } );
            }
            listWritten[propIndex]     = 1;
            listWrittenText[propIndex] = value;
        }
        vector<string> listRangeError;
        for ( size_t index = 0; index < listProperty.size(); ++index )
        {
            if ( listWritten[index] != 0 )
                SerializerUtil::collectPropertyRangeErrors( listProperty[index], ObjectAddress{ pInstance }, string(), listRangeError );
            // 판정은 직렬화기 · XMLFields 와 한 함수다 — 빠졌거나, 이름 · 경로 형식인데 빈 글이다.
            if ( SerializerUtil::isRequiredValueMissing( listProperty[index], listWritten[index] != 0, listWritten[index] != 0 ? &listWrittenText[index] : nullptr ) )
                return unexpected( Error{ ErrorKind::InvalidArgument, "a required field is missing", typeInfo._name.c_str() }.appendDetail( "." ).appendDetail( XMLFields::getAttributeName( listProperty[index] ).c_str() ) );
        }
        if ( listRangeError.empty() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a field is out of range", listRangeError.front() } );
        return {};
    }

    string InstancedStructText::write( const void* pInstance, const void* pDefaults, const TypeInfo& typeInfo )
    {
        using Internal                  = InstancedStructInternal;
        const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
        string                  text;
        for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
        {
            ISequenceContainerWrapper* const pList = Internal::findTextList( prop, context );
            if ( pList == nullptr && ( prop._bIsContainer == SW_TRUE || SerializerUtil::canCarryValueType( prop._typeName, context ) == false ) )
                continue;
            const string value        = pList != nullptr ? Internal::formatList( prop, *pList, pInstance, context ) : SerializerUtil::formatPropertyText( prop, ObjectAddress{ pInstance }, context );
            const string defaultValue = pList != nullptr ? Internal::formatList( prop, *pList, pDefaults, context ) : SerializerUtil::formatPropertyText( prop, ObjectAddress{ pDefaults }, context );
            if ( prop._metadata->_bRequired == SW_FALSE && value == defaultValue )
                continue;
            text += text.empty() ? "" : ", ";
            text += XMLFields::getAttributeName( prop );
            text += "=";
            text += value;
        }
        return text;
    }
} // namespace sw
