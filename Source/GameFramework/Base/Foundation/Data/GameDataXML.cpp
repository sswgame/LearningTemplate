#include "pch.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"
#include "Engine/Utility/FloatCurve.h"

namespace sw
{
    SW_LOG_CALLER( "GameDataXML" );

    namespace
    {
        struct GameDataXMLInternal
        {
            static constexpr const utf8* kNumberSeparators = ", \t;";

            static constexpr const utf8* kArrCurveChild[]        = { "Key" };
            static constexpr const utf8* kArrCurveKeyAttribute[] = { "time", "value", "interpolation" };

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
                for ( const string_view handRead : listHandRead )
                {
                    if ( handRead == attribute )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool GameDataXML::loadRoot( XMLDocument& doc, string_view path, const utf8* pRootName, XMLNode& outRoot, string& outSourceName )
    {
        outSourceName = string( path );
        switch ( XMLReadUtil::loadRoot( doc, path, pRootName, outRoot, &outSourceName ) )
        {
            case XMLRootStatus::Found:
            {
                return true;
            }
            case XMLRootStatus::NotLoaded:
            {
                SW_LOG_WARNING( "Failed to read %#", path );
                return false;
            }
            case XMLRootStatus::RootMissing:
            {
                SW_LOG_WARNING( "Missing <%#> root in %#", pRootName, outSourceName );
                return false;
            }
        }
        return false;
    }

    bool GameDataXML::parseRoot( XMLDocument& doc, string_view xmlText, string_view sourceName, const utf8* pRootName, XMLNode& outRoot )
    {
        switch ( XMLReadUtil::parseRoot( doc, xmlText, sourceName, pRootName, outRoot ) )
        {
            case XMLRootStatus::Found:
            {
                return true;
            }
            case XMLRootStatus::NotLoaded:
            {
                SW_LOG_WARNING( "Failed to parse %#", sourceName );
                return false;
            }
            case XMLRootStatus::RootMissing:
            {
                SW_LOG_WARNING( "Missing <%#> root in %#", pRootName, sourceName );
                return false;
            }
        }
        return false;
    }

    bool GameDataXML::loadFileWith( void* pCall, ReadRootFunction pReadRoot, string_view path, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        return loadRoot( doc, path, pRootName, root, sourceName ) && pReadRoot( pCall, root, sourceName );
    }

    bool GameDataXML::loadTextWith( void* pCall, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        return parseRoot( doc, xmlText, sourceName, pRootName, root ) && pReadRoot( pCall, root, sourceName );
    }

    bool GameDataXML::readFields( const XMLNode& node, void* pInstance, const TypeInfo& typeInfo, string_view sourceName, std::initializer_list<string_view> listHandRead )
    {
        using Internal                       = GameDataXMLInternal;
        const utf8* const       pElement     = node.getName() != nullptr ? node.getName() : "?";
        const SerializeContext& context      = SerializeContext::getDefault();
        const auto&             listProperty = typeInfo.getPropertiesWithBase();
        bool                    bOk          = true;

        // 1) 적힌 속성마다 칸을 찾아 읽는다. 모르는 이름은 오타다 — 조용히 넘기면 그 칸은 기본값으로 남는다.
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            const string_view attributeName{ attribute.getName() != nullptr ? attribute.getName() : "" };
            if ( Internal::isHandRead( attributeName, listHandRead ) )
                continue;
            const PropertyInfo* pFound = nullptr;
            for ( const PropertyInfo& prop : listProperty )
            {
                if ( Internal::makeAttributeName( prop ) == attributeName )
                    pFound = &prop;
            }
            if ( pFound == nullptr )
            {
                SW_LOG_ERROR( "%#: <%# %#=...> is not a field of %#", sourceName, pElement, attributeName, typeInfo._name.c_str() );
                bOk = false;
                continue;
            }
            const utf8* const pValue = attribute.getValue() != nullptr ? attribute.getValue() : "";
            if ( SerializerUtil::applyPropertyText( *pFound, pInstance, pValue, context ) == false )
            {
                SW_LOG_ERROR( "%#: <%# %#=\"%#\"> cannot be read as %#", sourceName, pElement, attributeName, pValue, pFound->_typeName.c_str() );
                bOk = false;
            }
        }

        // 2) 필수 칸이 빠졌는지 본다. 초기값은 형식일 뿐이라 대신 쓰지 않는다.
        for ( const PropertyInfo& prop : listProperty )
        {
            if ( prop._metadata._bRequired == SW_FALSE )
                continue;
            const string attributeName = Internal::makeAttributeName( prop );
            if ( node.findAttribute( attributeName.c_str() ) == nullptr )
            {
                SW_LOG_ERROR( "%#: <%#> needs %#=\"...\" (%#)", sourceName, pElement, attributeName.c_str(), typeInfo._name.c_str() );
                bOk = false;
            }
        }

        // 3) 범위는 자르지 않고 알린다 — 잘라 쓰면 데이터의 실수가 다른 값으로 조용히 돈다. 범위의 원본은 `PROPERTY( Min · Max )` 하나이고,
        //    바이너리 · JSON · XML 과 같은 읽기 공통 끝이 칸마다 알린다. 여기서는 어느 파일 · 원소인지만 덧붙인다.
        if ( SerializerUtil::isWithinDeclaredRanges( typeInfo, pInstance ) == false )
        {
            SW_LOG_ERROR( "%#: <%#> has values outside PROPERTY( Min, Max ) of %# (listed above)", sourceName, pElement, typeInfo._name.c_str() );
            bOk = false;
        }
        return bOk;
    }

