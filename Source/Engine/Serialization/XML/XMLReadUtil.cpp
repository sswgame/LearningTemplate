#include "pch.h"

#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    SW_LOG_CALLER( "XMLReadUtil" );

    namespace
    {
        struct XMLReadUtilInternal
        {
            /** @brief 실수 묶음(`parseFloat2` …) · 실수 목록의 구분자입니다. */
            static constexpr const utf8* kNumberSeparators = ", \t;";

            /** @brief @p text 가 정확히 `Count` 개의 실수로 읽힐 때만 @p outArr 를 채우고 true 입니다(규칙은 `StringUtil::parseExactFloats`). */
            template <uint32 Count>
            [[nodiscard]] static bool parseExactFloats( string_view text, float32 ( &outArr )[Count] )
            {
                return StringUtil::parseExactFloats( text, kNumberSeparators, outArr, Count );
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
                    SW_LOG_ERROR_ID( "GameData.AttributeMissing", "%#: <%#> needs %#=\"%#\"", sourceName, node.getName(), pAttribute, pShape );
                    return false;
                }
                TVector parsed{};
                if ( pParse( string_view( pText ), parsed ) == false )
                {
                    SW_LOG_ERROR_ID( "GameData.AttributeShapeMismatch", "%#: <%#> %#=\"%#\" must be numbers shaped \"%#\"", sourceName, node.getName(), pAttribute, pText, pShape );
                    return false;
                }
                inoutValue = parsed;
                return true;
            }

            /** @brief `readOptionalNumber` 의 몸통입니다. 읽기 · 범위 검사는 `XMLNode` 하나가 하고, 여기서는 어느 파일인지만 덧붙입니다. */
            template <typename T>
            [[nodiscard]] static bool readOptionalInRange( const XMLNode& node, const utf8* pAttribute, string_view sourceName, T minValue, T maxValue, T& inoutValue )
            {
                if ( node.readOptionalAttributeInRange( pAttribute, minValue, maxValue, inoutValue ) )
                    return true;
                SW_LOG_ERROR_ID( "GameData.ValueInvalid", "%#: <%#> has an invalid %# (listed above)", sourceName, node.getName(), pAttribute );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    XMLRootStatus XMLReadUtil::loadRoot( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot, string* pOutAbsPath )
    {
        if ( outDocument.loadResource( path, pOutAbsPath ) == false )
            return XMLRootStatus::NotLoaded;
        outRoot = outDocument.getRoot( pRootName );
        return outRoot.isValid() ? XMLRootStatus::Found : XMLRootStatus::RootMissing;
    }

    XMLRootStatus XMLReadUtil::parseRoot( XMLDocument& outDocument, string_view xmlText, string_view sourceName, const utf8* pRootName,
                                          XMLNode& outRoot )
    {
        if ( outDocument.parse( xmlText, sourceName ) == false )
            return XMLRootStatus::NotLoaded;
        outRoot = outDocument.getRoot( pRootName );
        return outRoot.isValid() ? XMLRootStatus::Found : XMLRootStatus::RootMissing;
    }

    hashed_string XMLReadUtil::readOptionalName( const XMLNode& node, const utf8* pAttribute )
    {
        const string_view text = StringUtil::trim( node.getAttributeText( pAttribute ) );
        if ( text.empty() )
            return hashed_string{};
        const hashed_string name( text );
        // 빈 이름의 철자(`None`, 대소문자 무시)는 이름 값이 될 수 없다 — 담으면 "적지 않음"과 같아진다(언리얼 `NAME_None` 과 같은 함정).
        // 그 글을 값으로 쓰는 속성(열거형 `align="none"`)은 글 그대로 읽어야 하므로, 이름으로 읽은 것을 오류로 알린다.
        if ( name.empty() )
            SW_LOG_ERROR( "<%# %#=\"%#\">: '%#' spells the empty name, so a name cannot hold it - read this attribute as text",
                          node.getName() != nullptr ? node.getName() : "?", pAttribute, text, text );
        return name;
    }

    bool XMLReadUtil::hasOnlyKnownNames( const XMLNode& node, std::initializer_list<string_view> listAttribute, std::initializer_list<string_view> listChild, string_view context )
    {
        bool bKnown = true;
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( VectorUtil::contains( listAttribute, string_view{ attribute.getName() } ) )
                continue;
            SW_LOG_ERROR( "%#: unknown attribute '%#'", context, attribute.getName() );
            bKnown = false;
        }
        for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( VectorUtil::contains( listChild, string_view{ child.getName() } ) )
                continue;
            SW_LOG_ERROR( "%#: unknown element '%#'", context, child.getName() );
            bKnown = false;
        }
        return bKnown;
    }

    bool XMLReadUtil::readFloatList( string_view text, float32* pOutValue, uint32 count )
    {
        return StringUtil::parseExactFloats( text, ", \t\r\n", pOutValue, count );
    }

    string XMLReadUtil::makeFloatListText( const float32* pValue, uint32 count )
    {
        string text;
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( index > 0 )
                text.push_back( ',' );
            text.append( to_string( pValue[index] ) );
        }
        return text;
    }

    bool XMLReadUtil::loadRootOrWarn( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot, string& outSourceName )
    {
        outSourceName = string( path );
        switch ( loadRoot( outDocument, path, pRootName, outRoot, &outSourceName ) )
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

    bool XMLReadUtil::parseRootOrWarn( XMLDocument& outDocument, string_view xmlText, string_view sourceName, const utf8* pRootName, XMLNode& outRoot )
    {
        switch ( parseRoot( outDocument, xmlText, sourceName, pRootName, outRoot ) )
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

    const utf8* XMLReadUtil::findRequiredID( const XMLNode& node, string_view sourceName )
    {
        const utf8* pID = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pID ) )
        {
            SW_LOG_WARNING( "%#: <%#> without an id - skipped", sourceName, node.getName() );
            return nullptr;
        }
        return pID;
    }

    bool XMLReadUtil::readRequiredName( const XMLNode& node, const utf8* pAttribute, string_view sourceName, hashed_string& outName )
    {
        outName = readOptionalName( node, pAttribute );
        if ( outName.empty() == false )
            return true;
        SW_LOG_ERROR( "%#: <%#> needs a non-empty name attribute '%#'", sourceName, node.getName() != nullptr ? node.getName() : "?", pAttribute );
        return false;
    }

    void XMLReadUtil::parseNameList( string_view text, string_view separators, vector<hashed_string>& outListName )
    {
        outListName.clear();
        appendNameList( text, separators, outListName );
    }

    void XMLReadUtil::appendNameList( string_view text, string_view separators, vector<hashed_string>& outListName )
    {
        StringUtil::forEachToken( text, separators, [&]( string_view token )
        {
            const hashed_string name( token );
            // 빈 이름의 철자(`none`)는 이름이 될 수 없다 — 목록에 빈 이름이 조용히 들어가지 않게 오류로 알리고 넣지 않는다.
            if ( name.empty() )
            {
                SW_LOG_ERROR( "A name list holds '%#', which spells the empty name - skipped", token );
                return;
            }
            outListName.push_back( name );
        } );
    }

    bool XMLReadUtil::parseFloatList( string_view text, uint32 maxCount, vector<float32>& outListValue )
    {
        vector<float32> listValue;
        bool            bValid = true;
        StringUtil::forEachToken( text, XMLReadUtilInternal::kNumberSeparators, [&]( string_view token )
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

    bool XMLReadUtil::parseFloat2( string_view text, float2& outValue )
    {
        float32 arrValue[2] = {};
        if ( XMLReadUtilInternal::parseExactFloats( text, arrValue ) == false )
            return false;
        outValue = float2{ arrValue[0], arrValue[1] };
        return true;
    }

    bool XMLReadUtil::parseFloat3( string_view text, float3& outValue )
    {
        float32 arrValue[3] = {};
        if ( XMLReadUtilInternal::parseExactFloats( text, arrValue ) == false )
            return false;
        outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
        return true;
    }

    bool XMLReadUtil::parseFloat4( string_view text, float4& outValue )
    {
        float32 arrValue[4] = {};
        if ( XMLReadUtilInternal::parseExactFloats( text, arrValue ) == false )
            return false;
        outValue = float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
        return true;
    }

    bool XMLReadUtil::readOptionalFloat2( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float2& inoutValue )
    {
        return XMLReadUtilInternal::readFloatAttribute( node, pAttribute, sourceName, false, "x y", inoutValue, &XMLReadUtil::parseFloat2 );
    }

    bool XMLReadUtil::readOptionalFloat3( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float3& inoutValue )
    {
        return XMLReadUtilInternal::readFloatAttribute( node, pAttribute, sourceName, false, "x y z", inoutValue, &XMLReadUtil::parseFloat3 );
    }

    bool XMLReadUtil::readRequiredFloat3( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float3& outValue )
    {
        return XMLReadUtilInternal::readFloatAttribute( node, pAttribute, sourceName, true, "x y z", outValue, &XMLReadUtil::parseFloat3 );
    }

    bool XMLReadUtil::readOptionalNumber( const XMLNode& node, const utf8* pAttribute, string_view sourceName, int32 minValue, int32 maxValue, int32& inoutValue )
    {
        return XMLReadUtilInternal::readOptionalInRange( node, pAttribute, sourceName, minValue, maxValue, inoutValue );
    }

    bool XMLReadUtil::readOptionalNumber( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float32 minValue, float32 maxValue, float32& inoutValue )
    {
        return XMLReadUtilInternal::readOptionalInRange( node, pAttribute, sourceName, minValue, maxValue, inoutValue );
    }

    bool XMLReadUtil::readOptionalBool( const XMLNode& node, const utf8* pAttribute, bool& inoutValue )
    {
        std::optional<bool> value;
        if ( node.readOptionalAttribute( pAttribute, value ) == false )
            return false;
        if ( value.has_value() )
            inoutValue = *value;
        return true;
    }

    bool XMLReadUtil::readOptionalFlag( const XMLNode& node, const utf8* pAttribute, uint8& inoutFlag )
    {
        bool bValue = inoutFlag != SW_FALSE;
        if ( readOptionalBool( node, pAttribute, bValue ) == false )
            return false;
        inoutFlag = bValue ? SW_TRUE : SW_FALSE;
        return true;
    }
} // namespace sw
