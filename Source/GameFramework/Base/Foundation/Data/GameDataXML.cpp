#include "pch.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
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

            /** @brief @p text 가 정확히 `Count` 개의 실수로 읽힐 때만 @p outArr 를 채우고 true 입니다(모자라거나 남거나 숫자가 아닌 토큰이 있으면 false, @p outArr 는 그대로). */
            template <size_t Count>
            [[nodiscard]] static bool parseExactFloats( string_view text, float32 ( &outArr )[Count] )
            {
                float32 arrValue[Count] = {};
                size_t  tokenCount      = 0;
                bool    bValid          = true;
                StringUtil::forEachToken( text, kNumberSeparators, [&]( string_view token )
                {
                    float32 value = 0.0f;
                    if ( tokenCount >= Count || StringUtil::parseFloat( token, value ) == false )
                        bValid = false;
                    else
                        arrValue[tokenCount] = value;
                    ++tokenCount;
                } );
                if ( bValid == false || tokenCount != Count )
                    return false;
                for ( size_t index = 0; index < Count; ++index )
                {
                    outArr[index] = arrValue[index];
                }
                return true;
            }

            /**
             * @brief 속성 하나를 @p pParse 로 읽어 @p inoutValue 에 담습니다. 선택 속성(@p bRequired = false)은 없으면 값을 두고 true 입니다.
             * @details 있는데 읽히지 않거나 필수인데 없으면 어느 파일 · 원소 · 속성인지 오류로 알리고 false 입니다(값은 그대로).
             */
            template <typename TVector>
            [[nodiscard]] static bool readFloatAttribute( const XMLNode& node, const utf8* pAttribute, string_view sourceName, bool bRequired, const utf8* pShape, TVector& inoutValue,
                                                          bool ( *pParse )( string_view, TVector& ) )
            {
                const utf8* pText = node.findAttribute( pAttribute );
                if ( pText == nullptr )
                {
                    if ( bRequired == false )
                        return true;
                    SW_LOG_ERROR( "%#: <%#> needs %#=\"%#\"", sourceName, node.getName(), pAttribute, pShape );
                    return false;
                }
                TVector parsed{};
                if ( pParse( string_view( pText ), parsed ) == false )
                {
                    SW_LOG_ERROR( "%#: <%#> %#=\"%#\" must be numbers shaped \"%#\"", sourceName, node.getName(), pAttribute, pText, pShape );
                    return false;
                }
                inoutValue = parsed;
                return true;
            }

            /** @brief `GameDataXML::readOptionalNumber` 의 몸통입니다. 읽기 · 범위 검사는 `XMLNode` 하나가 하고, 여기서는 어느 파일인지만 덧붙입니다. */
            template <typename T>
            [[nodiscard]] static bool readOptionalInRange( const XMLNode& node, const utf8* pAttribute, string_view sourceName, T minValue, T maxValue, T& inoutValue )
            {
                if ( node.readOptionalAttributeInRange( pAttribute, minValue, maxValue, inoutValue ) )
                    return true;
                SW_LOG_ERROR( "%#: <%#> has an invalid %# (listed above)", sourceName, node.getName(), pAttribute );
                return false;
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

    bool GameDataXML::readFields( const XMLNode& node, void* pInstance, const TypeInfo& typeInfo, string_view sourceName, std::initializer_list<string_view> listHandRead,
                                  AcceptAttributeFunction pfnHandRead )
    {
        using Internal = GameDataXMLInternal;
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
                SW_LOG_ERROR( "%#: <%# %#=...> is not a field of %#", sourceName, pElement, attributeName, typeInfo._name.c_str() );
                bOk = false;
                continue;
            }
            const utf8* const pValue = attribute.getValue() != nullptr ? attribute.getValue() : "";
            if ( SerializerUtil::applyPropertyText( *pFound, MutableObjectAddress{ pInstance }, pValue, context ) == false )
            {
                SW_LOG_ERROR( "%#: <%# %#=\"%#\"> cannot be read as %#", sourceName, pElement, attributeName, pValue, pFound->_typeName.c_str() );
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
                SW_LOG_ERROR( "%#: <%#> needs %#=\"...\" (%#)", sourceName, pElement, attributeName.c_str(), typeInfo._name.c_str() );
                bOk = false;
            }
        }

        // 3) 범위는 자르지 않고 알린다 — 잘라 쓰면 데이터의 실수가 다른 값으로 조용히 돈다. 범위의 원본은 `PROPERTY( Min · Max )` 하나이고,
        //    바이너리 · JSON · XML 과 같은 읽기 공통 끝이 칸마다 알린다. 여기서는 어느 파일 · 원소인지만 덧붙인다.
        if ( SerializerUtil::isWithinDeclaredRanges( typeInfo, ObjectAddress{ pInstance } ) == false )
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

    bool GameDataXML::parseFloatList( string_view text, uint32 maxCount, vector<float32>& outListValue )
    {
        vector<float32> listValue;
        bool            bValid = true;
        StringUtil::forEachToken( text, GameDataXMLInternal::kNumberSeparators, [&]( string_view token )
        {
            float32 value = 0.0f;
            if ( listValue.size() >= maxCount || StringUtil::parseFloat( token, value ) == false )
                bValid = false;
            else
                listValue.push_back( value );
        } );
        if ( bValid == false || listValue.empty() )
            return false;
        outListValue = std::move( listValue );
        return true;
    }

    bool GameDataXML::parseFloat2( string_view text, float2& outValue )
    {
        float32 arrValue[2] = {};
        if ( GameDataXMLInternal::parseExactFloats( text, arrValue ) == false )
            return false;
        outValue = float2{ arrValue[0], arrValue[1] };
        return true;
    }

    bool GameDataXML::parseFloat3( string_view text, float3& outValue )
    {
        float32 arrValue[3] = {};
        if ( GameDataXMLInternal::parseExactFloats( text, arrValue ) == false )
            return false;
        outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
        return true;
    }

    bool GameDataXML::parseFloat4( string_view text, float4& outValue )
    {
        float32 arrValue[4] = {};
        if ( GameDataXMLInternal::parseExactFloats( text, arrValue ) == false )
            return false;
        outValue = float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
        return true;
    }

    bool GameDataXML::readOptionalFloat2( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float2& inoutValue )
    {
        return GameDataXMLInternal::readFloatAttribute( node, pAttribute, sourceName, false, "x y", inoutValue, &GameDataXML::parseFloat2 );
    }

    bool GameDataXML::readOptionalFloat3( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float3& inoutValue )
    {
        return GameDataXMLInternal::readFloatAttribute( node, pAttribute, sourceName, false, "x y z", inoutValue, &GameDataXML::parseFloat3 );
    }

    bool GameDataXML::readRequiredFloat3( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float3& outValue )
    {
        return GameDataXMLInternal::readFloatAttribute( node, pAttribute, sourceName, true, "x y z", outValue, &GameDataXML::parseFloat3 );
    }

    bool GameDataXML::readOptionalNumber( const XMLNode& node, const utf8* pAttribute, string_view sourceName, int32 minValue, int32 maxValue, int32& inoutValue )
    {
        return GameDataXMLInternal::readOptionalInRange( node, pAttribute, sourceName, minValue, maxValue, inoutValue );
    }

    bool GameDataXML::readOptionalNumber( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float32 minValue, float32 maxValue, float32& inoutValue )
    {
        return GameDataXMLInternal::readOptionalInRange( node, pAttribute, sourceName, minValue, maxValue, inoutValue );
    }

    bool GameDataXML::readOptionalBool( const XMLNode& node, const utf8* pAttribute, bool& inoutValue )
    {
        std::optional<bool> value;
        if ( node.readOptionalAttribute( pAttribute, value ) == false )
            return false;
        if ( value.has_value() )
            inoutValue = *value;
        return true;
    }

    bool GameDataXML::readOptionalFlag( const XMLNode& node, const utf8* pAttribute, uint8& inoutFlag )
    {
        bool bValue = inoutFlag != SW_FALSE;
        if ( readOptionalBool( node, pAttribute, bValue ) == false )
            return false;
        inoutFlag = bValue ? SW_TRUE : SW_FALSE;
        return true;
    }

    bool GameDataXML::readFloatCurve( const XMLNode& parent, const utf8* pElement, float32 minValue, FloatCurve& outCurve, string_view sourceName )
    {
        outCurve._listKey.clear();
        const XMLNode curveNode = parent.findChild( pElement );
        if ( curveNode.isValid() == false )
            return true;

        bool bValid = XMLNameCheck::reportUnknownChildren( curveNode, GameDataXMLInternal::kArrCurveChild, sourceName );
        for ( XMLNode keyNode = curveNode.findChild( "Key" ); keyNode; keyNode = keyNode.findNextSibling( "Key" ) )
        {
            bValid = XMLNameCheck::reportUnknownAttributes( keyNode, GameDataXMLInternal::kArrCurveKeyAttribute, sourceName ) && bValid;
            // 키의 시간과 값은 필수다. 값은 @p minValue 아래면 자르지 않고 오류다.
            const std::optional<float32> time  = keyNode.readRequiredAttribute<float32>( "time" );
            const std::optional<float32> value = keyNode.readRequiredAttribute<float32>( "value" );
            if ( time.has_value() == false || value.has_value() == false )
            {
                bValid = false;
                continue;
            }
            if ( *value < minValue )
            {
                SW_LOG_ERROR( "%#: <Key time=\"%#\" value=\"%#\"> is below %#", sourceName, *time, *value, minValue );
                bValid = false;
                continue;
            }
            FloatCurveKey key{};
            key._time                           = *time;
            key._value                          = *value;
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
                    SW_LOG_ERROR( "%#: <Key> has unknown interpolation '%#'", sourceName, interpolationText );
                    bValid = false;
                    continue;
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