    const utf8* GameDataXML::findRequiredID( const XMLNode& node, string_view sourceName )
    {
        const utf8* pID = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pID ) )
        {
            SW_LOG_WARNING( "%#: <%#> without an id - skipped", sourceName, node.getName() );
            return nullptr;
        }
        return pID;
    }

    hashed_string GameDataXML::readName( const XMLNode& node, const utf8* pAttribute )
    {
        return XMLReadUtil::readName( node, pAttribute );
    }

    void GameDataXML::parseNameList( string_view text, string_view separators, vector<hashed_string>& outListName )
    {
        outListName.clear();
        appendNameList( text, separators, outListName );
    }

    void GameDataXML::appendNameList( string_view text, string_view separators, vector<hashed_string>& outListName )
    {
        StringUtil::forEachToken( text, separators, [&]( string_view token )
        { outListName.push_back( hashed_string( token ) ); } );
    }

    uint32 GameDataXML::parseFloats( string_view text, float32* pOutValue, uint32 maxCount )
    {
        if ( pOutValue == nullptr )
            return 0;
        uint32 tokenIndex = 0;
        uint32 readCount  = 0;
        StringUtil::forEachToken( text, GameDataXMLInternal::kNumberSeparators, [&]( string_view token )
        {
            if ( tokenIndex >= maxCount )
                return;
            float32 value = 0.0f;
            if ( StringUtil::parseFloat( token, value ) )
            {
                pOutValue[tokenIndex] = value;
                ++readCount;
            }
            ++tokenIndex;
        } );
        return readCount;
    }

    float4 GameDataXML::parseFloat4( string_view text, const float4& fallback )
    {
        float32 arrValue[4] = { fallback._x, fallback._y, fallback._z, fallback._w };
        (void)parseFloats( text, arrValue, 4 ); // 빠진 성분은 기본값 그대로
        return float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
    }

    float3 GameDataXML::parseFloat3( string_view text, const float3& fallback )
    {
        float32 arrValue[3] = { fallback._x, fallback._y, fallback._z };
        (void)parseFloats( text, arrValue, 3 ); // 못 읽은 칸은 대체값이 남는다
        return float3{ arrValue[0], arrValue[1], arrValue[2] };
    }

    bool GameDataXML::readFloatCurve( const XMLNode& parent, const utf8* pElement, float32 minValue, FloatCurve& outCurve, string_view sourceName )
    {
        outCurve._listKey.clear();
        const XMLNode curveNode = parent.findChild( pElement );
        if ( curveNode.isValid() == false )
            return true;

        bool bValid = XMLNameCheck::reportUnknownChildren( curveNode, GameDataXMLInternal::kArrCurveChild, sourceName, LogLevel::Warning );
        for ( XMLNode keyNode = curveNode.findChild( "Key" ); keyNode; keyNode = keyNode.findNextSibling( "Key" ) )
        {
            bValid = XMLNameCheck::reportUnknownAttributes( keyNode, GameDataXMLInternal::kArrCurveKeyAttribute, sourceName, LogLevel::Warning ) && bValid;
            FloatCurveKey key{};
            key._time                           = keyNode.getAttributeFloat( "time", 0.0f );
            key._value                          = MathUtil::max( minValue, keyNode.getAttributeFloat( "value", 1.0f ) );
            key._interpolation                  = CurveInterpolation::Linear;
            const string_view interpolationText = keyNode.getAttributeText( "interpolation" );
            if ( interpolationText.empty() == false )
            {
                if ( StringUtil::equals( interpolationText, "linear", true ) )
                    key._interpolation = CurveInterpolation::Linear;
                else if ( StringUtil::equals( interpolationText, "constant", true ) )
                    key._interpolation = CurveInterpolation::Constant;
                else if ( StringUtil::equals( interpolationText, "cubic", true ) )
                    key._interpolation = CurveInterpolation::Cubic;
                else
                {
                    SW_LOG_WARNING( "%#: <Key> has unknown interpolation '%#' - linear is used", sourceName, interpolationText );
                    bValid = false;
                }
            }
            outCurve._listKey.push_back( key );
        }
        outCurve.sortAndComputeAutoTangents();
        return bValid;
    }

    bool GameDataXML::parseNumber( string_view token, int32& outValue )
    {
        return StringUtil::parseInt( token, outValue );
    }

    bool GameDataXML::parseNumber( string_view token, float32& outValue )
    {
        return StringUtil::parseFloat( token, outValue );
    }
} // namespace sw
